#pragma once

#include "SlotHandle.h"
#include <functional>

// 前方宣言
template<typename T>
class ObjectSlotSystemBase;

template<typename T>
class WeakSlotPtr;

class SlotControlBase;

/**
 * @brief 参照カウント方式のスマートポインタ（軽量版）
 *
 * オブジェクトプール内の要素への参照を管理する。
 * コピー時に参照カウントが増加し、破棄時に減少する。
 * 参照カウントが0になると、要素は自動的に削除される。
 *
 * 通知機能が不要な場合に使用する。
 * 通知機能が必要な場合はSignalSlotPtrを使用すること。
 * 基底型で持ち回したい場合はSlotRefに変換すること。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SlotPtr {
    friend class WeakSlotPtr<T>;

    // SlotRefが全てのSlotPtr<U>のprivateメンバにアクセスするため
    template<typename U>
    friend class SlotRef;

public:
    /**
     * @brief デフォルトコンストラクタ
     */
    SlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief nullptrからの構築
     */
    SlotPtr(std::nullptr_t)
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief ハンドルとプールポインタを指定して構築
     */
    SlotPtr(SlotHandle handle, ObjectSlotSystemBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /**
     * @brief コピーコンストラクタ
     */
    SlotPtr(const SlotPtr& other)
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->AddRef(m_handle);
        }
    }

    /**
     * @brief コピー代入演算子
     */
    SlotPtr& operator=(const SlotPtr& other) {
        if (this != &other) {
            Release();
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
                m_slot->AddRef(m_handle);
            }
        }
        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     */
    SlotPtr(SlotPtr&& other) noexcept
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        other.m_handle = SlotHandle::Invalid();
        other.m_slot = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     */
    SlotPtr& operator=(SlotPtr&& other) noexcept {
        if (this != &other) {
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
    SlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /**
     * @brief デストラクタ
     */
    ~SlotPtr() {
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
     */
    T* Get() {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 要素へのポインタを取得 (const版)
     */
    const T* Get() const {
        if (!IsValid()) return nullptr;
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 参照が有効かどうかを判定
     */
    bool IsValid() const {
        return m_slot != nullptr && m_slot->IsValidHandle(m_handle);
    }

    /**
     * @brief bool変換演算子
     */
    explicit operator bool() const { return IsValid(); }

    /**
     * @brief 参照カウントを取得
     */
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCount(m_handle);
    }

    /**
     * @brief 弱参照を生成
     */
    WeakSlotPtr<T> GetWeak() const;

    /**
     * @brief 参照を解放
     */
    void Reset() {
        Release();
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /**
     * @brief ハンドルを取得
     */
    SlotHandle GetHandle() const { return m_handle; }

    /**
     * @brief プールの非テンプレート基底を取得（SlotRef用）
     */
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 等価比較
    bool operator==(const SlotPtr& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /// 非等価比較
    bool operator!=(const SlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

private:
    void Release() {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->ReleaseRef(m_handle);
        }
    }

    SlotHandle m_handle;
    ObjectSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs != nullptr; }