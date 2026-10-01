#pragma once

#include "SlotHeader.h"
#include "../thirdparty/rootVector/VirtualMemoryAllocator.h"

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <algorithm>
#include <vector>
#include <utility>

// ============================================================
// 動作環境の確認
// ============================================================
// この実装は「型ごとに4GBの仮想アドレスを4GB境界に揃えて予約する」ことを
// 前提にしている。そのため次の両方を満たす環境でしか使えない。
//   - OS の仮想メモリ機能が使える（VirtualAlloc / mmap）
//   - 64ビット環境である
// それ以外の環境（WebAssembly、32ビット等）では ObjectSlot.h が
// 自動的に従来の実装（detail/）へ切り替えるので、ここに来ることはない。
#if !defined(ROOT_VECTOR_STABLE_ADDRESS) || (UINTPTR_MAX <= 0xFFFFFFFFu)
    #error "experimental_detail は OS 仮想メモリが使える 64 ビット環境専用です。ObjectSlot.h を通して含めてください。"
#endif

/**
 * @brief SlotStorageの定数を求めるためのコンパイル時計算
 */
struct SlotStorageMath {
    SlotStorageMath() = delete;

    /// 値を境界の倍数に切り上げる
    static constexpr size_t AlignUp(size_t value, size_t alignment) {
        return (value + alignment - 1) / alignment * alignment;
    }
};

/**
 * @class SlotStorage
 * @brief プール1つ分の「スロット」と「要素本体」を、ポインタを無効にせずに保持するストレージ
 *
 * 【責任】
 * - スロット（見出し＋位置表の項目）を確保し、二度と移動させない
 * - 要素本体の領域を確保し、Compact で本体だけを詰め直す
 * - 圧縮値（4バイト）からスロット・本体のアドレスを求める
 * - 未使用領域をOSへ返却する（Truncate / TrimFreeBodies）
 *
 * 【スロットと本体を分ける理由】
 * ポインタが持つ圧縮値は「スロット番号」であって本体の位置ではない。
 * スロットは動かないので、本体を詰め直してもポインタは有効なまま。
 * 本体の現在位置はスロットごとの位置表に入っており、
 * 要素へのアクセスは「位置表[番号] を読んで基底に足す」の1段間接になる。
 *
 *   ポインタの値      = [区画番号 4bit][スロット番号 28bit]
 *   位置表[番号]      = 本体の位置（区画番号 4bit ＋ 区画内オフセット 28bit）
 *   見出し[番号]      = 参照カウント・世代番号・自分の圧縮値・プール番号
 *   本体              = sizeof(T) 間隔で並ぶ。Compact で先頭に寄せられる
 *
 * 【領域の配置（共有領域方式）】
 * - 型ごとに4GBの仮想アドレス空間を1つ予約する（アドレスは「基底＋32ビットの値」で求める）
 * - 領域は区画の大きさ（256MB）の境界に揃える。SlotRef が見出しのアドレスから
 *   区画の先頭をマスク演算で逆算するためで、SlotPtr のアクセスには揃えは不要
 * - 256MBずつ16区画に分け、プールはタグ番号の区画を使う（区画15は無効値用）
 * - 区画の中を「位置表」「見出しの並び」「本体の並び」の3つに分ける
 * - 位置表を区画の先頭に置くので、項目のアドレスは「区画の先頭＋番号×4」で
 *   要素の型に依存しない。型を知らない SlotRef が本体まで辿れるのはこのため
 * - 物理メモリはそれぞれページ単位（通常4KB）で必要分だけコミットする
 *
 * 【注意事項】
 * - 要素が破棄されても見出しは読める状態が保たれる。
 *   古いポインタが安全に「無効」と判定できるのはこのため
 * - Truncateで切り詰めた範囲だけは領域が返却される
 * - このストレージの破棄時に領域は返却しない（静的破棄の順序によらず
 *   古いポインタが安全に無効判定できるようにするため）
 *
 * @tparam T 要素の型
 */
template<typename T>
class SlotStorage {
public:
    /** 本体1つのバイト数（＝本体の並びの間隔） */
    static constexpr size_t ELEMENT_BYTES = sizeof(T);

    /** 見出し1つのバイト数 */
    static constexpr size_t HEADER_BYTES = sizeof(SlotHeader);

    /** 位置表の項目1つのバイト数 */
    static constexpr size_t ENTRY_BYTES = sizeof(uint32_t);

    static_assert(ELEMENT_BYTES + HEADER_BYTES + ENTRY_BYTES <= SlotValue::REGION_BYTES / 4,
        "要素が大きすぎます。1要素は64MB未満である必要があります。");

    /** 型ごとに予約する共有領域のバイト数 */
    static constexpr size_t ARENA_BYTES = static_cast<size_t>(1) << 32;

    /** 共有領域の先頭を揃える境界（区画の大きさ。SlotRef が見出しのアドレスから区画の先頭を逆算するため） */
    static constexpr size_t ARENA_ALIGNMENT = SlotValue::REGION_BYTES;

    /** 各並びの先頭を揃える境界（並びごとに別々にコミットするため、どの環境のページ境界にも乗る64KBにする） */
    static constexpr size_t AREA_ALIGNMENT = 65536;

    /** 1プールに入るスロット数の上限（本体＋位置表＋見出しが区画に収まる数） */
    static constexpr uint32_t MAX_RECORDS =
        static_cast<uint32_t>((SlotValue::REGION_BYTES - AREA_ALIGNMENT * 2) / (ELEMENT_BYTES + ENTRY_BYTES + HEADER_BYTES));

    /** 区画内で位置表が始まるオフセット（先頭。SlotRef が型を知らずに辿れるよう固定） */
    static constexpr size_t TABLE_AREA_OFFSET = 0;

    /** 区画内で見出しの並びが始まるオフセット */
    static constexpr size_t HEADER_AREA_OFFSET = SlotStorageMath::AlignUp(TABLE_AREA_OFFSET + MAX_RECORDS * ENTRY_BYTES, AREA_ALIGNMENT);

    /** 区画内で本体の並びが始まるオフセット */
    static constexpr size_t BODY_AREA_OFFSET = SlotStorageMath::AlignUp(HEADER_AREA_OFFSET + MAX_RECORDS * HEADER_BYTES, AREA_ALIGNMENT);

    static_assert(BODY_AREA_OFFSET + static_cast<size_t>(MAX_RECORDS) * ELEMENT_BYTES <= SlotValue::REGION_BYTES,
        "本体の並びが区画に収まらない。");

    SlotStorage() = default;
    ~SlotStorage() = default;
    SlotStorage(const SlotStorage&) = delete;
    SlotStorage& operator=(const SlotStorage&) = delete;

    /// このストレージが使うタグ（区画番号）を設定する。スロットを追加する前に1度だけ呼ぶ
    void SetTag(uint8_t tag) { m_tag = tag; }

    /// タグを取得
    uint8_t GetTag() const { return m_tag; }

    /// 現在のスロット数を取得
    uint32_t Size() const { return m_size; }

    /// 現在の本体数（Compact 前は空きを含む）を取得
    uint32_t BodyCount() const { return m_bodyCount; }

    /// 入れられるスロット数の上限を取得
    static constexpr uint32_t MaxRecords() { return MAX_RECORDS; }

    // ================================================================
    // 圧縮値との変換
    // ================================================================

    /// スロット番号からこのストレージの圧縮値を求める
    uint32_t ValueOf(uint32_t slotIndex) const {
        return SlotValue::Make(m_tag, slotIndex);
    }

    /// 圧縮値からスロット番号を求める
    static uint32_t IndexFromValue(uint32_t value) {
        return SlotValue::Offset(value);
    }

    /**
     * @brief 圧縮値から位置表の項目のアドレスを求める（検証なし。区画の先頭＋番号×4）
     *
     * 項目は動かないので、SlotRef は圧縮値からこのアドレスを求めて要素を追いかける。
     */
    static uint32_t* EntryAt(uint32_t value) {
        const uintptr_t regionBase = s_arenaBase + (value & ~SlotValue::OFFSET_MASK);
        return reinterpret_cast<uint32_t*>(regionBase + TABLE_AREA_OFFSET + static_cast<size_t>(SlotValue::Offset(value)) * ENTRY_BYTES);
    }

    /// 圧縮値から見出しのアドレスを求める（検証なし）
    static SlotHeader* HeaderAt(uint32_t value) {
        const uintptr_t regionBase = s_arenaBase + (value & ~SlotValue::OFFSET_MASK);
        return reinterpret_cast<SlotHeader*>(regionBase + HEADER_AREA_OFFSET + static_cast<size_t>(SlotValue::Offset(value)) * HEADER_BYTES);
    }

    /**
     * @brief 本体の位置（位置表の値）から本体のアドレスを求める（検証なし）
     *
     * 基底アドレスと値の足し算1つで求まる（ロード命令のアドレス計算に畳み込まれる）。
     */
    static T* BodyAt(uint32_t bodyValue) {
        return reinterpret_cast<T*>(s_arenaBase + bodyValue);
    }

    /// 圧縮値から要素本体のアドレスを求める（検証なし。位置表を1回引く）
    static T* ElementAt(uint32_t value) {
        return BodyAt(*EntryAt(value));
    }

    /// 圧縮値から要素本体と見出しをまとめて求める（検証なし）
    static void Locate(uint32_t value, T*& outElement, SlotHeader*& outHeader) {
        outElement = ElementAt(value);
        outHeader = HeaderAt(value);
    }

    // ================================================================
    // スロット番号によるアクセス
    // ================================================================

    /// スロット番号から要素本体を取得（範囲検証なし。構築済みかどうかはプールが管理する）
    T* Element(uint32_t slotIndex) { return ElementAt(ValueOf(slotIndex)); }

    /// スロット番号から要素本体を取得（const版）
    const T* Element(uint32_t slotIndex) const { return ElementAt(ValueOf(slotIndex)); }

    /// スロット番号から見出しを取得（範囲検証なし）
    SlotHeader* Header(uint32_t slotIndex) { return HeaderAt(ValueOf(slotIndex)); }

    /// スロット番号から見出しを取得（const版）
    const SlotHeader* Header(uint32_t slotIndex) const { return HeaderAt(ValueOf(slotIndex)); }

    /// スロット番号から位置表の項目を取得
    uint32_t* Entry(uint32_t slotIndex) { return EntryAt(ValueOf(slotIndex)); }

    /// スロット番号から本体の添字（本体の並びの中の位置）を求める
    uint32_t BodyIndexOf(uint32_t slotIndex) const {
        return BodyIndexFromValue(*EntryAt(ValueOf(slotIndex)));
    }

    // ================================================================
    // スロットと本体の確保
    // ================================================================

    /**
     * @brief 末尾にスロットを1つ追加する
     *
     * 初めて使うスロットは見出しを初期化し、一度使われて切り詰められていない
     * スロットは見出し（世代番号）をそのまま引き継ぐ。本体はまだ割り当てない。
     *
     * @return 追加したスロットの番号
     */
    uint32_t PushBackSlot() {
        EnsureSlotCapacity(m_size + 1);
        const uint32_t slotIndex = m_size++;
        if (slotIndex >= m_highWater) {
            *Header(slotIndex) = SlotHeader{};
            m_highWater = m_size;
        }
        return slotIndex;
    }

    /**
     * @brief スロットに本体の領域を割り当てる
     *
     * 空いている本体があればそれを、なければ末尾の本体を割り当て、位置表に記録する。
     * 本体は構築しない（プールが構築する）。
     *
     * @param slotIndex 本体を割り当てるスロット
     * @return 割り当てた本体へのポインタ（未構築）
     */
    T* AttachBody(uint32_t slotIndex) {
        uint32_t bodyIndex = 0;
        if (!m_freeBodies.empty()) {
            bodyIndex = m_freeBodies.back();
            m_freeBodies.pop_back();
        }
        else {
            EnsureBodyCapacity(m_bodyCount + 1);
            bodyIndex = m_bodyCount++;
        }
        const uint32_t bodyValue = BodyValueOf(bodyIndex);
        *Entry(slotIndex) = bodyValue;
        return BodyAt(bodyValue);
    }

