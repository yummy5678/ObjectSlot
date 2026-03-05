#pragma once

#include "SlotHandle.h"
#include "Subscription.h"
#include <functional>

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

/**
 * @brief 通知機能付き参照カウント方式スマートポインタ
 *
 * SignalSlotBase内の要素への参照を管理し、
 * 購読パターンによる解放通知機能を持つ。
 *
 * Subscribe()で登録されたコールバックは、
 * この要素の参照カウントが0になり解放される時に実行される。
 * 購読者はSubscribe()が返すSubscriptionオブジェクトを保持し、
 * Subscriptionの破棄で購読が自動解除される。
 *
 * 主な用途:
 * - 他のリソースが依存するオブジェクトの管理
 * - 解放順序の制御が必要なリソースの管理
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SignalSlotPtr
{
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    SignalSlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief ハンドルとプールポインタを指定して構築
     *
     * @param handle プール内の要素を指すハンドル
     * @param slot 要素が属するプールへのポインタ
     */
    SignalSlotPtr(SlotHandle handle, SignalSlotSystemBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /**
     * @brief コピーコンストラクタ
     */
    SignalSlotPtr(const SignalSlotPtr& other)
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
        {
            m_slot->AddRef(m_handle);
        }
    }

    /**
     * @brief コピー代入演算子
     */
    SignalSlotPtr& operator=(const SignalSlotPtr& other)
    {
        if (this != &other)
        {
            Release();
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
            {
                m_slot->AddRef(m_handle);
            }
        }
        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     */
    SignalSlotPtr(SignalSlotPtr&& other) noexcept
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        other.m_handle = SlotHandle::Invalid();
        other.m_slot = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     */
    SignalSlotPtr& operator=(SignalSlotPtr&& other) noexcept
    {
        if (this != &other)
        {
            Release();
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            other.m_handle = SlotHandle::Invalid();
            other.m_slot = nullptr;
        }
        return *this;
    }

    /**
     * @brief nullptr代入演算子
     */
    SignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    /**
     * @brief デストラクタ
     */
    ~SignalSlotPtr()
    {
        Release();
    }

    /// アロー演算子
    T* operator->() { return Get(); }

    /// アロー演算子 (const版)
    const T* operator->() const { return Get(); }

    /// 間接参照演算子
    T& operator*() { return *Get(); }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *Get(); }

    /**
     * @brief 要素へのポインタを取得
     * @return 有効な場合は要素へのポインタ、無効な場合はnullptr
     */
    T* Get()
    {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 要素へのポインタを取得 (const版)
     */
    const T* Get() const
    {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 参照が有効かどうかを判定
     */
    bool IsValid() const
    {
        return m_slot != nullptr && m_slot->IsValidHandle(m_handle);
    }

    /**
     * @brief bool変換演算子
     */
    explicit operator bool() const { return IsValid(); }

    /**
     * @brief 参照カウントを取得
     */
    uint32_t UseCount() const
    {
        if (!IsValid()) return 0;
        return m_slot->GetRefCount(m_handle);
    }

    /**
     * @brief 参照を解放
     */
    void Reset()
    {
        Release();
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /**
     * @brief ハンドルを取得
     */
    SlotHandle GetHandle() const { return m_handle; }

    /**
     * @brief 解放通知の購読を登録
     *
     * この要素が解放される時に実行されるコールバックを登録する。
     * 返されるSubscriptionオブジェクトが破棄されると購読は自動解除される。
     *
     * @param callback 解放時に実行する関数
     * @return 購読オブジェクト（購読者側で保持すること）
     */
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (m_slot == nullptr || !m_slot->IsValidHandle(m_handle))
        {
            return Subscription<T>();
        }

        uint32_t id = m_slot->AddSubscription(m_handle.index, std::move(callback));
        return Subscription<T>(m_slot, m_handle.index, id);
    }

    /// 等価比較
    bool operator==(const SignalSlotPtr& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /// 非等価比較
    bool operator!=(const SignalSlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

private:
    void Release()
    {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
        {
            m_slot->ReleaseRef(m_handle);
        }
    }

    SlotHandle m_handle;
    SignalSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }