#pragma once

#include "SlotHandle.h"
#include "Subscription.h"
#include <functional>

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

class SlotControlBase;

/**
 * @brief 通知機能付き参照カウント方式スマートポインタ
 *
 * SignalSlotSystemBase内の要素への参照を管理し、
 * 購読パターンによる解放通知機能を持つ。
 *
 * Subscribe()で登録されたコールバックは、
 * この要素の参照カウントが0になり解放される時に実行される。
 * 購読者はSubscribe()が返すSubscriptionオブジェクトを保持し、
 * Subscriptionの破棄で購読が自動解除される。
 *
 * 主な用途:
 * - VulkanデバイスやVMAなど、他のリソースが依存するオブジェクトの管理
 * - 解放順序の制御が必要なリソースの管理
 *
 * 基底型で持ち回したい場合はSlotRefに変換すること。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SignalSlotPtr
{
    // SlotRefが全てのSignalSlotPtr<U>のprivateメンバにアクセスするため
    template<typename U>
    friend class SlotRef;

public:
    /// デフォルトコンストラクタ
    SignalSlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /// nullptrからの構築
    SignalSlotPtr(std::nullptr_t)
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /// ハンドルとプールポインタを指定して構築
    SignalSlotPtr(SlotHandle handle, SignalSlotSystemBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /// コピーコンストラクタ
    SignalSlotPtr(const SignalSlotPtr& other)
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
            m_slot->AddRef(m_handle);
    }

    /// コピー代入演算子
    SignalSlotPtr& operator=(const SignalSlotPtr& other)
    {
        if (this != &other) {
            Release();
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
                m_slot->AddRef(m_handle);
        }
        return *this;
    }

    /// ムーブコンストラクタ
    SignalSlotPtr(SignalSlotPtr&& other) noexcept
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        other.m_handle = SlotHandle::Invalid();
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    SignalSlotPtr& operator=(SignalSlotPtr&& other) noexcept
    {
        if (this != &other) {
            Release();
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            other.m_handle = SlotHandle::Invalid();
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// nullptr代入演算子
    SignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SignalSlotPtr() { Release(); }

    /// アロー演算子
    T* operator->() { return Get(); }

    /// アロー演算子 (const版)
    const T* operator->() const { return Get(); }

    /// 間接参照演算子
    T& operator*() { return *Get(); }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *Get(); }

    /// 要素へのポインタを取得
    T* Get() {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /// 要素へのポインタを取得 (const版)
    const T* Get() const {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /// 参照が有効かどうかを判定
    bool IsValid() const {
        return m_slot != nullptr && m_slot->IsValidHandle(m_handle);
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCount(m_handle);
    }

    /// 参照を解放
    void Reset() {
        Release();
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /// ハンドルを取得
    SlotHandle GetHandle() const { return m_handle; }

    /// プールの非テンプレート基底を取得（SlotRef用）
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 解放通知の購読を登録
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (m_slot == nullptr || !m_slot->IsValidHandle(m_handle))
            return Subscription<T>();

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
    /// 参照を解放する内部処理
    void Release() {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle))
            m_slot->ReleaseRef(m_handle);
    }

    /** 要素を識別するハンドル */
    SlotHandle m_handle;

    /** 要素が属するプールへのポインタ */
    SignalSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }