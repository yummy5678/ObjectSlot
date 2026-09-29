#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

// ============================================================
// プラットフォーム判定マクロ
// ============================================================
// OS仮想メモリ機能（VirtualAlloc / mmap）が使える環境で定義される。
// この定義がない環境ではmallocによるフォールバック実装が使われる。
#if defined(_WIN32) || defined(__linux__) || defined(__APPLE__)
	#define ROOT_VECTOR_STABLE_ADDRESS
#endif

// ============================================================
// プラットフォーム別ヘッダ
// ============================================================
#if defined(_WIN32)
	#define WIN32_LEAN_AND_MEAN
	#define NOMINMAX
	#include <Windows.h>
#elif defined(__linux__) || defined(__APPLE__)
	#include <sys/mman.h>
	#include <unistd.h>
#else
	#include <cstdlib>
#endif

/**
 * @class virtual_memory_allocator
 * @brief OS固有の仮想メモリ操作を抽象化する静的ユーティリティクラス
 *
 * 【責任】
 * - 仮想アドレス空間の予約（物理メモリを消費しない領域確保）
 * - 指定した境界に揃えた仮想アドレス空間の予約（圧縮ポインタ用）
 * - 物理メモリのページ単位でのコミット／デコミット
 * - 予約済み仮想アドレス空間の全解放
 * - OSごとのページサイズ・確保粒度の取得
 *
 * 【使用用途】
 * - root_vector等のカスタムコンテナが内部ストレージとして使用する
 * - 仮想アドレスを固定したまま物理メモリだけを段階的に追加する用途
 *
 * 【対応環境】
 * - Windows（VirtualAlloc / VirtualFree、境界指定にはVirtualAlloc2を動的に利用）
 * - Linux / macOS（mmap / mprotect / madvise / munmap）
 * - その他（malloc / freeによるフォールバック実装）
 *
 * 【フォールバック環境の注意】
 * - 仮想メモリの仕組みがないため、reserveでmallocにより全量確保する
 * - commit/decommitは確保済みのため何もしない
 * - 容量の拡張はroot_vector側がmalloc+ムーブ+freeで行う
 */
class virtual_memory_allocator
{
public:
	virtual_memory_allocator() = delete;

	/// 仮想アドレス空間を予約（物理メモリはまだ割り当てない）
	static inline void* reserve(size_t size_bytes);

	/**
	 * @brief 指定した境界に揃えて仮想アドレス空間を予約する
	 *
	 * 返される先頭アドレスは alignment の倍数になる。
	 * OSによっては要求より大きい領域を確保して内側を使うため、
	 * 解放時には out_origin / out_origin_bytes を release() に渡すこと。
	 *
	 * @param size_bytes       予約するバイト数
	 * @param alignment        先頭アドレスを揃える境界（2のべき乗）
	 * @param out_origin       [out] 解放時に渡す実際の確保先頭アドレス
	 * @param out_origin_bytes [out] 解放時に渡す実際の確保バイト数
	 * @return 境界に揃った先頭アドレス。失敗時はnullptr
	 */
	static inline void* reserve_aligned(size_t size_bytes, size_t alignment, void** out_origin, size_t* out_origin_bytes);

	/// 予約済み領域のコミット範囲を拡張する（戻り値は更新後のベースアドレス）
	static inline void* commit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes);

	/// コミット済み領域を縮小して物理メモリを返却する（戻り値は更新後のベースアドレス）
	static inline void* decommit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes);

	/// 予約した仮想アドレス空間を全て解放
	static inline void release(void* base_address, size_t reserved_size_bytes);

	/// OSのメモリページサイズを取得（グローバルキャッシュから返す）
	static inline size_t get_page_size();

	/// OSの確保粒度を取得（グローバルキャッシュから返す）
	static inline size_t get_allocation_granularity();

private:
	/// 値を指定境界の倍数に切り上げる
	static inline uintptr_t align_up_address(uintptr_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(static_cast<uintptr_t>(alignment) - 1);
	}
};


