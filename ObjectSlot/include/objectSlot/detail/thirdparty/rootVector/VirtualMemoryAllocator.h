#pragma once

#include <cstddef>
#include <cstdint>
#include <cassert>

// ============================================================
// プラットフォーム判定マクロ
// ============================================================
// OS仮想メモリ機能（VirtualAlloc / mmap）が使える環境で定義される。
// この定義がない環境ではrealloc方式のフォールバックが使われるため、
// RootVectorのベースアドレスが変わる可能性がある。
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
	#include <cstring>
#endif

/**
 * @class VirtualMemoryAllocator
 * @brief OS固有の仮想メモリ操作を抽象化する静的ユーティリティクラス
 *
 * 【責任】
 * - 仮想アドレス空間の予約（物理メモリを消費しない領域確保）
 * - 物理メモリのページ単位でのコミット／デコミット
 * - 予約済み仮想アドレス空間の全解放
 * - OSごとのページサイズ・確保粒度の取得
 *
 * 【使用用途】
 * - RootVector等のカスタムコンテナが内部ストレージとして使用する
 * - 仮想アドレスを固定したまま物理メモリだけを段階的に追加する用途
 *
 * 【対応環境】
 * - Windows（VirtualAlloc / VirtualFree）
 * - Linux / macOS（mmap / mprotect / madvise / munmap）
 * - その他（malloc / realloc / freeによるフォールバック実装）
 *
 * 【フォールバック環境の注意】
 * - 仮想メモリの仕組みがないため、reallocで領域を拡張する
 * - Commitの戻り値でベースアドレスが変わる可能性がある
 * - ネイティブ環境では戻り値は常に入力と同じアドレスを返す
 */
class VirtualMemoryAllocator
{
public:
	VirtualMemoryAllocator() = delete;

	/// 仮想アドレス空間を予約（物理メモリはまだ割り当てない）
	static inline void* Reserve(size_t sizeBytes);

	/// 予約済み領域のコミット範囲を拡張する（戻り値は更新後のベースアドレス）
	static inline void* Commit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes);

	/// コミット済み領域を縮小して物理メモリを返却する（戻り値は更新後のベースアドレス）
	static inline void* Decommit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes);

	/// 予約した仮想アドレス空間を全て解放
	static inline void Release(void* baseAddress, size_t reservedSizeBytes);

	/// OSのメモリページサイズを取得（初回のみOS呼び出し、以降はキャッシュ値を返す）
	static inline size_t GetPageSize();

	/// OSの確保粒度を取得（初回のみOS呼び出し、以降はキャッシュ値を返す）
	static inline size_t GetAllocationGranularity();
};


// ============================================================
// ページサイズ・確保粒度のグローバルキャッシュ
// ============================================================
// プログラム起動時に一度だけOSから取得し、以降は定数として参照する。
// inline変数（C++17）によりヘッダーオンリーでもODR違反を回避する。

#if defined(_WIN32)

inline const size_t g_PageSize = []() {
	SYSTEM_INFO si;
	::GetSystemInfo(&si);
	return static_cast<size_t>(si.dwPageSize);
}();

inline const size_t g_AllocationGranularity = []() {
	SYSTEM_INFO si;
	::GetSystemInfo(&si);
	return static_cast<size_t>(si.dwAllocationGranularity);
}();

#elif defined(__linux__) || defined(__APPLE__)

inline const size_t g_PageSize = static_cast<size_t>(::sysconf(_SC_PAGESIZE));
inline const size_t g_AllocationGranularity = g_PageSize;

#else

inline constexpr size_t g_PageSize = 65536;
inline constexpr size_t g_AllocationGranularity = g_PageSize;

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
 * @param sizeBytes 予約するバイト数
 * @return 予約された仮想アドレスの先頭ポインタ。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Reserve(size_t sizeBytes)
{
	void* ptr = ::VirtualAlloc(
		nullptr,
		sizeBytes,
		MEM_RESERVE,
		PAGE_READWRITE
	);
	return ptr;
}

