#pragma once

#include "SlotHandle.h"
#include <vector>
#include <queue>
#include <cassert>
#include <functional>
#include <algorithm>

/**
 * @brief 非テンプレートのプール制御基底クラス
 *
 * 参照カウント、世代番号、生存フラグなど
 * 型に依存しない管理機能を提供する。
 *
 * SlotRefがテンプレートを超えて参照カウント操作を
 * 行えるようにするための基盤クラス。
 *
 * 型依存のデータ（m_data）は派生クラスのObjectSlotSystemBaseが持つ。
 *
 * 【生存一覧】
 * ForEachが歯抜けのスロットを読み飛ばさずに済むよう、生存しているスロットの
 * 添字だけを並べた一覧を持つ。削除時は末尾の要素を空いた位置へ移して詰めるため
 * 一覧は常に隙間がなく、走査コストは生存数に比例する。
 * 詰める操作で並び順が崩れるため、SortActiveIndexList() で昇順に戻せる。
 */
class SlotControlBase {
public:
    virtual ~SlotControlBase() = default;

    /** 生存一覧内の位置が「未登録」であることを表す値 */
    static constexpr uint32_t INVALID_POSITION = UINT32_MAX;

    /// 生存スロットの添字一覧を取得（ForEachの走査順）
    const std::vector<uint32_t>& GetActiveIndexList() const { return m_activeIndexList; }

    /// 生存一覧が添字の昇順に並んでいるか
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

    /// ハンドルが有効かどうかを検証
    bool IsValidHandle(SlotHandle handle) const {
        if (handle.index >= m_alive.size()) {
            return false;
        }
        if (!m_alive[handle.index]) {
            return false;
        }
        if (m_generations[handle.index] != handle.generation) {
            return false;
        }
        return true;
    }

    /// 指定ハンドルの参照カウントを取得
    uint32_t GetRefCount(SlotHandle handle) const {
        if (!IsValidHandle(handle)) {
            return 0;
        }
        return m_refCounts[handle.index];
    }

    /// インデックス指定で参照カウントを取得（検証なし、SlotPtr/SignalSlotPtr用）
    uint32_t GetRefCountByIndex(uint32_t index) const {
        return m_refCounts[index];
    }

    /// 有効な要素数を取得
    size_t Count() const { return m_count; }

    /// プールの総容量を取得（削除済み含む）
    size_t Capacity() const { return m_alive.size(); }

    /// 最大容量を設定（0で無制限）
    void SetMaxCapacity(size_t maxCapacity) { m_maxCapacity = maxCapacity; }

    /// 最大容量を取得
    size_t GetMaxCapacity() const { return m_maxCapacity; }

    /// 新しい要素を追加可能か判定
    bool CanCreate() const {
        if (m_maxCapacity == 0) return true;
        return m_count < m_maxCapacity;
    }

    /// 無効な購読IDを表す定数
    static constexpr uint32_t INVALID_SUBSCRIPTION_ID = UINT32_MAX;

    /**
     * @brief 要素本体のアドレス一覧（スロット番号 → 本体のアドレス）の先頭を取得
     *
     * 型を知らない SlotRef が要素へ辿るために使う。一覧は派生クラスが持ち、
     * 再確保のたびにこのポインタを更新する。Compact() で本体が動いた時は
     * 一覧の中身が書き換わるので、スロット番号を持っていれば追従できる。
     */
    void* const* LocationData() const { return m_locationData; }

    /// 解放通知を購読する（通知機能のないプールでは INVALID_SUBSCRIPTION_ID を返す）
    virtual uint32_t SubscribeByIndex(uint32_t slotIndex, std::function<void()> callback) {
        (void)slotIndex;
        (void)callback;
        return INVALID_SUBSCRIPTION_ID;
    }

    /// インデックス指定で購読を解除する（SignalSlotSystemBaseで実装）
    /// SubscriptionRefのデストラクタから呼ばれる
    virtual void RemoveSubscriptionByIndex(uint32_t slotIndex, uint32_t subscriptionId) {
        (void)slotIndex;
        (void)subscriptionId;
    }
    
    /// インデックス指定で購読コールバックを差し替える（SignalSlotSystemBaseで実装）
    virtual void UpdateSubscriptionCallbackByIndex(uint32_t slotIndex, uint32_t subscriptionId, std::function<void()> callback) {
        (void)slotIndex;
        (void)subscriptionId;
        (void)callback;
    }


    /// インデックス指定で参照カウントを増加（SlotRef用）
    void AddRefByIndex(uint32_t index) {
        if (index < m_alive.size() && m_alive[index]) {
            ++m_refCounts[index];
        }
    }

    /// インデックス指定で参照カウントを減少（SlotRef用）
    void ReleaseRefByIndex(uint32_t index) {
        if (index < m_alive.size() && m_alive[index]) {
            assert(m_refCounts[index] > 0);
            --m_refCounts[index];

            if (m_refCounts[index] == 0) {
                SlotHandle handle{ index, m_generations[index] };
                RemoveInternal(handle);
            }
        }
    }

    /// インデックスからハンドルを構築
    SlotHandle HandleFromIndex(uint32_t index) const {
        return { index, m_generations[index] };
    }

protected:
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
     * @brief スロットを生存一覧から取り除く（隙間を詰める）
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

    /// ハンドル指定で参照カウントを増加
    void AddRef(SlotHandle handle) {
        if (IsValidHandle(handle)) {
            ++m_refCounts[handle.index];
        }
    }

    /// ハンドル指定で参照カウントを減少
    void ReleaseRef(SlotHandle handle) {
        if (IsValidHandle(handle)) {
            assert(m_refCounts[handle.index] > 0);
            --m_refCounts[handle.index];

            if (m_refCounts[handle.index] == 0) {
                RemoveInternal(handle);
            }
        }
    }

    /// 要素を削除する内部処理（派生クラスで実装）
    virtual void RemoveInternal(SlotHandle handle) = 0;

    /** 各スロットの世代番号 */
    std::vector<uint32_t> m_generations;

    /** 各スロットの生存フラグ */
    std::vector<bool> m_alive;

    /** 各スロットの参照カウント */
    std::vector<uint32_t> m_refCounts;

    /** 再利用可能なスロットのインデックス */
    std::queue<uint32_t> m_freeList;

    /** 有効な要素数 */
    size_t m_count = 0;

    /** 最大容量 (0は無制限) */
    size_t m_maxCapacity = 0;

    /** 要素本体のアドレス一覧の先頭（派生クラスが更新する） */
    void* const* m_locationData = nullptr;

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
};