// ============================================================
// ページサイズ・確保粒度のグローバルキャッシュ
// ============================================================
// プログラム起動時に一度だけOSから取得し、以降は定数として参照する。
// inline変数（C++17）によりヘッダーオンリーでもODR違反を回避する。

#if defined(_WIN32)

inline const size_t g_page_size = []() {
	SYSTEM_INFO si;
	::GetSystemInfo(&si);
	return static_cast<size_t>(si.dwPageSize);
}();

inline const size_t g_allocation_granularity = []() {
	SYSTEM_INFO si;
	::GetSystemInfo(&si);
	return static_cast<size_t>(si.dwAllocationGranularity);
}();

#elif defined(__linux__) || defined(__APPLE__)

inline const size_t g_page_size = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
inline const size_t g_allocation_granularity = g_page_size;

#else

inline constexpr size_t g_page_size = 65536;
inline constexpr size_t g_allocation_granularity = g_page_size;

#endif


// ============================================================
// Windows 実装
// ============================================================
#if defined(_WIN32)

/**
 * @brief 仮想アドレス空間を予約する（Windows版）
 *
 * VirtualAllocのMEM_RESERVEフラグで仮想アドレス空間だけを確保する。
 * この時点では物理メモリは消費されない。
 * 確保サイズはOS確保粒度（通常64KB）に切り上げられる。
 *
 * @param size_bytes 予約するバイト数
 * @return 予約された仮想アドレスの先頭ポインタ。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve(size_t size_bytes)
{
	void* ptr = ::VirtualAlloc(
		nullptr,
		size_bytes,
		MEM_RESERVE,
		PAGE_READWRITE
	);
	return ptr;
}

/**
 * @brief 指定した境界に揃えて仮想アドレス空間を予約する（Windows版）
 *
 * 【手順】
 * 1. VirtualAlloc2 が使える環境（Windows 10 1803以降かつ対応SDK）であれば、
 *    MEM_ADDRESS_REQUIREMENTS で境界を直接指定して予約する。
 *    この場合は要求サイズぴったりの領域が返る。
 * 2. 使えない環境では、要求サイズ＋境界分を余分に予約し、
 *    その内側にある最初の境界を先頭アドレスとして使う。
 *    Windowsは予約の一部だけを解放できないため、余分な仮想アドレス空間は
 *    解放時までそのまま残る（物理メモリは消費しない）。
 *
 * VirtualAlloc2 はリンク時に解決せず、実行時に GetProcAddress で探す。
 * これにより古いWindowsでもバイナリが起動し、手順2に自動的に切り替わる。
 *
 * @param size_bytes       予約するバイト数
 * @param alignment        先頭アドレスを揃える境界（2のべき乗）
 * @param out_origin       [out] 解放時に渡す実際の確保先頭アドレス
 * @param out_origin_bytes [out] 解放時に渡す実際の確保バイト数
 * @return 境界に揃った先頭アドレス。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve_aligned(size_t size_bytes, size_t alignment, void** out_origin, size_t* out_origin_bytes)
{
	assert((alignment & (alignment - 1)) == 0 && "境界は2のべき乗である必要があります。");

	// ---- 手順1: VirtualAlloc2 による直接指定 ----
	// MEM_EXTENDED_PARAMETER が宣言されているSDKでのみコンパイルされる。
#if defined(MEM_EXTENDED_PARAMETER_TYPE_BITS)
	using VirtualAlloc2Function = PVOID(WINAPI*)(
		HANDLE, PVOID, SIZE_T, ULONG, ULONG, MEM_EXTENDED_PARAMETER*, ULONG);

	// 一度だけ探して結果を保持する（見つからなければnullptrのまま）
	static const VirtualAlloc2Function virtual_alloc2 = []() -> VirtualAlloc2Function {
		HMODULE module = ::GetModuleHandleW(L"kernelbase.dll");
		if (module == nullptr) return nullptr;
		return reinterpret_cast<VirtualAlloc2Function>(::GetProcAddress(module, "VirtualAlloc2"));
	}();

	if (virtual_alloc2 != nullptr)
	{
		MEM_ADDRESS_REQUIREMENTS requirements = {};
		requirements.Alignment = alignment;

		MEM_EXTENDED_PARAMETER parameter = {};
		parameter.Type = MemExtendedParameterAddressRequirements;
		parameter.Pointer = &requirements;

		void* ptr = virtual_alloc2(
			::GetCurrentProcess(),
			nullptr,
			size_bytes,
			MEM_RESERVE,
			PAGE_READWRITE,
			&parameter,
			1
		);

		if (ptr != nullptr)
		{
			*out_origin = ptr;
			*out_origin_bytes = size_bytes;
			return ptr;
		}
		// 失敗した場合は手順2へ落とす
	}
#endif

	// ---- 手順2: 余分に予約して内側の境界を使う ----
	const size_t padded_bytes = size_bytes + alignment;
	void* origin = ::VirtualAlloc(nullptr, padded_bytes, MEM_RESERVE, PAGE_READWRITE);
	if (origin == nullptr)
	{
		*out_origin = nullptr;
		*out_origin_bytes = 0;
		return nullptr;
	}

	const uintptr_t aligned = align_up_address(reinterpret_cast<uintptr_t>(origin), alignment);
	*out_origin = origin;
	*out_origin_bytes = padded_bytes;
	return reinterpret_cast<void*>(aligned);
}

/**
 * @brief 予約済み領域のコミット範囲を拡張する（Windows版）
 *
 * 旧コミット境界から新コミット境界までの差分領域に対して
 * VirtualAllocのMEM_COMMITフラグで物理メモリを割り当てる。
 * 仮想アドレスは変わらないため、常に同じベースアドレスを返す。
 *
 * @param base_address reserve()で取得した先頭アドレス
 * @param old_committed_bytes 現在のコミット済みバイト数
 * @param new_committed_bytes 新しいコミット済みバイト数（oldより大きいこと）
 * @return ベースアドレス（常にbase_addressと同じ値）。失敗時はnullptr
 */
