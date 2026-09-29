#pragma once

#include "SlotControlBase.h"
#include "EnableSlotFromThis.h"
#include "../thirdparty/rootVector/RootVector.h"

#include <type_traits>
#include <vector>
#include <algorithm>
#include <utility>

// 前方宣言
template<typename T>
class SlotPtr;

template<typename T>
class WeakSlotPtr;

/**
 * @brief オブジェクトプールの基底クラス（軽量版）
 *
 * SlotControlBaseを継承し、型依存のデータストレージを追加する。
 * root_vectorにより要素本体をメモリ上に連続配置して管理する。
 * ネイティブ環境では要素のアドレスが生涯変わらない。
 *
 * 【スロットと本体】
 * ポインタは「スロット番号」を持ち、本体の場所は位置一覧（スロット番号 → 本体のアドレス）で引く。
 * スロットは動かないが、本体は Compact() で先頭に詰め直せる。
 * 詰め直しても位置一覧を書き換えるだけなので、ポインタは有効なまま。
 *
 * 【要素の型に求める条件】
 * - ムーブ構築が可能であること
 * - デフォルト構築が可能であること（削除した本体の場所に空のオブジェクトを置くため）
 *
 * 【削除済み本体の扱い】
 * 削除時は要素のデストラクタを呼んだ後、デフォルト構築した空のオブジェクトを置く。
 * これによりストレージからは全ての本体が常に構築済みに見え、Clear() や
 * ShrinkToFit() でストレージが要素を破棄しても二重破棄にならない。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystemBase : public SlotControlBase {
    friend class SlotPtr<T>;
    friend class WeakSlotPtr<T>;

public:
    virtual ~ObjectSlotSystemBase() = default;

    /// ハンドルから要素を取得（無効なら nullptr）
    T* Get(SlotHandle handle) {
        if (!IsValidHandle(handle)) return nullptr;
        return Element(handle.index);
    }

    /// ハンドルから要素を取得（const版）
    const T* Get(SlotHandle handle) const {
        if (!IsValidHandle(handle)) return nullptr;
        return Element(handle.index);
    }

    /// スロット番号から要素本体を取得（範囲検証なし）
    T* Element(uint32_t slotIndex) { return static_cast<T*>(m_locations[slotIndex]); }

    /// スロット番号から要素本体を取得（const版）
    const T* Element(uint32_t slotIndex) const { return static_cast<const T*>(m_locations[slotIndex]); }

    /// 要素本体のアドレス一覧の先頭（スロット番号で引く）
    T* const* Locations() const { return reinterpret_cast<T* const*>(m_locations.data()); }

    /**
     * @brief メモリ使用量の内訳
     */
    struct MemoryUsage {
        size_t reservedBytes = 0;       ///< 予約済み仮想アドレス空間（物理メモリは消費しない）
        size_t elementBytes = 0;        ///< 要素本体のコミット済みバイト数（ページ単位に切り上げ）
        size_t metadataBytes = 0;       ///< 世代番号・生死フラグ・参照カウント・位置一覧・フリーリスト・生存一覧
        size_t subscriptionBytes = 0;   ///< 購読リスト（通知機能付きプールのみ）
        size_t refEntryBytes = 0;       ///< 未使用（互換のため残している）

        /// 物理メモリを消費している合計
        size_t TotalPhysicalBytes() const { return elementBytes + metadataBytes + subscriptionBytes + refEntryBytes; }
    };

    /// メモリ使用量の内訳を取得
    MemoryUsage GetMemoryUsage() const {
        MemoryUsage usage;
        usage.reservedBytes = m_data.reserved_bytes();
        usage.elementBytes  = m_data.committed_bytes();
        usage.metadataBytes =
              m_generations.capacity() * sizeof(uint32_t)
            + m_alive.capacity() / 8                         // 1スロット1ビット
            + m_refCounts.capacity() * sizeof(uint32_t)
            + m_locations.capacity() * sizeof(void*)
            + m_bodyIndexOf.capacity() * sizeof(uint32_t)
            + m_freeBodies.capacity() * sizeof(uint32_t)
            + m_freeList.size() * sizeof(uint32_t)
            + m_activeIndexList.capacity() * sizeof(uint32_t)
            + m_activeListPositions.capacity() * sizeof(uint32_t)
            + m_pendingActiveListRemovals.capacity() * sizeof(uint32_t);
        return usage;
    }

    /**
     * @brief 生存している全要素に関数を適用する
     *
     * 生存一覧の順に走査するため、歯抜けのスロットを読み飛ばすコストがかからない。
     * 走査中に要素が削除されても安全で、一覧の書き換えは走査完了まで遅延される。
     * 走査中に生成された要素は今回の走査対象に含まれない（再利用されたスロットは除く）。
     *
     * @tparam Func void(SlotHandle, T&) の形で呼べる関数
     * @param func 各要素に対して呼ぶ関数
     */
    template<typename Func>
    void ForEach(Func&& func) {
        ++m_activeListLockDepth;

        const size_t activeCount = m_activeIndexList.size();
        for (size_t position = 0; position < activeCount; ++position) {
            const uint32_t slotIndex = m_activeIndexList[position];
            if (!m_alive[slotIndex]) continue;

            SlotHandle handle{ slotIndex, m_generations[slotIndex] };
            func(handle, *Element(slotIndex));
        }

        --m_activeListLockDepth;
        if (m_activeListLockDepth == 0) {
            ProcessPendingActiveListRemovals();
        }
    }

    /// 生存している全要素に関数を適用する（const版）
    template<typename Func>
    void ForEach(Func&& func) const {
        const size_t activeCount = m_activeIndexList.size();
        for (size_t position = 0; position < activeCount; ++position) {
            const uint32_t slotIndex = m_activeIndexList[position];
            if (!m_alive[slotIndex]) continue;

            SlotHandle handle{ slotIndex, m_generations[slotIndex] };
            func(handle, *Element(slotIndex));
        }
    }

    /**
     * @brief プール内の全要素を削除
     */
    void Clear() {
        m_data.clear();
        m_generations.clear();
        m_alive.clear();
        m_refCounts.clear();
        m_locations.clear();
        m_bodyIndexOf.clear();
        m_freeBodies.clear();
        m_freeList = std::queue<uint32_t>();
        m_count = 0;
        ClearActiveIndexList();
        UpdateLocationData();
    }

    /**
     * @brief 指定した数の要素分のメモリを事前確保
     */
    void Reserve(size_t capacity) {
        m_data.reserve(capacity);
        m_generations.reserve(capacity);
        m_alive.reserve(capacity);
        m_refCounts.reserve(capacity);
        m_locations.reserve(capacity);
        m_bodyIndexOf.reserve(capacity);
        m_activeIndexList.reserve(capacity);
        m_activeListPositions.reserve(capacity);
        UpdateLocationData();
        RefreshLocationsIfMoved();
    }

    /**
     * @brief 末尾の空きスロットと空き本体を切り詰め、物理メモリを返却する
     *
     * 要素は動かさない。途中の空きは残るので、詰めたい場合は Compact() を使う。
     */
    void ShrinkToFit() {
        // スロットの末尾を切り詰める
        size_t newSize = m_alive.size();
        while (newSize > 0 && !m_alive[newSize - 1]) {
            --newSize;
        }
        if (newSize < m_alive.size()) {
            m_generations.resize(newSize);
            m_generations.shrink_to_fit();
            m_alive.resize(newSize);
            m_alive.shrink_to_fit();
            m_refCounts.resize(newSize);
            m_refCounts.shrink_to_fit();
            m_locations.resize(newSize);
            m_locations.shrink_to_fit();
            m_bodyIndexOf.resize(newSize);
            m_bodyIndexOf.shrink_to_fit();
            m_activeListPositions.resize(newSize);
            m_activeListPositions.shrink_to_fit();

            std::queue<uint32_t> newFreeList;
            while (!m_freeList.empty()) {
                const uint32_t index = m_freeList.front();
                m_freeList.pop();
                if (index < newSize) {
                    newFreeList.push(index);
                }
            }
            m_freeList = std::move(newFreeList);
            UpdateLocationData();
        }

        TrimFreeBodies();
    }

    /**
     * @brief 生存している要素の本体を先頭から隙間なく並べ直す
     *
     * 削除の繰り返しで本体の並びに空きが混ざると、走査が飛び飛びになり
     * 物理メモリも返せない。シーン切り替えなど処理が止まってよい時に呼ぶと、
     * 本体を詰め直して末尾の物理メモリを返却する。
     *
     * ポインタ・弱参照・SlotRef・購読は全てスロット番号を基準にしているので、
     * 呼び出し後もそのまま使える。要素はムーブ構築で移されるので、
     * 要素のアドレスを生ポインタで別に保持している場合だけは無効になる。
     *
     * 詰め直した後は ForEach の走査順を本体の並び順に揃える。
     * ForEach の中からは呼べない（何もせず戻る）。
     */
    void Compact() {
        if (m_activeListLockDepth > 0) return;

        // 生存スロットを現在の本体位置の昇順に並べる
        std::vector<uint32_t> liveSlots;
        liveSlots.reserve(m_activeIndexList.size());
        for (uint32_t slotIndex : m_activeIndexList) {
            if (m_alive[slotIndex]) liveSlots.push_back(slotIndex);
        }
        std::sort(liveSlots.begin(), liveSlots.end(),
            [this](uint32_t a, uint32_t b) { return m_bodyIndexOf[a] < m_bodyIndexOf[b]; });

        // 昇順に処理するので、移動先は常に移動元より手前で、空のオブジェクトが置かれている
        for (uint32_t destination = 0; destination < liveSlots.size(); ++destination) {
            const uint32_t slotIndex = liveSlots[destination];
            const uint32_t source = m_bodyIndexOf[slotIndex];
            if (source != destination) {
                T& to = m_data.get(destination);
                T& from = m_data.get(source);
                to.~T();
                new (&to) T(std::move(from));
                from.~T();
                new (&from) T();
                m_bodyIndexOf[slotIndex] = destination;
            }
        }

        // 末尾に残った空のオブジェクトを捨てて物理メモリを返す
        m_data.resize(liveSlots.size());
        m_data.shrink_to_fit();
        m_freeBodies.clear();
        RebuildLocations();

        // 走査順を本体の並び順に揃える
        m_activeIndexList = std::move(liveSlots);
        std::fill(m_activeListPositions.begin(), m_activeListPositions.end(), INVALID_POSITION);
        for (size_t position = 0; position < m_activeIndexList.size(); ++position) {
            m_activeListPositions[m_activeIndexList[position]] = static_cast<uint32_t>(position);
        }
        m_isActiveIndexListSorted = std::is_sorted(m_activeIndexList.begin(), m_activeIndexList.end());
    }

