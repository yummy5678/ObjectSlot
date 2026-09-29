#pragma once

#include "SlotHandle.h"
#include "SlotHeader.h"

#include <vector>
#include <queue>
#include <cassert>
#include <functional>
#include <algorithm>

/**
 * @class SlotControlBase
 * @brief 全てのプールに共通する、要素の型に依存しない管理機能をまとめた基底クラス
 *
 * 【責任】
 * - スロットの生死フラグ、フリーリスト、生存数、生成上限の管理
 * - ForEach用の生存スロット一覧（走査中の削除を遅延する仕組みを含む）
 * - 全プールを通し番号で引ける全体台帳の提供
 *   （型を知らないSlotRefやSubscriptionRefが、見出しのpoolIdからプールへ辿るため）
 * - 型消去された経路での参照カウント操作・購読操作の入口（仮想関数）
 *
 * 【設計上の位置づけ】
 * 参照カウントと世代番号はプールの配列ではなく各要素の見出し（SlotHeader）にある。
 * 型が分かる経路（SlotPtr等）は見出しに直接触れるので、このクラスを経由しない。
 * このクラスの仮想関数を通るのは、参照カウントが0になった時の削除と、
 * 型を知らない経路（SlotRef、SubscriptionRef）だけである。
 *
 * 【使用用途】
 * - ObjectSlotSystemBase<T> が継承する。利用者が直接使うことはない
 */
class SlotControlBase {
public:
    virtual ~SlotControlBase() {
        Registry()[m_poolId] = nullptr;
    }

    /** 生存一覧内の位置が「未登録」であることを表す値 */
    static constexpr uint32_t INVALID_POSITION = UINT32_MAX;

    /** 「プール未登録」を表す全体番号 */
    static constexpr uint32_t INVALID_POOL_ID = UINT32_MAX;

    /** 「購読していない」を表す購読ID */
    static constexpr uint32_t INVALID_SUBSCRIPTION_ID = UINT32_MAX;

    // ================================================================
    // 全体台帳
    // ================================================================

    /// 全体番号からプールを取得（破棄済みなら nullptr）
    static SlotControlBase* PoolFromId(uint32_t poolId) {
        std::vector<SlotControlBase*>& registry = Registry();
        if (poolId >= registry.size()) return nullptr;
        return registry[poolId];
    }

    /// このプールの全体番号を取得
    uint32_t GetPoolId() const { return m_poolId; }

    // ================================================================
    // 生存一覧
    // ================================================================

    /// 生存スロットの添字一覧を取得（ForEachの走査順）
    const std::vector<uint32_t>& GetActiveIndexList() const { return m_activeIndexList; }

    /// 生存一覧が昇順に並んでいるか
    bool IsActiveIndexListSorted() const { return m_isActiveIndexListSorted; }

    /**
     * @brief 生存一覧を添字の昇順に並べ替える
     *
     * 削除の繰り返しで走査順がメモリ順から崩れると、ForEachのキャッシュ効率が落ちる。
     * シーン切り替えなど処理が止まってよい瞬間に呼ぶと、走査がメモリ順に戻る。
     * 並べ替え後は位置の逆引き表も作り直す。
     */
    void SortActiveIndexList() {
        if (m_isActiveIndexListSorted) return;

        std::sort(m_activeIndexList.begin(), m_activeIndexList.end());

        const size_t activeCount = m_activeIndexList.size();
        for (size_t position = 0; position < activeCount; ++position) {
            m_activeListPositions[m_activeIndexList[position]] = static_cast<uint32_t>(position);
        }

        m_isActiveIndexListSorted = true;
    }

    // ================================================================
    // 状態の問い合わせ
    // ================================================================

    /// ハンドルが生きている要素を指しているか（生死と世代番号を確認）
    bool IsValidHandle(SlotHandle handle) const {
        if (handle.index >= m_alive.size()) return false;
        if (!m_alive[handle.index]) return false;
        return HeaderAt(handle.index)->generation == handle.generation;
    }

    /// ハンドルが指す要素の参照カウントを取得（無効なら0）
    uint32_t GetRefCount(SlotHandle handle) const {
        if (!IsValidHandle(handle)) return 0;
        return HeaderAt(handle.index)->refCount;
    }

    /// 添字で参照カウントを取得（範囲検証なし）
    uint32_t GetRefCountByIndex(uint32_t index) const {
        return HeaderAt(index)->refCount;
    }

    /// 添字から現在の世代番号を含むハンドルを作る
    SlotHandle HandleFromIndex(uint32_t index) const {
        return { index, HeaderAt(index)->generation };
    }

    /// 生存している要素数を取得
    size_t Count() const { return m_count; }

    /// 確保済みスロット数（生死を問わない）を取得
    size_t Capacity() const { return m_alive.size(); }

    /// 生成できる要素数の上限を設定（0で無制限）
    void SetMaxCapacity(size_t maxCapacity) { m_maxCapacity = maxCapacity; }

    /// 生成上限を取得
    size_t GetMaxCapacity() const { return m_maxCapacity; }

    /// 新しい要素を生成できるか
    bool CanCreate() const {
        if (m_maxCapacity == 0) return true;
        return m_count < m_maxCapacity;
    }

    // ================================================================
    // 型消去された経路からの操作
    // ================================================================

    /// 添字で参照カウントを増やす（生きているスロットのみ）
    void AddRefByIndex(uint32_t index) {
        if (index < m_alive.size() && m_alive[index]) {
            ++HeaderAt(index)->refCount;
        }
    }

    /// 添字で参照カウントを減らし、0になれば削除する
    void ReleaseRefByIndex(uint32_t index) {
        if (index < m_alive.size() && m_alive[index]) {
            SlotHeader* header = HeaderAt(index);
            assert(header->refCount > 0);
            --header->refCount;
            if (header->refCount == 0) {
                RemoveInternal(SlotHandle{ index, header->generation });
            }
        }
    }

    /**
     * @brief 見出しの参照カウントが0になった時に呼ぶ削除の入口
     *
     * SlotPtr / SlotRef が参照カウントを直接減らして0になった場合に呼ばれる。
     * 見出しが持つ圧縮値から添字を求め、削除処理（RemoveInternal）へ渡す。
     * 通知機能付きプールでは、通知ループ中なら削除が遅延される。
     *
     * @param header 参照カウントが0になった要素の見出し
     */
    void OnRefCountReachedZero(SlotHeader* header) {
        const uint32_t index = IndexFromValue(header->selfValue);
        RemoveInternal(SlotHandle{ index, header->generation });
    }

    /// 圧縮値からこのプール内の添字を求める
    virtual uint32_t IndexFromValue(uint32_t value) const = 0;

    /// 添字から見出しを取得（範囲検証なし）
    virtual SlotHeader* HeaderAt(uint32_t index) = 0;

    /// 添字から見出しを取得（const版）
    virtual const SlotHeader* HeaderAt(uint32_t index) const = 0;

    /// 解放通知を購読する（通知機能のないプールでは INVALID_SUBSCRIPTION_ID を返す）
    virtual uint32_t SubscribeByIndex(uint32_t slotIndex, std::function<void()> callback) {
        (void)slotIndex;
        (void)callback;
        return INVALID_SUBSCRIPTION_ID;
    }

    /// 購読を解除する（通知機能のないプールでは何もしない）
    virtual void RemoveSubscriptionByIndex(uint32_t slotIndex, uint32_t subscriptionId) {
        (void)slotIndex;
        (void)subscriptionId;
    }

    /// 購読のコールバックを差し替える（通知機能のないプールでは何もしない）
    virtual void UpdateSubscriptionCallbackByIndex(uint32_t slotIndex, uint32_t subscriptionId, std::function<void()> callback) {
        (void)slotIndex;
        (void)subscriptionId;
        (void)callback;
    }

protected:
    /// 全体台帳に自身を登録して番号を受け取る
    SlotControlBase()
        : m_poolId(RegisterPool(this))
    {
    }

    // ================================================================
    // 生存一覧の操作
    // ================================================================

    /**
     * @brief スロットを生存一覧に加える
     *
     * 削除が遅延中のスロットを再確保した場合は既に登録されているので二重登録しない。
     * フリーリストから取った小さい添字を末尾に足すと昇順が崩れるため、その場合は
     * 並び順フラグを落とす。
     *
     * @param slotIndex 追加するスロットの添字
     */
    void AddToActiveIndexList(uint32_t slotIndex) {
        if (slotIndex >= m_activeListPositions.size()) {
            m_activeListPositions.resize(slotIndex + 1, INVALID_POSITION);
        }

        if (m_activeListPositions[slotIndex] != INVALID_POSITION) return;

        if (!m_activeIndexList.empty() && m_activeIndexList.back() > slotIndex) {
            m_isActiveIndexListSorted = false;
        }

        m_activeListPositions[slotIndex] = static_cast<uint32_t>(m_activeIndexList.size());
        m_activeIndexList.push_back(slotIndex);
    }

