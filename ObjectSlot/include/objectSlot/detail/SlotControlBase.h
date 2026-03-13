#pragma once

#include "SlotHandle.h"
#include <vector>
#include <queue>
#include <cassert>
#include <functional>

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
 */
class SlotControlBase {
public:
    virtual ~SlotControlBase() = default;

    /// ハンドルが有効かどうかを検証
    bool IsValidHandle(SlotHandle handle) const {
        if (handle.index == SlotHandle::INVALID_INDEX) {
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

    /// 生ポインタからスロットインデックスを取得（派生クラスで実装）
    virtual uint32_t IndexFromRawPtr(void* rawPtr) const = 0;

    /// SlotRefのポインタ更新用の登録（RefSlotSystemBaseで実装）
    virtual void RegisterRef(void** ptrLocation, uint32_t slotIndex) {
        (void)ptrLocation;
        (void)slotIndex;
    }

    /// SlotRefの登録を解除し、対応するスロットインデックスを返す
    /// 登録が見つからない場合はINVALID_INDEXを返す
    virtual uint32_t UnregisterRef(void** ptrLocation) {
        (void)ptrLocation;
        return SlotHandle::INVALID_INDEX;
    }

    /// ptrLocationからスロットインデックスを検索する（登録を解除しない）
    /// SlotRefのコピー操作時に使用する
    virtual uint32_t FindIndexByRef(const void* ptrLocation) const {
        (void)ptrLocation;
        return SlotHandle::INVALID_INDEX;
    }

    /// 無効な購読IDを表す定数
    static constexpr uint32_t INVALID_SUBSCRIPTION_ID = UINT32_MAX;

    /// SlotRefの購読登録に必要な情報を返す構造体
    struct SubscribeRefResult {
        uint32_t slotIndex = SlotHandle::INVALID_INDEX;
        uint32_t subscriptionId = INVALID_SUBSCRIPTION_ID;
    };

    /// SlotRefから解放通知を購読する（RefSlotSystemBaseで実装）
    /// ptrLocationでRefEntryを特定し、購読を登録してIDを返す
    /// 通知機能のないプールではデフォルト実装が空の結果を返す
    virtual SubscribeRefResult SubscribeByRef(void** ptrLocation, std::function<void()> callback) {
        (void)ptrLocation;
        (void)callback;
        return {};
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
};