    /**
     * @brief スロットから本体の領域を切り離す（本体は破棄済みであること）
     *
     * 本体の場所は空きとして記録し、次の AttachBody で再利用する。
     *
     * @param slotIndex 本体を切り離すスロット
     */
    void DetachBody(uint32_t slotIndex) {
        uint32_t* entry = Entry(slotIndex);
        m_freeBodies.push_back(BodyIndexFromValue(*entry));
        *entry = SlotValue::INVALID;
    }

    /// 指定数のスロットと本体が入るよう領域を先に確保する
    void Reserve(uint32_t count) {
        EnsureSlotCapacity(count);
        EnsureBodyCapacity(count);
    }

    /// スロット数と本体数を0にする（領域と見出しは残す）
    void ResetSize() {
        m_size = 0;
        m_bodyCount = 0;
        m_freeBodies.clear();
    }

    // ================================================================
    // 詰め直しと切り詰め
    // ================================================================

    /**
     * @brief 生存している本体を先頭から隙間なく並べ直す
     *
     * 渡されたスロットを現在の本体位置の昇順に並べ替え、その順に本体を
     * 0, 1, 2, ... の位置へムーブする。位置表を書き換えるので、スロット番号を
     * 持つポインタは全て有効なまま。
     * 呼び出し後、liveSlots は本体の並び順になっている。
     *
     * @param liveSlots [in/out] 生存しているスロット番号の一覧
     */
    void Compact(std::vector<uint32_t>& liveSlots) {
        std::sort(liveSlots.begin(), liveSlots.end(),
            [this](uint32_t a, uint32_t b) { return BodyIndexOf(a) < BodyIndexOf(b); });

        for (uint32_t destination = 0; destination < liveSlots.size(); ++destination) {
            const uint32_t slotIndex = liveSlots[destination];
            uint32_t* entry = Entry(slotIndex);
            const uint32_t source = BodyIndexFromValue(*entry);
            if (source == destination) continue;

            // 昇順に処理しているので移動先は常に移動元より手前で、まだ誰も使っていない
            T* from = BodyAt(*entry);
            const uint32_t destinationValue = BodyValueOf(destination);
            T* to = BodyAt(destinationValue);
            new (to) T(std::move(*from));
            from->~T();
            *entry = destinationValue;
        }

        m_freeBodies.clear();
        m_bodyCount = static_cast<uint32_t>(liveSlots.size());
        ReleaseBodiesBeyond(m_bodyCount);
    }

    /**
     * @brief 末尾のスロットを切り詰め、不要になった領域を返却する
     *
     * 切り詰めた範囲の見出しは読めなくなるため、その範囲を指す古いポインタは
     * 呼び出し後に触ってはいけない。本体は、切り詰めるスロットの分を空きから外し、
     * 末尾の空き本体を返却する。
     *
     * @param newSize 切り詰め後のスロット数
     */
    void Truncate(uint32_t newSize) {
        if (newSize >= m_size) return;
        m_size = newSize;
        m_highWater = std::min(m_highWater, newSize);
        ReleaseSlotsBeyond(newSize);
        TrimFreeBodies();
    }

    /**
     * @brief 末尾に連続する空き本体を返却する（スロットは変えない）
     */
    void TrimFreeBodies() {
        if (m_freeBodies.empty()) return;
        std::sort(m_freeBodies.begin(), m_freeBodies.end());
        // 末尾から連続している空きを数える
        uint32_t trailing = 0;
        while (trailing < m_freeBodies.size()
            && m_freeBodies[m_freeBodies.size() - 1 - trailing] == m_bodyCount - 1 - trailing) {
            ++trailing;
        }
        if (trailing == 0) return;
        m_freeBodies.resize(m_freeBodies.size() - trailing);
        m_bodyCount -= trailing;
        ReleaseBodiesBeyond(m_bodyCount);
    }

