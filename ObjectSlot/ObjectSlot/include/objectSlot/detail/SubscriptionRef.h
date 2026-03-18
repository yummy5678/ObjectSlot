#pragma once

#include "SlotControlBase.h"
#include <functional>

/**
 * @brief 非テンプレートの通知購読管理オブジェクト
 *
 * Subscription<T>の非テンプレート版。
 * SlotControlBase*経由で購読を管理するため、
 * SlotRefのように型消去された状況でも使用できる。
 *
 * このオブジェクトが生存している間は購読が有効で、
 * 破棄されると購読が自動的に解除される。
 *
 * 主な用途:
 * - SlotRefのSubscribe()が返す購読オブジェクト
 * - 型消去されたポインタから解放通知を受け取る場合
 *
 * コピー不可、ムーブ可能。
 */
class SubscriptionRef
{
public:
    /// デフォルトコンストラクタ（無効状態）
    SubscriptionRef()
        : m_control(nullptr)
        , m_slotIndex(0)
        , m_subscriptionId(SlotControlBase::INVALID_SUBSCRIPTION_ID)
    {
    }

    /**
     * @brief 購読情報を指定して構築
     *
     * @param control 購読先のプール制御ブロック
     * @param slotIndex 購読先のスロットインデックス
     * @param subscriptionId 購読を識別するID
     */
    SubscriptionRef(SlotControlBase* control, uint32_t slotIndex, uint32_t subscriptionId)
        : m_control(control)
        , m_slotIndex(slotIndex)
        , m_subscriptionId(subscriptionId)
    {
    }

    // コピー禁止
    SubscriptionRef(const SubscriptionRef&) = delete;
    SubscriptionRef& operator=(const SubscriptionRef&) = delete;

    /// ムーブコンストラクタ
    SubscriptionRef(SubscriptionRef&& other) noexcept
        : m_control(other.m_control)
        , m_slotIndex(other.m_slotIndex)
        , m_subscriptionId(other.m_subscriptionId)
    {
        other.m_control = nullptr;
    }

    /// ムーブ代入演算子
    SubscriptionRef& operator=(SubscriptionRef&& other) noexcept
    {
        if (this != &other)
        {
            Unsubscribe();

            m_control = other.m_control;
            m_slotIndex = other.m_slotIndex;
            m_subscriptionId = other.m_subscriptionId;

            other.m_control = nullptr;
        }
        return *this;
    }

    /**
     * @brief デストラクタ
     *
     * 有効な購読であれば自動的に解除する。
     */
    ~SubscriptionRef()
    {
        Unsubscribe();
    }

    /// 購読を手動で解除
    void Unsubscribe()
    {
        if (m_control != nullptr)
        {
            m_control->RemoveSubscriptionByIndex(m_slotIndex, m_subscriptionId);
            m_control = nullptr;
        }
    }

    /**
     * @brief 購読コールバックを差し替え
     *
     * @param newCallback 新しいコールバック関数
     */
    void UpdateCallback(std::function<void()> newCallback)
    {
        if (m_control != nullptr)
        {
            m_control->UpdateSubscriptionCallbackByIndex(
                m_slotIndex, m_subscriptionId, std::move(newCallback));
        }
    }

    /// 購読が有効かどうかを判定
    bool IsValid() const { return m_control != nullptr; }

private:
    /** 購読先のプール制御ブロック */
    SlotControlBase* m_control;

    /** 購読先のスロットインデックス */
    uint32_t m_slotIndex;

    /** 購読を識別するID */
    uint32_t m_subscriptionId;
};