    /**
     * @brief スロットを生存一覧から取り除く
     *
     * 走査中（ロック中）なら一覧を書き換えず、走査完了後に取り除く。
     * それ以外は末尾の要素を空いた位置へ移して詰める（順序は崩れる）。
     *
     * @param slotIndex 取り除くスロットの添字
     */
    void RemoveFromActiveIndexList(uint32_t slotIndex) {
        if (m_activeListLockDepth > 0) {
            m_pendingActiveListRemovals.push_back(slotIndex);
            return;
        }

        if (slotIndex >= m_activeListPositions.size()) return;

        const uint32_t position = m_activeListPositions[slotIndex];
        if (position == INVALID_POSITION) return;

        const uint32_t lastPosition = static_cast<uint32_t>(m_activeIndexList.size() - 1);

        if (position != lastPosition) {
            const uint32_t movedSlotIndex = m_activeIndexList[lastPosition];
            m_activeIndexList[position] = movedSlotIndex;
            m_activeListPositions[movedSlotIndex] = position;
            m_isActiveIndexListSorted = false;
        }

        m_activeIndexList.pop_back();
        m_activeListPositions[slotIndex] = INVALID_POSITION;
    }

    /// 走査中に遅延された一覧からの除去をまとめて実行する
    void ProcessPendingActiveListRemovals() {
        if (m_pendingActiveListRemovals.empty()) return;

        std::vector<uint32_t> pending = std::move(m_pendingActiveListRemovals);
        m_pendingActiveListRemovals.clear();

        for (uint32_t slotIndex : pending) {
            // 遅延中に再確保されたスロットは取り除かない
            if (slotIndex < m_alive.size() && !m_alive[slotIndex]) {
                RemoveFromActiveIndexList(slotIndex);
            }
        }
    }

    /// 生存一覧を空にする
    void ClearActiveIndexList() {
        m_activeIndexList.clear();
        m_activeListPositions.clear();
        m_pendingActiveListRemovals.clear();
        m_activeListLockDepth = 0;
        m_isActiveIndexListSorted = true;
    }

    /// ハンドルで参照カウントを増やす（有効なハンドルのみ）
    void AddRef(SlotHandle handle) {
        if (IsValidHandle(handle)) {
            ++HeaderAt(handle.index)->refCount;
        }
    }

    /// 要素を削除する（通知や遅延の扱いは派生クラスが決める）
    virtual void RemoveInternal(SlotHandle handle) = 0;

    // ================================================================
    // メンバ変数
    // ================================================================

    /** スロットごとの生死フラグ */
    std::vector<bool> m_alive;

    /** 空きスロットの添字（先入れ先出しで再利用する） */
    std::queue<uint32_t> m_freeList;

    /** 生存している要素数 */
    size_t m_count = 0;

    /** 生成上限（0で無制限） */
    size_t m_maxCapacity = 0;

    /** 生存スロットの添字一覧（ForEachはこの順に走査する） */
    std::vector<uint32_t> m_activeIndexList;

    /** スロット添字 → 生存一覧内の位置（未登録なら INVALID_POSITION） */
    std::vector<uint32_t> m_activeListPositions;

    /** 走査中に発生した、生存一覧から取り除くべき添字 */
    std::vector<uint32_t> m_pendingActiveListRemovals;

    /** ForEachの入れ子深さ（0以外の間は生存一覧を書き換えない） */
    uint32_t m_activeListLockDepth = 0;

    /** 生存一覧が添字の昇順に並んでいるか */
    bool m_isActiveIndexListSorted = true;

private:
    /// 全体台帳（初回利用時に構築されるため静的初期化順序に依存しない）
    static std::vector<SlotControlBase*>& Registry() {
        static std::vector<SlotControlBase*> registry;
        return registry;
    }

    /// プールを全体台帳に登録し、番号を返す（空き番号があれば再利用する）
    static uint32_t RegisterPool(SlotControlBase* pool) {
        std::vector<SlotControlBase*>& registry = Registry();
        for (size_t poolId = 0; poolId < registry.size(); ++poolId) {
            if (registry[poolId] == nullptr) {
                registry[poolId] = pool;
                return static_cast<uint32_t>(poolId);
            }
        }
        registry.push_back(pool);
        return static_cast<uint32_t>(registry.size() - 1);
    }

    /** このプールの全体番号 */
    uint32_t m_poolId;
};