    // ================================================================
    // メモリ使用量
    // ================================================================

    /// 予約済み仮想アドレス空間のバイト数（区画1つ分。未確保なら0）
    size_t ReservedBytes() const {
        return (m_regionBase != nullptr) ? SlotValue::REGION_BYTES : 0;
    }

    /// 物理メモリを消費しているバイト数（本体・位置表・見出しの合計）
    size_t CommittedBytes() const {
        return m_committedBodyBytes + m_committedTableBytes + m_committedHeaderBytes;
    }

    /// 型ごとの共有領域の基底アドレス（テスト・デバッガ表示用。未予約なら0）
    static uintptr_t ArenaBase() { return s_arenaBase; }

    /// 区画の先頭アドレス（テスト用。未確保なら nullptr）
    const unsigned char* RegionBase() const { return m_regionBase; }

private:
    // ================================================================
    // 本体の添字と位置の変換
    // ================================================================

    /// 本体の添字から位置表の値を求める
    uint32_t BodyValueOf(uint32_t bodyIndex) const {
        return SlotValue::Make(m_tag, static_cast<uint32_t>(BODY_AREA_OFFSET) + bodyIndex * static_cast<uint32_t>(ELEMENT_BYTES));
    }

    /// 位置表の値から本体の添字を求める
    static uint32_t BodyIndexFromValue(uint32_t bodyValue) {
        return (SlotValue::Offset(bodyValue) - static_cast<uint32_t>(BODY_AREA_OFFSET)) / static_cast<uint32_t>(ELEMENT_BYTES);
    }

    // ================================================================
    // 領域管理
    // ================================================================

    /// 上限超過のエラーを出して終了する
    static void AbortCapacity() {
        std::fprintf(stderr,
            "[SlotStorage] 致命的エラー: 1プールの上限に達しました。\n"
            "  要素サイズ: %zu バイト（見出し %zu バイト、位置表 %zu バイト）\n"
            "  上限: %u 要素（プールあたり %zu バイト）\n"
            "  対処: 要素サイズか要素数を減らしてください。\n",
            ELEMENT_BYTES, HEADER_BYTES, ENTRY_BYTES,
            static_cast<unsigned>(MAX_RECORDS), SlotValue::REGION_BYTES);
        std::abort();
    }

    /// 指定数のスロット（位置表＋見出し）が入るように領域を用意する
    void EnsureSlotCapacity(uint32_t count) {
        if (count > MAX_RECORDS) AbortCapacity();
        AcquireRegionIfNeeded();
        CommitArea(TABLE_AREA_OFFSET, static_cast<size_t>(MAX_RECORDS) * ENTRY_BYTES, static_cast<size_t>(count) * ENTRY_BYTES, m_committedTableBytes);
        CommitArea(HEADER_AREA_OFFSET, static_cast<size_t>(MAX_RECORDS) * HEADER_BYTES, static_cast<size_t>(count) * HEADER_BYTES, m_committedHeaderBytes);
    }

    /// 指定数の本体が入るように領域を用意する
    void EnsureBodyCapacity(uint32_t count) {
        if (count > MAX_RECORDS) AbortCapacity();
        AcquireRegionIfNeeded();
        CommitArea(BODY_AREA_OFFSET, static_cast<size_t>(MAX_RECORDS) * ELEMENT_BYTES, static_cast<size_t>(count) * ELEMENT_BYTES, m_committedBodyBytes);
    }

    /// 指定数を超えるスロットの領域を返却する
    void ReleaseSlotsBeyond(uint32_t keepCount) {
        if (m_regionBase == nullptr) return;
        DecommitArea(TABLE_AREA_OFFSET, static_cast<size_t>(keepCount) * ENTRY_BYTES, m_committedTableBytes);
        DecommitArea(HEADER_AREA_OFFSET, static_cast<size_t>(keepCount) * HEADER_BYTES, m_committedHeaderBytes);
    }

