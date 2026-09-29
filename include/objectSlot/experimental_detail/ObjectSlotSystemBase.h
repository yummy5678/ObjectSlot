#pragma once

#include "SlotControlBase.h"
#include "SlotStorage.h"
#include "EnableSlotFromThis.h"

#include <type_traits>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <utility>
#include <vector>
#include <algorithm>

template<typename T>
class SlotPtr;

template<typename T>
class WeakSlotPtr;

/**
 * @class ObjectSlotSystemBase
 * @brief 型ごとのオブジェクトプールの基底クラス
 *
 * 【責任】
 * - SlotStorageを使って要素をアドレス不変の領域に置き、生成・削除・走査を提供する
 * - 型ごとのタグ台帳（タグ → プール）を管理し、SlotPtrがプール本体を持たずに済むようにする
 * - 各要素の見出し（参照カウント・世代番号・圧縮値・全体番号）を生成時に初期化する
 *
 * 【タグとは】
 * 同じ型のプールを区別する4ビットの番号。SlotPtrは「タグ＋スロット番号」の
 * 4バイトだけを持ち、参照カウントが0になった時だけタグからプールを引く。
 * タグは型ごとに最大15個。
 *
 * 【スロットと本体】
 * スロット（見出し・位置表）は動かないが、要素本体は Compact() で詰め直せる。
 * ポインタはスロット番号を持つので、詰め直しても有効なまま。
 *
 * 【要素の型に求める条件】
 * - ムーブ構築が可能であること（Createで受け取った値を領域に構築するため）
 * - デフォルトコンストラクタは不要（削除済みスロットに空のオブジェクトは置かない）
 *
 * 【使用用途】
 * - ObjectSlotSystem / SignalSlotSystem が継承する。利用者はそれらのシングルトンを使う
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystemBase : public SlotControlBase {
    friend class SlotPtr<T>;
    friend class WeakSlotPtr<T>;

public:
    using Storage = SlotStorage<T>;

    /** タグが取りうる値の個数 */
    static constexpr uint32_t TAG_COUNT = SlotValue::TAG_COUNT;

    /** 「タグ未設定」を表す値 */
    static constexpr uint8_t INVALID_TAG = SlotValue::INVALID_TAG;

    /// タグを割り当て、型ごとのタグ台帳に登録する
    ObjectSlotSystemBase()
        : m_tag(AllocateTag())
    {
        s_poolByTag[m_tag] = this;
        m_storage.SetTag(m_tag);
    }

    /// 生きている要素を全て破棄し、タグ台帳から外す
    virtual ~ObjectSlotSystemBase() {
        DestroyAllLiveElements();
        s_poolByTag[m_tag] = nullptr;
    }

    // ================================================================
    // タグ台帳
    // ================================================================

    /// タグからプールを取得（破棄済みなら nullptr）
    static ObjectSlotSystemBase* PoolFromTag(uint8_t tag) {
        return s_poolByTag[tag];
    }

    /// このプールのタグを取得
    uint8_t GetTag() const { return m_tag; }

    // ================================================================
    // メモリ使用量
    // ================================================================

    /**
     * @brief メモリ使用量の内訳
     *
     * 要素本体には各要素の見出し（16バイト）が含まれる。
     */
    struct MemoryUsage {
        size_t reservedBytes = 0;       ///< 予約済み仮想アドレス空間（物理メモリは消費しない）
        size_t elementBytes = 0;        ///< 要素本体＋見出しのコミット済みバイト数
        size_t metadataBytes = 0;       ///< 生死フラグ・フリーリスト・生存一覧
        size_t subscriptionBytes = 0;   ///< 購読リスト（通知機能付きプールのみ）

        /// 物理メモリを消費している合計
        size_t TotalPhysicalBytes() const { return elementBytes + metadataBytes + subscriptionBytes; }
    };

    /// メモリ使用量の内訳を取得
    MemoryUsage GetMemoryUsage() const {
        MemoryUsage usage;
        usage.reservedBytes = m_storage.ReservedBytes();
        usage.elementBytes  = m_storage.CommittedBytes();
        usage.metadataBytes =
              m_alive.capacity() / 8                         // 1スロット1ビット
            + m_freeList.size() * sizeof(uint32_t)
            + m_activeIndexList.capacity() * sizeof(uint32_t)
            + m_activeListPositions.capacity() * sizeof(uint32_t)
            + m_pendingActiveListRemovals.capacity() * sizeof(uint32_t);
        return usage;
    }

    // ================================================================
    // 要素アクセス
    // ================================================================

    /// ハンドルから要素を取得（無効なら nullptr）
    T* Get(SlotHandle handle) {
        if (!IsValidHandle(handle)) return nullptr;
        return m_storage.Element(handle.index);
    }

    /// ハンドルから要素を取得（const版）
    const T* Get(SlotHandle handle) const {
        if (!IsValidHandle(handle)) return nullptr;
        return m_storage.Element(handle.index);
    }

    /// 添字から見出しを取得
    SlotHeader* HeaderAt(uint32_t index) override {
        return m_storage.Header(index);
    }

    /// 添字から見出しを取得（const版）
    const SlotHeader* HeaderAt(uint32_t index) const override {
        return m_storage.Header(index);
    }

    /// 圧縮値からこのプール内の添字を求める
    uint32_t IndexFromValue(uint32_t value) const override {
        return Storage::IndexFromValue(value);
    }

    /**
     * @brief 参照カウントが0になった要素を圧縮値で指定して削除する
     *
     * 型が分かっている経路（SlotPtr / SignalSlotPtr）用の入口。
     * 添字の計算を仮想関数を通さずに行い、削除処理だけを仮想関数で呼ぶ。
     *
     * @param value 参照カウントが0になった要素の圧縮値
     */
    void OnRefCountReachedZeroByValue(uint32_t value) {
        const uint32_t index = Storage::IndexFromValue(value);
        RemoveInternal(SlotHandle{ index, m_storage.Header(index)->generation });
    }

    /// 添字から圧縮値を求める
    uint32_t ValueOf(uint32_t index) const {
        return m_storage.ValueOf(index);
    }

    /// 要素を保持するストレージを取得（テスト・デバッグ用）
    const Storage& GetStorage() const { return m_storage; }

    // ================================================================
    // 走査
    // ================================================================

    /**
     * @brief 生存している全要素に関数を適用する
     *
     * 生存一覧の順に走査する。走査中に要素が削除されても安全で、
     * 一覧の書き換えは走査完了まで遅延される。
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

            // 圧縮値を1回だけ求め、見出しと本体の両方をそこから引く
            T* element = nullptr;
            SlotHeader* header = nullptr;
            Storage::Locate(m_storage.ValueOf(slotIndex), element, header);
            SlotHandle handle{ slotIndex, header->generation };
            func(handle, *element);
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

            // 圧縮値を1回だけ求め、見出しと本体の両方をそこから引く
            T* element = nullptr;
            SlotHeader* header = nullptr;
            Storage::Locate(m_storage.ValueOf(slotIndex), element, header);
            SlotHandle handle{ slotIndex, header->generation };
            func(handle, *element);
        }
    }

    // ================================================================
    // 一括操作
    // ================================================================

    /**
     * @brief 全要素を破棄してプールを空にする
     *
     * 領域は返却せず、各スロットの見出しは世代番号を進めて残す。
     * このため、残っている古いポインタや弱参照は安全に「無効」と判定される。
     * 領域を返却したい場合は続けて ShrinkToFit() を呼ぶ。
     */
    void Clear() {
        DestroyAllLiveElements();
        m_storage.ResetSize();
        m_alive.clear();
        m_freeList = std::queue<uint32_t>();
        m_count = 0;
        ClearActiveIndexList();
    }

    /// 指定数の要素が入るよう領域を先に確保する
    void Reserve(size_t capacity) {
        m_storage.Reserve(static_cast<uint32_t>(capacity));
        m_alive.reserve(capacity);
        m_activeIndexList.reserve(capacity);
        m_activeListPositions.reserve(capacity);
    }

    /**
     * @brief 末尾の空きスロットを切り詰め、領域を返却する
     *
     * 切り詰められる範囲を指す古いポインタ・弱参照は、呼び出し後に触ってはいけない
     * （その範囲の見出しが読めなくなるため）。
     */
    void ShrinkToFit() {
        uint32_t newSize = m_storage.Size();
        while (newSize > 0 && !m_alive[newSize - 1]) {
            --newSize;
        }
        if (newSize == m_storage.Size()) {
            m_storage.TrimFreeBodies();
            return;
        }

        m_storage.Truncate(newSize);
        m_alive.resize(newSize);
        m_alive.shrink_to_fit();
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

        std::vector<uint32_t> liveSlots;
        liveSlots.reserve(m_activeIndexList.size());
        for (uint32_t slotIndex : m_activeIndexList) {
            if (m_alive[slotIndex]) liveSlots.push_back(slotIndex);
        }

        m_storage.Compact(liveSlots);

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
     * @brief スロットを確保して要素を構築する
     *
     * フリーリストに空きがあればそれを再利用し、なければ末尾に追加する。
     * 要素をムーブ構築した後、見出しの圧縮値・全体番号を設定し、
     * 参照カウントは0のまま返す（呼び出し側が最初の参照を加える）。
     * EnableSlotFromThis を継承した型なら、自分の圧縮値を渡す。
     *
     * @param obj 構築元の要素（ムーブされる）
     * @return 確保したスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        uint32_t index = 0;
        if (!m_freeList.empty()) {
            index = m_freeList.front();
            m_freeList.pop();
            m_alive[index] = true;
        }
        else {
            index = m_storage.PushBackSlot();
            m_alive.push_back(true);
        }

        T* element = m_storage.AttachBody(index);
        new (element) T(std::move(obj));

        SlotHeader& header = *m_storage.Header(index);
        header.refCount = 0;
        header.selfValue = m_storage.ValueOf(index);
        header.poolId = GetPoolId();

        AddToActiveIndexList(index);

        if constexpr (std::is_base_of_v<EnableSlotFromThis<T>, T>) {
            element->InitSlotFromThis(header.selfValue);
        }

        ++m_count;
        return SlotHandle{ index, header.generation };
    }

    /**
     * @brief 要素を削除する
     *
     * 生存一覧から外し、世代番号を進めて古いハンドル・弱参照を無効化し、
     * 要素のデストラクタを呼んでスロットをフリーリストへ戻す。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        RemoveFromActiveIndexList(handle.index);
        m_alive[handle.index] = false;

        SlotHeader* header = m_storage.Header(handle.index);
        ++header->generation;
        header->refCount = 0;
        m_storage.Element(handle.index)->~T();
        m_storage.DetachBody(handle.index);

        m_freeList.push(handle.index);
        --m_count;
    }

    /** 要素と見出しを保持するストレージ */
    Storage m_storage;

private:
    /// 生きている全要素のデストラクタを呼び、見出しを「削除済み」にする
    void DestroyAllLiveElements() {
        const uint32_t size = m_storage.Size();
        for (uint32_t index = 0; index < size; ++index) {
            if (!m_alive[index]) continue;
            SlotHeader* header = m_storage.Header(index);
            ++header->generation;
            header->refCount = 0;
            m_storage.Element(index)->~T();
            m_storage.DetachBody(index);
            m_alive[index] = false;
        }
    }

    /// 型ごとの通し番号からタグを割り当てる（上限を超えたら強制終了）
    static uint8_t AllocateTag() {
        if (s_nextTag >= INVALID_TAG) {
            std::fprintf(stderr,
                "[ObjectSlotSystemBase] 致命的エラー: 型ごとのプール数の上限に達しました。\n"
                "  上限: %u プール\n"
                "  対処: 同じ型のプールの生成回数を減らしてください。\n",
                static_cast<unsigned>(INVALID_TAG));
            std::abort();
        }
        return s_nextTag++;
    }

    /** このプールのタグ */
    uint8_t m_tag;

    /** 次に割り当てるタグ（型ごと） */
    static inline uint8_t s_nextTag = 0;

    /** タグ → プール（型ごと） */
    static inline ObjectSlotSystemBase* s_poolByTag[TAG_COUNT] = {};
};