/**
 * @brief 予約済み領域のコミット範囲を拡張する（Windows版）
 *
 * 旧コミット境界から新コミット境界までの差分領域に対して
 * VirtualAllocのMEM_COMMITフラグで物理メモリを割り当てる。
 * 仮想アドレスは変わらないため、常に同じベースアドレスを返す。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param oldCommittedBytes 現在のコミット済みバイト数
 * @param newCommittedBytes 新しいコミット済みバイト数（oldより大きいこと）
 * @return ベースアドレス（常にbaseAddressと同じ値）。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Commit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes)
{
	char* commitStart = static_cast<char*>(baseAddress) + oldCommittedBytes;
	size_t commitSize = newCommittedBytes - oldCommittedBytes;

	void* result = ::VirtualAlloc(
		commitStart,
		commitSize,
		MEM_COMMIT,
		PAGE_READWRITE
	);

	return (result != nullptr) ? baseAddress : nullptr;
}

/**
 * @brief コミット済み領域を縮小して物理メモリを返却する（Windows版）
 *
 * 新コミット境界から旧コミット境界までの差分領域に対して
 * VirtualFreeのMEM_DECOMMITフラグで物理メモリをOSに返却する。
 * 仮想アドレス空間は維持されるため、常に同じベースアドレスを返す。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param oldCommittedBytes 現在のコミット済みバイト数
 * @param newCommittedBytes 新しいコミット済みバイト数（oldより小さいこと）
 * @return ベースアドレス（常にbaseAddressと同じ値）。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Decommit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes)
{
	char* decommitStart = static_cast<char*>(baseAddress) + newCommittedBytes;
	size_t decommitSize = oldCommittedBytes - newCommittedBytes;

	BOOL result = ::VirtualFree(
		decommitStart,
		decommitSize,
		MEM_DECOMMIT
	);

	return (result != FALSE) ? baseAddress : nullptr;
}

/**
 * @brief 予約した仮想アドレス空間を全て解放する（Windows版）
 *
 * VirtualFreeのMEM_RELEASEフラグで仮想アドレス空間ごと解放する。
 * MEM_RELEASE使用時はsizeに0を指定する必要がある（OS仕様）。
 * コミット済みの物理メモリも同時に解放される。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param reservedSizeBytes 使用しない（OS仕様で0を渡す）
 */
inline void VirtualMemoryAllocator::Release(void* baseAddress, [[maybe_unused]] size_t reservedSizeBytes)
{
	if (baseAddress)
	{
		::VirtualFree(baseAddress, 0, MEM_RELEASE);
	}
}

/**
 * @brief OSのメモリページサイズを取得する（Windows版）
 *
 * GetSystemInfoから取得するdwPageSize（通常4096バイト）を返す。
 * 初回呼び出し時にOSから取得し、以降は静的ローカル変数のキャッシュを返す。
 *
 * @return ページサイズ（バイト単位）
 */
inline size_t VirtualMemoryAllocator::GetPageSize()
{
	return g_PageSize;
}

/**
 * @brief OSの確保粒度を取得する（Windows版）
 *
 * GetSystemInfoから取得するdwAllocationGranularity（通常65536バイト = 64KB）を返す。
 * 初回呼び出し時にOSから取得し、以降は静的ローカル変数のキャッシュを返す。
 * VirtualAllocのMEM_RESERVEはこの粒度にアライメントされる。
 *
 * @return 確保粒度（バイト単位）
 */