    /// 指定数を超える本体の領域を返却する
    void ReleaseBodiesBeyond(uint32_t keepCount) {
        if (m_regionBase == nullptr) return;
        DecommitArea(BODY_AREA_OFFSET, static_cast<size_t>(keepCount) * ELEMENT_BYTES, m_committedBodyBytes);
    }

    /// バイト数をページ境界に切り上げる
    static size_t RoundUpToPage(size_t bytes) {
        const size_t pageSize = virtual_memory_allocator::get_page_size();
        return (bytes + pageSize - 1) & ~(pageSize - 1);
    }

    /// 区画内の1つの並びのコミット範囲を広げる（必要量を倍々で増やし、ページ境界と上限に合わせる）
    void CommitArea(size_t areaOffset, size_t areaLimitBytes, size_t requiredBytes, size_t& committedBytes) {
        if (requiredBytes <= committedBytes) return;

        size_t newCommitted = std::max(requiredBytes, committedBytes * 2);
        newCommitted = std::min(RoundUpToPage(newCommitted), RoundUpToPage(areaLimitBytes));
        newCommitted = std::max(newCommitted, RoundUpToPage(requiredBytes));

        void* result = virtual_memory_allocator::commit(m_regionBase + areaOffset, committedBytes, newCommitted);
        if (result == nullptr) {
            std::fprintf(stderr, "[SlotStorage] 致命的エラー: 物理メモリのコミットに失敗しました。\n");
            std::abort();
        }
        committedBytes = newCommitted;
    }

    /// 区画内の1つの並びのコミット範囲を縮める
    void DecommitArea(size_t areaOffset, size_t keepBytes, size_t& committedBytes) {
        const size_t newCommitted = RoundUpToPage(keepBytes);
        if (newCommitted >= committedBytes) return;
        virtual_memory_allocator::decommit(m_regionBase + areaOffset, committedBytes, newCommitted);
        committedBytes = newCommitted;
    }

    /// 区画が未確保なら、型ごとの共有領域からタグに対応する区画を取る
    void AcquireRegionIfNeeded() {
        if (m_regionBase != nullptr) return;
        if (s_arenaBase == 0) {
            void* origin = nullptr;
            size_t originBytes = 0;
            void* base = virtual_memory_allocator::reserve_aligned(ARENA_BYTES, ARENA_ALIGNMENT, &origin, &originBytes);
            if (base == nullptr) {
                std::fprintf(stderr,
                    "[SlotStorage] 致命的エラー: 共有領域（%zu バイト）の予約に失敗しました。\n"
                    "  対処: OBJECT_SLOT_USE_EXPERIMENTAL を外して従来の実装を使ってください。\n",
                    ARENA_BYTES);
                std::abort();
            }
            s_arenaBase = reinterpret_cast<uintptr_t>(base);
        }
        m_regionBase = reinterpret_cast<unsigned char*>(s_arenaBase + static_cast<uintptr_t>(m_tag) * SlotValue::REGION_BYTES);
    }

    // ================================================================
    // メンバ変数
    // ================================================================

    /** このストレージのタグ（区画番号） */
    uint8_t m_tag = SlotValue::INVALID_TAG;

    /** 現在のスロット数 */
    uint32_t m_size = 0;

    /** 見出しを初期化済みのスロット数 */
    uint32_t m_highWater = 0;

    /** 現在の本体数（空きを含む） */
    uint32_t m_bodyCount = 0;

    /** 空いている本体の添字（後入れ先出し） */
    std::vector<uint32_t> m_freeBodies;

    /** 区画の先頭アドレス（未確保なら nullptr） */
    unsigned char* m_regionBase = nullptr;

    /** 各並びのコミット済みバイト数 */
    size_t m_committedBodyBytes = 0;
    size_t m_committedTableBytes = 0;
    size_t m_committedHeaderBytes = 0;

    /** 型ごとの共有領域の基底アドレス（256MB境界。未予約なら0） */
    static inline uintptr_t s_arenaBase = 0;
};