protected:
    /**
     * @brief 新しい要素用のスロットを確保
     *
     * スロットはフリーリストがあれば再利用し、なければ末尾に追加する。
     * 本体は空きがあれば空のオブジェクトを破棄して構築し直し、なければ末尾に追加する。
     * 確保したスロットは生存一覧に加える。
     *
     * @param obj 格納する要素（ムーブされる）
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        SlotHandle handle;

        if (!m_freeList.empty()) {
            handle.index = m_freeList.front();
            m_freeList.pop();
            handle.generation = m_generations[handle.index];
            m_alive[handle.index] = true;
            m_refCounts[handle.index] = 0;
        }
        else {
            handle.index = static_cast<uint32_t>(m_alive.size());
            handle.generation = 0;
            m_generations.push_back(0);
            m_alive.push_back(true);
            m_refCounts.push_back(0);
            m_locations.push_back(nullptr);
            m_bodyIndexOf.push_back(0);
            UpdateLocationData();
        }

        // 本体を割り当てる
        uint32_t bodyIndex = 0;
        if (!m_freeBodies.empty()) {
            bodyIndex = m_freeBodies.back();
            m_freeBodies.pop_back();
            T& body = m_data.get(bodyIndex);
            body.~T();
            new (&body) T(std::move(obj));
        }
        else {
            bodyIndex = static_cast<uint32_t>(m_data.size());
            m_data.push_back(std::move(obj));
            RefreshLocationsIfMoved();
        }
        m_bodyIndexOf[handle.index] = bodyIndex;
        m_locations[handle.index] = &m_data.get(bodyIndex);

        AddToActiveIndexList(handle.index);

        if constexpr (std::is_base_of_v<EnableSlotFromThis<T>, T>) {
            Element(handle.index)->InitSlotFromThis(handle, this);
        }

        ++m_count;
        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * 生存一覧から外し、世代番号を進めて古いハンドル・弱参照を無効化し、
     * 要素のデストラクタを呼ぶ。本体の場所には空のオブジェクトを置いて空きに戻す。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        RemoveFromActiveIndexList(handle.index);
        m_alive[handle.index] = false;
        ++m_generations[handle.index];
        m_refCounts[handle.index] = 0;

        T& body = *Element(handle.index);
        body.~T();
        new (&body) T();
        m_freeBodies.push_back(m_bodyIndexOf[handle.index]);
        m_locations[handle.index] = nullptr;

        m_freeList.push(handle.index);
        --m_count;
    }

    /** 要素本体の連続配置ストレージ（ネイティブ環境ではアドレス不変） */
    root_vector<T> m_data;