inline void* virtual_memory_allocator::commit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes)
{
	char* commit_start = static_cast<char*>(base_address) + old_committed_bytes;
	size_t commit_size = new_committed_bytes - old_committed_bytes;

	void* result = ::VirtualAlloc(
		commit_start,
		commit_size,
		MEM_COMMIT,
		PAGE_READWRITE
	);

	return (result != nullptr) ? base_address : nullptr;
}

/**
 * @brief コミット済み領域を縮小して物理メモリを返却する（Windows版）
 *
 * 新コミット境界から旧コミット境界までの差分領域に対して
 * VirtualFreeのMEM_DECOMMITフラグで物理メモリをOSに返却する。
 * 仮想アドレス空間は維持されるため、常に同じベースアドレスを返す。
 *
 * @param base_address reserve()で取得した先頭アドレス
 * @param old_committed_bytes 現在のコミット済みバイト数
 * @param new_committed_bytes 新しいコミット済みバイト数（oldより小さいこと）
 * @return ベースアドレス（常にbase_addressと同じ値）。失敗時はnullptr
 */
inline void* virtual_memory_allocator::decommit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes)
{
	char* decommit_start = static_cast<char*>(base_address) + new_committed_bytes;
	size_t decommit_size = old_committed_bytes - new_committed_bytes;

	BOOL result = ::VirtualFree(
		decommit_start,
		decommit_size,
		MEM_DECOMMIT
	);

	return (result != FALSE) ? base_address : nullptr;
}

/**
 * @brief 予約した仮想アドレス空間を全て解放する（Windows版）
 *
 * VirtualFreeのMEM_RELEASEフラグで仮想アドレス空間ごと解放する。
 * MEM_RELEASE使用時はsizeに0を指定する必要がある（OS仕様）。
 * コミット済みの物理メモリも同時に解放される。
 *
 * reserve_aligned() で予約した領域は、out_origin で受け取った
 * 実際の確保先頭アドレスを渡すこと（揃えた後のアドレスではない）。
 *
 * @param base_address reserve()またはreserve_alignedのout_originで取得した先頭アドレス
 * @param reserved_size_bytes 使用しない（OS仕様で0を渡す）
 */