inline size_t VirtualMemoryAllocator::GetAllocationGranularity()
{
	return g_AllocationGranularity;
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
 * @param sizeBytes 予約するバイト数
 * @return 予約された仮想アドレスの先頭ポインタ。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Reserve(size_t sizeBytes)
{
	void* ptr = ::mmap(
		nullptr,
		sizeBytes,
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
 * @brief 予約済み領域のコミット範囲を拡張する（POSIX版）
 *
 * 旧コミット境界から新コミット境界までの差分領域に対して
 * mprotectで読み書き可能（PROT_READ | PROT_WRITE）に変更する。
 * 仮想アドレスは変わらないため、常に同じベースアドレスを返す。
 *
 * mprotectはページアライメントされたアドレスを要求するため、
 * 内部でオフセットをページ境界に切り下げ、サイズをページ境界に切り上げて
 * アライメントを保証する。呼び出し側がアライメント済みの値を渡す場合でも
 * 安全に動作する（切り下げ/切り上げが空振りするだけ）。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param oldCommittedBytes 現在のコミット済みバイト数
 * @param newCommittedBytes 新しいコミット済みバイト数（oldより大きいこと）
 * @return ベースアドレス（常にbaseAddressと同じ値）。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Commit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes)
{
	const size_t pageSize = g_PageSize;

	// オフセットをページ境界に切り下げ、サイズをページ境界に切り上げ
	const size_t alignedStart = oldCommittedBytes & ~(pageSize - 1);
	const size_t alignedEnd   = (newCommittedBytes + pageSize - 1) & ~(pageSize - 1);

	char* commitStart = static_cast<char*>(baseAddress) + alignedStart;
	size_t commitSize = alignedEnd - alignedStart;

	int result = ::mprotect(
		commitStart,
		commitSize,
		PROT_READ | PROT_WRITE
	);

	return (result == 0) ? baseAddress : nullptr;
}

/**
 * @brief コミット済み領域を縮小して物理メモリを返却する（POSIX版）
 *
 * 新コミット境界から旧コミット境界までの差分領域に対して、
 * まずmadvise(MADV_DONTNEED)で物理ページの解放をカーネルに助言し、
 * 次にmprotect(PROT_NONE)でアクセス不可に戻して予約状態に復帰させる。
 * 仮想アドレスは変わらないため、常に同じベースアドレスを返す。
 *
 * mprotectはページアライメントされたアドレスを要求するため、
 * 内部でデコミット範囲のオフセットをページ境界に切り上げ、
 * 終端をページ境界に切り下げてアライメントを保証する。
 * これにより、まだ使用中のデータがあるページを誤ってデコミットしない。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param oldCommittedBytes 現在のコミット済みバイト数
 * @param newCommittedBytes 新しいコミット済みバイト数（oldより小さいこと）
 * @return ベースアドレス（常にbaseAddressと同じ値）。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Decommit(void* baseAddress, size_t oldCommittedBytes, size_t newCommittedBytes)
{
	const size_t pageSize = g_PageSize;

	// デコミット開始をページ境界に切り上げ（使用中データを守る）
	const size_t alignedStart = (newCommittedBytes + pageSize - 1) & ~(pageSize - 1);
	// デコミット終端をページ境界に切り下げ
	const size_t alignedEnd   = oldCommittedBytes & ~(pageSize - 1);

	// 切り上げ/切り下げの結果、デコミットする範囲がない場合は何もしない
	if (alignedStart >= alignedEnd)
	{
		return baseAddress;
	}

	char* decommitStart = static_cast<char*>(baseAddress) + alignedStart;
	size_t decommitSize = alignedEnd - alignedStart;

	::madvise(decommitStart, decommitSize, MADV_DONTNEED);

	int result = ::mprotect(decommitStart, decommitSize, PROT_NONE);
	return (result == 0) ? baseAddress : nullptr;
}

/**
 * @brief 予約した仮想アドレス空間を全て解放する（POSIX版）
 *
 * munmapで予約した全領域を解放する。
 * コミット済みの物理メモリも同時に返却される。
 *
 * @param baseAddress Reserve()で取得した先頭アドレス
 * @param reservedSizeBytes Reserve()に渡したバイト数と同じ値
 */
inline void VirtualMemoryAllocator::Release(void* baseAddress, size_t reservedSizeBytes)
{
	if (baseAddress)
	{
		::munmap(baseAddress, reservedSizeBytes);
	}
}

/**
 * @brief OSのメモリページサイズを取得する（POSIX版）
 *
 * sysconf(_SC_PAGESIZE)でシステムのページサイズ（通常4096バイト）を取得する。
 * 初回呼び出し時にOSから取得し、以降は静的ローカル変数のキャッシュを返す。
 *
 * @return ページサイズ（バイト単位）
 */
inline size_t VirtualMemoryAllocator::GetPageSize()
{
	return g_PageSize;
}

/**
 * @brief OSの確保粒度を取得する（POSIX版）
 *
 * POSIX環境ではmmapの確保粒度はページサイズと同じ。
 * Windowsのような64KB粒度の制約はない。
 *
 * @return 確保粒度（バイト単位、ページサイズと同値）
 */
inline size_t VirtualMemoryAllocator::GetAllocationGranularity()
{
	return GetPageSize();
}


// ============================================================
// フォールバック実装 (Emscripten等、仮想メモリ非対応環境)
// ============================================================
#else

/**
 * @brief メモリを確保する（フォールバック版）
 *
 * 仮想メモリの予約ができない環境のため、
 * mallocで指定サイズ分の実メモリを一括確保する。
 * ネイティブ環境と異なり、この時点で物理メモリを全て消費する。
 * 一括確保のためCommit()でのrealloc（アドレス移動）は発生しない。
 *
 * @param sizeBytes 確保するバイト数
 * @return 確保されたメモリの先頭ポインタ。失敗時はnullptr
 */
inline void* VirtualMemoryAllocator::Reserve(size_t sizeBytes)
{
    return std::malloc(sizeBytes);
}

/**
 * @brief 物理メモリのコミット（フォールバック版、何もしない）
 *
 * Reserveの時点で全量確保済みのため、追加のコミットは不要。
 * インターフェースの互換性のために常にbaseAddressを返す。
 *
 * @param baseAddress 現在のメモリ先頭アドレス
 * @param oldCommittedBytes 使用しない
 * @param newCommittedBytes 使用しない
 * @return baseAddress（常に同じ値）
 */
inline void* VirtualMemoryAllocator::Commit(
	void* baseAddress,
	[[maybe_unused]] size_t oldCommittedBytes,
	size_t newCommittedBytes)
{
	return baseAddress;
}

/**
 * @brief メモリ領域の縮小（フォールバック版、何もしない）
 *
 * reallocで縮小するとアドレスが移動する可能性があり、
 * フォールバック環境では縮小しても物理メモリがOSに返却される保証がないため、
 * 縮小の実益がない。そのため何もせず現在のアドレスを返す。
 *
 * @param baseAddress 現在のメモリ先頭アドレス
 * @param oldCommittedBytes 使用しない
 * @param newCommittedBytes 使用しない
 * @return baseAddress（常に同じ値）
 */
inline void* VirtualMemoryAllocator::Decommit(
	void* baseAddress,
	[[maybe_unused]] size_t oldCommittedBytes,
	[[maybe_unused]] size_t newCommittedBytes)
{
	return baseAddress;
}

/**
 * @brief 確保したメモリを全て解放する（フォールバック版）
 *
 * mallocまたはreallocで確保したメモリをfreeで解放する。
 *
 * @param baseAddress 現在のメモリ先頭アドレス
 * @param reservedSizeBytes 使用しない
 */
inline void VirtualMemoryAllocator::Release(void* baseAddress, [[maybe_unused]] size_t reservedSizeBytes)
{
	if (baseAddress)
	{
		std::free(baseAddress);
	}
}

/**
 * @brief ページサイズを取得する（フォールバック版）
 *
 * 仮想メモリの概念がない環境では、コミット粒度として
 * 65536バイト（64KB）を返す。
 * Wasmのメモリページサイズや一般的な確保粒度に合わせた値。
 *
 * @return 64KB
 */
inline size_t VirtualMemoryAllocator::GetPageSize()
{
	return g_PageSize;
}

/**
 * @brief 確保粒度を取得する（フォールバック版）
 *
 * ページサイズと同値を返す。
 *
 * @return 64KB（ページサイズと同値）
 */
inline size_t VirtualMemoryAllocator::GetAllocationGranularity()
{
	return g_AllocationGranularity;
}

#endif