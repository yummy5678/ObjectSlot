#pragma once
#include <cstdint>
#include <functional>

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

/**
 * @brief 通知購読の有効期間を管理するオブジェクト
 *
 * SystemのSubscribe()が返す購読オブジェクト。
 * このオブジェクトが生存している間は購読が有効で、
 * 破棄されると購読が自動的に解除される。
 *
 * 購読者側がメンバ変数として保持することで、
 * 購読者の寿命と購読の寿命を一致させる。
 *
 * コピー不可、ムーブ可能。
 *
 * @tparam T 購読先のプールが管理する要素の型
 */
template<typename T>
class Subscription
{
public:
    /// デフォルトコンストラクタ（無効状態）
    Subscription()
        : m_slot(nullptr)
        , m_slotIndex(0)
        , m_subscriptionId(0)
    {
    }

    /**
     * @brief 購読情報を指定して構築
     *
     * @param slot 購読先の要素が属するプール
     * @param slotIndex 購読先の要素のスロットインデックス
     * @param subscriptionId 購読を識別するID
     */
    Subscription(SignalSlotSystemBase<T>* slot, uint32_t slotIndex, uint32_t subscriptionId)
        : m_slot(slot)
        , m_slotIndex(slotIndex)
        , m_subscriptionId(subscriptionId)
    {
    }

    // コピー禁止
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /// ムーブコンストラクタ
    Subscription(Subscription&& other) noexcept
        : m_slot(other.m_slot)
        , m_slotIndex(other.m_slotIndex)
        , m_subscriptionId(other.m_subscriptionId)
    {
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Unsubscribe();

            m_slot = other.m_slot;
            m_slotIndex = other.m_slotIndex;
            m_subscriptionId = other.m_subscriptionId;

            other.m_slot = nullptr;
        }
        return *this;
    }

    /**
     * @brief デストラクタ
     *
     * 有効な購読であれば自動的に解除する。
     */
    ~Subscription()
    {
        Unsubscribe();
    }

    /// 購読を手動で解除
    void Unsubscribe()
    {
        if (m_slot != nullptr)
        {
            m_slot->RemoveSubscription(m_slotIndex, m_subscriptionId);
            m_slot = nullptr;
        }
    }

    /**
     * @brief 購読コールバックを差し替え
     *
     * @param newCallback 新しいコールバック関数
     */
    void UpdateCallback(std::function<void()> newCallback)
    {
        if (m_slot != nullptr)
        {
            m_slot->UpdateSubscriptionCallback(m_slotIndex, m_subscriptionId, std::move(newCallback));
        }
    }

    /// 購読が有効かどうかを判定
    bool IsValid() const { return m_slot != nullptr; }

private:
    /** 購読先の要素が属するプール */
    SignalSlotSystemBase<T>* m_slot;

    /** 購読先のスロットインデックス */
    uint32_t m_slotIndex;

    /** 購読を識別するID */
    uint32_t m_subscriptionId;
};