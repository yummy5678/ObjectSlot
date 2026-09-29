#pragma once

#include "SlotControlBase.h"

#include <cstdint>
#include <functional>

/**
 * @class SubscriptionRef
 * @brief 要素の型を知らずに購読を管理する握り（RAIIで自動解除）
 *
 * 【責任】
 * - 破棄時に購読を解除する
 * - コールバックの差し替えと手動解除を提供する
 *
 * 【仕組み】
 * プールの全体番号・スロット添字・購読IDの3つ（12バイト）だけを持ち、
 * 操作時に全体台帳からプールを引く。プールが先に破棄されていれば何もしない。
 *
 * 【使用用途】
 * - SlotRef::Subscribe() の戻り値、および Subscription::ToRef() の結果
 */
class SubscriptionRef
{
public:
    /// 無効な状態で生成
    SubscriptionRef() = default;

    /// プールの全体番号・スロット添字・購読IDから生成
    SubscriptionRef(uint32_t poolId, uint32_t slotIndex, uint32_t subscriptionId)
        : m_poolId(poolId)
        , m_slotIndex(slotIndex)
        , m_subscriptionId(subscriptionId)
    {
    }

    SubscriptionRef(const SubscriptionRef&) = delete;
    SubscriptionRef& operator=(const SubscriptionRef&) = delete;

    /// ムーブ（元は無効になる）
    SubscriptionRef(SubscriptionRef&& other) noexcept
        : m_poolId(other.m_poolId)
        , m_slotIndex(other.m_slotIndex)
        , m_subscriptionId(other.m_subscriptionId)
    {
        other.m_poolId = SlotControlBase::INVALID_POOL_ID;
    }

    /// ムーブ代入（自分の購読は解除してから引き継ぐ）
    SubscriptionRef& operator=(SubscriptionRef&& other) noexcept
    {
        if (this != &other)
        {
            Unsubscribe();
            m_poolId = other.m_poolId;
            m_slotIndex = other.m_slotIndex;
            m_subscriptionId = other.m_subscriptionId;
            other.m_poolId = SlotControlBase::INVALID_POOL_ID;
        }
        return *this;
    }

    /// 破棄時に購読を解除
    ~SubscriptionRef()
    {
        Unsubscribe();
    }

    /// 購読を解除する（プールが破棄済みなら何もしない）
    void Unsubscribe()
    {
        if (m_poolId == SlotControlBase::INVALID_POOL_ID) return;
        SlotControlBase* pool = SlotControlBase::PoolFromId(m_poolId);
        if (pool != nullptr)
        {
            pool->RemoveSubscriptionByIndex(m_slotIndex, m_subscriptionId);
        }
        m_poolId = SlotControlBase::INVALID_POOL_ID;
    }

    /// コールバックを差し替える
    void UpdateCallback(std::function<void()> newCallback)
    {
        if (m_poolId == SlotControlBase::INVALID_POOL_ID) return;
        SlotControlBase* pool = SlotControlBase::PoolFromId(m_poolId);
        if (pool != nullptr)
        {
            pool->UpdateSubscriptionCallbackByIndex(m_slotIndex, m_subscriptionId, std::move(newCallback));
        }
    }

    /// 有効な購読を保持しているか
    bool IsValid() const { return m_poolId != SlotControlBase::INVALID_POOL_ID; }

private:
    /** プールの全体番号（無効なら INVALID_POOL_ID） */
    uint32_t m_poolId = SlotControlBase::INVALID_POOL_ID;

    /** スロットの添字 */
    uint32_t m_slotIndex = 0;

    /** 購読ID */
    uint32_t m_subscriptionId = SlotControlBase::INVALID_SUBSCRIPTION_ID;
};