inline void virtual_memory_allocator::release(void* base_address, [[maybe_unused]] size_t reserved_size_bytes)
{
	if (base_address)
	{
		::VirtualFree(base_address, 0, MEM_RELEASE);
	}
}

/**
 * @brief OSのメモリページサイズを取得する（Windows版）
 *
 * グローバルキャッシュから返す。通常4096バイト。
 *
 * @return ページサイズ（バイト単位）
 */
inline size_t virtual_memory_allocator::get_page_size()
{
	return g_page_size;
}

/**
 * @brief OSの確保粒度を取得する（Windows版）
 *
 * グローバルキャッシュから返す。通常65536バイト（64KB）。
 * VirtualAllocのMEM_RESERVEはこの粒度にアライメントされる。
 *
 * @return 確保粒度（バイト単位）
 */
inline size_t virtual_memory_allocator::get_allocation_granularity()
{
	return g_allocation_granularity;
}


// ============================================================
// Linux / macOS 実装 (POSIX)
// ============================================================
#elif defined(__linux__) || defined(__APPLE__)

/**
 * @brief 仮想アドレス空間を予約する（POSIX版）
 *
 * mmapでPROT_NONE（アクセス不可）として仮想アドレス空間だけを確保する。
 * MAP_PRIVATE | MAP_ANONYMOUSでファイル非関連の匿名マッピングを作る。
 * この時点では物理メモリはほぼ消費されない（ページテーブルのみ）。
 *
 * @param size_bytes 予約するバイト数
 * @return 予約された仮想アドレスの先頭ポインタ。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve(size_t size_bytes)
{
	void* ptr = ::mmap(
		nullptr,
		size_bytes,
		PROT_NONE,
		MAP_PRIVATE | MAP_ANONYMOUS,
		-1,
		0
	);

	if (ptr == MAP_FAILED)
	{
		return nullptr;
	}
	return ptr;
}

/**
 * @brief 指定した境界に揃えて仮想アドレス空間を予約する（POSIX版）
 *
 * 要求サイズ＋境界分を余分に確保し、境界の手前と後ろを munmap で切り落とす。
 * munmap は部分解放ができるため、最終的にぴったりのサイズが残る。
 * このため out_origin は揃えた後のアドレスと同じ値になる。
 *
 * @param size_bytes       予約するバイト数
 * @param alignment        先頭アドレスを揃える境界（2のべき乗）
 * @param out_origin       [out] 解放時に渡す実際の確保先頭アドレス（揃えた後と同じ）
 * @param out_origin_bytes [out] 解放時に渡す実際の確保バイト数（size_bytesと同じ）
 * @return 境界に揃った先頭アドレス。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve_aligned(size_t size_bytes, size_t alignment, void** out_origin, size_t* out_origin_bytes)
{
	assert((alignment & (alignment - 1)) == 0 && "境界は2のべき乗である必要があります。");

	const size_t padded_bytes = size_bytes + alignment;
	void* raw = ::mmap(nullptr, padded_bytes, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (raw == MAP_FAILED)
	{
		*out_origin = nullptr;
		*out_origin_bytes = 0;
		return nullptr;
	}

	const uintptr_t raw_address = reinterpret_cast<uintptr_t>(raw);
	const uintptr_t aligned_address = align_up_address(raw_address, alignment);

	// 境界の手前を切り落とす
	const size_t head_bytes = static_cast<size_t>(aligned_address - raw_address);
	if (head_bytes > 0)
	{
		::munmap(raw, head_bytes);
	}

	// 境界の後ろ（要求サイズを超えた分）を切り落とす
	const size_t tail_bytes = padded_bytes - head_bytes - size_bytes;
	if (tail_bytes > 0)
	{
		::munmap(reinterpret_cast<void*>(aligned_address + size_bytes), tail_bytes);
	}

	void* aligned = reinterpret_cast<void*>(aligned_address);
	*out_origin = aligned;
	*out_origin_bytes = size_bytes;
	return aligned;
}

/**
 * @brief 予約済み領域のコミット範囲を拡張する（POSIX版）
 *
 * 旧コミット境界から新コミット境界までの差分領域に対して
 * mprotectで読み書き可能（PROT_READ | PROT_WRITE）に変更する。
 * 仮想アドレスは変わらないため、常に同じベースアドレスを返す。
 *
 * mprotectはページアライメントされたアドレスを要求するため、
 * 内部でオフセットをページ境界に切り下げ、サイズをページ境界に切り上げて
 * アライメントを保証する。
 *
 * @param base_address reserve()で取得した先頭アドレス
 * @param old_committed_bytes 現在のコミット済みバイト数
 * @param new_committed_bytes 新しいコミット済みバイト数（oldより大きいこと）
 * @return ベースアドレス（常にbase_addressと同じ値）。失敗時はnullptr
 */
