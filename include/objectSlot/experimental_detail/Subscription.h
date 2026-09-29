#pragma once

#include "SlotHeader.h"
#include "SlotStorage.h"
#include "SubscriptionRef.h"

#include <cstdint>
#include <functional>

template<typename T>
class SignalSlotSystemBase;

/**
 * @class Subscription
 * @brief 解放通知の購読を表す握り（RAIIで自動解除）
 *
 * 【責任】
 * - 破棄時に購読を解除する
 * - コールバックの差し替えと手動解除を提供する
 * - 型消去した SubscriptionRef への変換を提供する
 *
 * 【仕組み】
 * 購読先スロットの圧縮値（タグ＋オフセット）と購読IDの8バイトだけを持つ。
 * プールは圧縮値のタグから型ごとの台帳で引く。
 *
 * 【使用用途】
 * - SignalSlotPtr::Subscribe() / WeakSignalSlotPtr::Subscribe() の戻り値
 *
 * @tparam T 購読先の要素の型
 */
template<typename T>
class Subscription
{
public:
    using Storage = SlotStorage<T>;

    /// 無効な状態で生成
    Subscription() = default;

    /// 購読先スロットの圧縮値と購読IDから生成
    Subscription(uint32_t slotValue, uint32_t subscriptionId)
        : m_slotValue(slotValue)
        , m_subscriptionId(subscriptionId)
    {
    }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /// ムーブ（元は無効になる）
    Subscription(Subscription&& other) noexcept
        : m_slotValue(other.m_slotValue)
        , m_subscriptionId(other.m_subscriptionId)
    {
        other.m_slotValue = SlotValue::INVALID;
    }

    /// ムーブ代入（自分の購読は解除してから引き継ぐ）
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Unsubscribe();
            m_slotValue = other.m_slotValue;
            m_subscriptionId = other.m_subscriptionId;
            other.m_slotValue = SlotValue::INVALID;
        }
        return *this;
    }

    /// 破棄時に購読を解除
    ~Subscription()
    {
        Unsubscribe();
    }

    /// 購読を解除する（プールが破棄済みなら何もしない）
    void Unsubscribe()
    {
        if (m_slotValue == SlotValue::INVALID) return;
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool != nullptr)
        {
            pool->RemoveSubscription(Storage::IndexFromValue(m_slotValue), m_subscriptionId);
        }
        m_slotValue = SlotValue::INVALID;
    }

    /// コールバックを差し替える
    void UpdateCallback(std::function<void()> newCallback)
    {
        if (m_slotValue == SlotValue::INVALID) return;
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool != nullptr)
        {
            pool->UpdateSubscriptionCallback(Storage::IndexFromValue(m_slotValue), m_subscriptionId, std::move(newCallback));
        }
    }

    /**
     * @brief 型消去した SubscriptionRef に変換する
     *
     * 変換後はこのオブジェクトは無効になり、解除の責任は SubscriptionRef に移る。
     *
     * @return 同じ購読を管理する SubscriptionRef
     */
    SubscriptionRef ToRef()
    {
        if (m_slotValue == SlotValue::INVALID) return SubscriptionRef();
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool == nullptr)
        {
            m_slotValue = SlotValue::INVALID;
            return SubscriptionRef();
        }
        SubscriptionRef ref(pool->GetPoolId(), Storage::IndexFromValue(m_slotValue), m_subscriptionId);
        m_slotValue = SlotValue::INVALID;
        return ref;
    }

    /// 有効な購読を保持しているか
    bool IsValid() const { return m_slotValue != SlotValue::INVALID; }

private:
    /// 購読先のプールを取得（破棄済みなら nullptr）
    SignalSlotSystemBase<T>* Pool() const
    {
        return SignalSlotSystemBase<T>::SignalPoolFromTag(SlotValue::Tag(m_slotValue));
    }

    /** 購読先スロットの圧縮値（無効なら INVALID） */
    uint32_t m_slotValue = SlotValue::INVALID;

    /** 購読ID */
    uint32_t m_subscriptionId = 0;
};