private:
    /// 基底クラスに位置一覧の先頭を知らせる
    void UpdateLocationData() {
        m_locationData = m_locations.data();
    }

    /// 全スロットの位置一覧を本体の添字から作り直す
    void RebuildLocations() {
        for (size_t slotIndex = 0; slotIndex < m_alive.size(); ++slotIndex) {
            m_locations[slotIndex] = m_alive[slotIndex] ? &m_data.get(m_bodyIndexOf[slotIndex]) : nullptr;
        }
        m_lastDataPointer = m_data.data();
        UpdateLocationData();
    }

    /// ストレージが引っ越していたら（仮想メモリのない環境）位置一覧を作り直す
    void RefreshLocationsIfMoved() {
        if (m_data.data() != m_lastDataPointer) {
            RebuildLocations();
        }
    }

    /// 末尾に連続する空き本体を切り落とし、物理メモリを返す
    void TrimFreeBodies() {
        if (m_freeBodies.empty()) return;
        std::sort(m_freeBodies.begin(), m_freeBodies.end());
        uint32_t trailing = 0;
        const uint32_t bodyCount = static_cast<uint32_t>(m_data.size());
        while (trailing < m_freeBodies.size()
            && m_freeBodies[m_freeBodies.size() - 1 - trailing] == bodyCount - 1 - trailing) {
            ++trailing;
        }
        if (trailing == 0) return;
        m_freeBodies.resize(m_freeBodies.size() - trailing);
        m_data.resize(bodyCount - trailing);
        m_data.shrink_to_fit();
        RefreshLocationsIfMoved();
    }

    /** スロット番号 → 本体のアドレス（削除済みは nullptr） */
    std::vector<void*> m_locations;

    /** スロット番号 → 本体の添字 */
    std::vector<uint32_t> m_bodyIndexOf;

    /** 空いている本体の添字（後入れ先出し） */
    std::vector<uint32_t> m_freeBodies;

    /** 前回位置一覧を作った時のストレージ先頭（引っ越し検出用） */
    T* m_lastDataPointer = nullptr;
};