inline void* virtual_memory_allocator::commit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes)
{
	const size_t page_size = g_page_size;

	const size_t aligned_start = old_committed_bytes & ~(page_size - 1);
	const size_t aligned_end   = (new_committed_bytes + page_size - 1) & ~(page_size - 1);

	char* commit_start = static_cast<char*>(base_address) + aligned_start;
	size_t commit_size = aligned_end - aligned_start;

	int result = ::mprotect(
		commit_start,
		commit_size,
		PROT_READ | PROT_WRITE
	);

	return (result == 0) ? base_address : nullptr;
}

/**
 * @brief コミット済み領域を縮小して物理メモリを返却する（POSIX版）
 *
 * 新コミット境界から旧コミット境界までの差分領域に対して、
 * madvise(MADV_DONTNEED)で物理ページの解放をカーネルに助言し、
 * mprotect(PROT_NONE)でアクセス不可に戻して予約状態に復帰させる。
 *
 * ページアライメントを内部で保証するため、デコミット開始はページ境界に切り上げ、
 * 終端はページ境界に切り下げる。使用中のデータを誤ってデコミットしない。
 *
 * @param base_address reserve()で取得した先頭アドレス
 * @param old_committed_bytes 現在のコミット済みバイト数
 * @param new_committed_bytes 新しいコミット済みバイト数（oldより小さいこと）
 * @return ベースアドレス（常にbase_addressと同じ値）。失敗時はnullptr
 */
inline void* virtual_memory_allocator::decommit(void* base_address, size_t old_committed_bytes, size_t new_committed_bytes)
{
	const size_t page_size = g_page_size;

	const size_t aligned_start = (new_committed_bytes + page_size - 1) & ~(page_size - 1);
	const size_t aligned_end   = old_committed_bytes & ~(page_size - 1);

	if (aligned_start >= aligned_end)
	{
		return base_address;
	}

	char* decommit_start = static_cast<char*>(base_address) + aligned_start;
	size_t decommit_size = aligned_end - aligned_start;

	::madvise(decommit_start, decommit_size, MADV_DONTNEED);

	int result = ::mprotect(decommit_start, decommit_size, PROT_NONE);
	return (result == 0) ? base_address : nullptr;
}

/**
 * @brief 予約した仮想アドレス空間を全て解放する（POSIX版）
 *
 * munmapで予約した全領域を解放する。
 * コミット済みの物理メモリも同時に返却される。
 *
 * @param base_address reserve()で取得した先頭アドレス
 * @param reserved_size_bytes reserve()に渡したバイト数と同じ値
 */
inline void virtual_memory_allocator::release(void* base_address, size_t reserved_size_bytes)
{
	if (base_address)
	{
		::munmap(base_address, reserved_size_bytes);
	}
}

/**
 * @brief OSのメモリページサイズを取得する（POSIX版）
 *
 * グローバルキャッシュから返す。通常4096バイト。
 *
 * @return ページサイズ（バイト単位）
 */
inline size_t virtual_memory_allocator::get_page_size()
{
	return g_page_size;
}

/**
 * @brief OSの確保粒度を取得する（POSIX版）
 *
 * POSIX環境ではページサイズと同値。
 *
 * @return 確保粒度（バイト単位、ページサイズと同値）
 */
