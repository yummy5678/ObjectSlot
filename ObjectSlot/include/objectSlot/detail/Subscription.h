#pragma once
#include <cstdint>
#include <functional>

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

/**
 * @brief 通知購読の有効期間を管理するオブジェクト
 *
 * SystemのSubscribe()が返す購読オブジェクトです。
 * このオブジェクトが生存している間は購読が有効で、
 * 破棄されると購読が自動的に解除されます。
 *
 * 購読者側がメンバ変数として保持することで、
 * 購読者の寿命と購読の寿命を一致させます。
 *
 * コピー不可、ムーブ可能です。
 *
 * @tparam T 購読先のプールが管理する要素の型
 */
template<typename T>
class Subscription
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    Subscription()
        : m_slot(nullptr)
        , m_slotIndex(0)
        , m_subscriptionId(0)
        , m_valid(false)
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
        , m_valid(true)
    {
    }

    // コピー禁止
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    /**
     * @brief ムーブコンストラクタ
     */
    Subscription(Subscription&& other) noexcept
        : m_slot(other.m_slot)
        , m_slotIndex(other.m_slotIndex)
        , m_subscriptionId(other.m_subscriptionId)
        , m_valid(other.m_valid)
    {
        other.m_slot = nullptr;
        other.m_valid = false;
    }

    /**
     * @brief ムーブ代入演算子
     */
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other)
        {
            Unsubscribe();

            m_slot = other.m_slot;
            m_slotIndex = other.m_slotIndex;
            m_subscriptionId = other.m_subscriptionId;
            m_valid = other.m_valid;

            other.m_slot = nullptr;
            other.m_valid = false;
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

    /**
     * @brief 購読を手動で解除
     */
    void Unsubscribe()
    {
        if (m_valid && m_slot != nullptr)
        {
            m_slot->RemoveSubscription(m_slotIndex, m_subscriptionId);
            m_slot = nullptr;
            m_valid = false;
        }
    }

    /**
     * @brief 購読コールバックを差し替え
     *
     * 購読IDはそのままに、実行される関数だけを更新する。
     * 主にムーブ操作時、コールバック内でキャプチャした
     * thisポインタを新しいアドレスに差し替えるために使用する。
     *
     * @param newCallback 新しいコールバック関数
     */
    void UpdateCallback(std::function<void()> newCallback)
    {
        if (m_valid && m_slot != nullptr)
        {
            m_slot->UpdateSubscriptionCallback(m_slotIndex, m_subscriptionId, std::move(newCallback));
        }
    }

    /**
     * @brief 購読が有効かどうかを判定
     * @return 有効ならtrue
     */
    bool IsValid() const
    {
        return m_valid;
    }

private:
    /** 購読先の要素が属するプール */
    SignalSlotSystemBase<T>* m_slot;

    /** 購読先のスロットインデックス */
    uint32_t m_slotIndex;

    /** 購読を識別するID */
    uint32_t m_subscriptionId;

    /** 購読が有効かどうか */
    bool m_valid;
};