inline size_t virtual_memory_allocator::get_allocation_granularity()
{
	return get_page_size();
}


// ============================================================
// フォールバック実装 (Emscripten等、仮想メモリ非対応環境)
// ============================================================
#else

/**
 * @brief 指定サイズのメモリを確保する（フォールバック版）
 *
 * 仮想メモリの予約ができない環境のため、
 * mallocで指定サイズ分の実メモリを一括確保する。
 * ネイティブ環境と異なり、この時点で物理メモリを全て消費する。
 *
 * @param size_bytes 確保するバイト数
 * @return 確保されたメモリの先頭ポインタ。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve(size_t size_bytes)
{
	return std::malloc(size_bytes);
}

/**
 * @brief 境界に揃えた確保（フォールバック版）
 *
 * この環境では圧縮ポインタが無効なため通常は呼ばれないが、
 * 万一呼ばれても動作するように malloc で余分に確保して内側を揃える。
 *
 * @param size_bytes       確保するバイト数
 * @param alignment        先頭アドレスを揃える境界
 * @param out_origin       [out] 解放時に渡す実際の確保先頭アドレス
 * @param out_origin_bytes [out] 解放時に渡す実際の確保バイト数
 * @return 境界に揃った先頭アドレス。失敗時はnullptr
 */
inline void* virtual_memory_allocator::reserve_aligned(size_t size_bytes, size_t alignment, void** out_origin, size_t* out_origin_bytes)
{
	const size_t padded_bytes = size_bytes + alignment;
	void* origin = std::malloc(padded_bytes);
	if (origin == nullptr)
	{
		*out_origin = nullptr;
		*out_origin_bytes = 0;
		return nullptr;
	}
	*out_origin = origin;
	*out_origin_bytes = padded_bytes;
	return reinterpret_cast<void*>(align_up_address(reinterpret_cast<uintptr_t>(origin), alignment));
}

/**
 * @brief 物理メモリのコミット（フォールバック版、何もしない）
 *
 * reserveの時点で全量確保済みのため、追加のコミットは不要。
 *
 * @param base_address 現在のメモリ先頭アドレス
 * @param old_committed_bytes 使用しない
 * @param new_committed_bytes 使用しない
 * @return base_address（常に同じ値）
 */
inline void* virtual_memory_allocator::commit(
	void* base_address,
	[[maybe_unused]] size_t old_committed_bytes,
	[[maybe_unused]] size_t new_committed_bytes)
{
	return base_address;
}

/**
 * @brief 物理メモリのデコミット（フォールバック版、何もしない）
 *
 * mallocで確保したメモリはページ単位で返却できないため、何もしない。
 *
 * @param base_address 現在のメモリ先頭アドレス
 * @param old_committed_bytes 使用しない
 * @param new_committed_bytes 使用しない
 * @return base_address（常に同じ値）
 */
inline void* virtual_memory_allocator::decommit(
	void* base_address,
	[[maybe_unused]] size_t old_committed_bytes,
	[[maybe_unused]] size_t new_committed_bytes)
{
	return base_address;
}

/**
 * @brief 確保したメモリを全て解放する（フォールバック版）
 *
 * @param base_address 現在のメモリ先頭アドレス
 * @param reserved_size_bytes 使用しない
 */
inline void virtual_memory_allocator::release(void* base_address, [[maybe_unused]] size_t reserved_size_bytes)
{
	if (base_address)
	{
		std::free(base_address);
	}
}

/**
 * @brief ページサイズを取得する（フォールバック版）
 *
 * コミット粒度として65536バイト（64KB）を返す。
 *
 * @return 64KB
 */
inline size_t virtual_memory_allocator::get_page_size()
{
	return g_page_size;
}

/**
 * @brief 確保粒度を取得する（フォールバック版）
 *
 * @return 64KB（ページサイズと同値）
 */
inline size_t virtual_memory_allocator::get_allocation_granularity()
{
	return g_allocation_granularity;
}

#endif
