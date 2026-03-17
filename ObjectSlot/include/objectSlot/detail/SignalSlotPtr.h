#pragma once

#include <functional>
#include "SlotHandle.h"
#include "Subscription.h"
#include "thirdparty/rootVector/RootVector.h"

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

template<typename T>
class WeakSignalSlotPtr;

class SlotControlBase;

/**
 * @brief 通知機能付き参照カウント方式スマートポインタ
 *
 * SignalSlotSystemBase内の要素への参照を管理し、
 * 購読パターンによる解放通知機能を持つ。
 *
 * root_pointerを内部に持つことで、全環境でGet()のコストを最小化する。
 * ネイティブ環境ではGet()はゼロコスト（生ポインタ返却）、
 * フォールバック環境ではポインタテーブル経由で安全にアクセスする。
 *
 * Subscribe()で登録されたコールバックは、
 * この要素の参照カウントが0になり解放される時に実行される。
 * 購読者はSubscribe()が返すSubscriptionオブジェクトを保持し、
 * Subscriptionの破棄で購読が自動解除される。
 *
 * 主な用途:
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
        : m_root_ptr()
        , m_slot(nullptr)
    {
    }

    /// nullptrからの構築
    SignalSlotPtr(std::nullptr_t)
        : m_root_ptr()
        , m_slot(nullptr)
    {
    }

    /// root_pointerとプールポインタを指定して構築
    SignalSlotPtr(typename root_vector<T>::root_pointer ptr, SignalSlotSystemBase<T>* slot)
        : m_root_ptr(ptr)
        , m_slot(slot)
    {
    }

    /// コピーコンストラクタ
    SignalSlotPtr(const SignalSlotPtr& other)
        : m_root_ptr(other.m_root_ptr)
        , m_slot(other.m_slot)
    {
        if (m_root_ptr && m_slot != nullptr)
            m_slot->AddRefByIndex(GetIndex());
    }

    /// コピー代入演算子
    SignalSlotPtr& operator=(const SignalSlotPtr& other)
    {
        if (this != &other) {
            Release();
            m_root_ptr = other.m_root_ptr;
            m_slot = other.m_slot;
            if (m_root_ptr && m_slot != nullptr)
                m_slot->AddRefByIndex(GetIndex());
        }
        return *this;
    }

    /// ムーブコンストラクタ
    SignalSlotPtr(SignalSlotPtr&& other) noexcept
        : m_root_ptr(other.m_root_ptr)
        , m_slot(other.m_slot)
    {
        other.m_root_ptr.reset();
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    SignalSlotPtr& operator=(SignalSlotPtr&& other) noexcept
    {
        if (this != &other) {
            Release();
            m_root_ptr = other.m_root_ptr;
            m_slot = other.m_slot;
            other.m_root_ptr.reset();
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

    /// アロー演算子（ゼロコスト）
    T* operator->() { return m_root_ptr.get(); }

    /// アロー演算子 (const版)
    const T* operator->() const { return m_root_ptr.get(); }

    /// 間接参照演算子
    T& operator*() { return *m_root_ptr; }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *m_root_ptr; }

    /// 要素へのポインタを取得（ゼロコスト）
    T* Get() { return m_root_ptr.get(); }

    /// 要素へのポインタを取得（ゼロコスト、const版）
    const T* Get() const { return m_root_ptr.get(); }

    /// 弱参照を生成
    WeakSignalSlotPtr<T> GetWeak() const;

    /// 別のSignalSlotPtrと内容を交換
    void Swap(SignalSlotPtr& other) noexcept {
        auto tmp = m_root_ptr;
        m_root_ptr = other.m_root_ptr;
        other.m_root_ptr = tmp;
        std::swap(m_slot, other.m_slot);
    }

    /// 参照が有効かどうかを判定
    bool IsValid() const {
        return static_cast<bool>(m_root_ptr) && m_slot != nullptr;
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCountByIndex(GetIndex());
    }

    /// 参照を解放
    void Reset() {
        Release();
        m_root_ptr.reset();
        m_slot = nullptr;
    }

    /// ハンドルを取得（インデックスからハンドルを再構築する）
    SlotHandle GetHandle() const {
        if (!IsValid()) return SlotHandle::Invalid();
        return m_slot->HandleFromIndex(GetIndex());
    }

    /// プールの非テンプレート基底を取得（SlotRef用）
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 解放通知の購読を登録
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (!m_root_ptr || m_slot == nullptr)
            return Subscription<T>();

        uint32_t index = GetIndex();
        uint32_t id = m_slot->AddSubscription(index, std::move(callback));
        return Subscription<T>(m_slot, index, id);
    }

    /// 等価比較（ポインタアドレスで比較）
    bool operator==(const SignalSlotPtr& other) const {
        return m_root_ptr.get() == other.m_root_ptr.get();
    }

    /// 非等価比較
    bool operator!=(const SignalSlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（ポインタアドレスで比較）
    bool operator<(const SignalSlotPtr& other) const { return m_root_ptr.get() < other.m_root_ptr.get(); }

    /// 以下比較
    bool operator<=(const SignalSlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SignalSlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SignalSlotPtr& other) const { return !(*this < other); }

private:
    /// スロットインデックスをポインタ演算で算出
    uint32_t GetIndex() const {
        return static_cast<uint32_t>(m_root_ptr.get() - m_slot->DataPtr());
    }

    /// 参照を解放する内部処理
    void Release() {
        if (m_root_ptr && m_slot != nullptr)
            m_slot->ReleaseRefByIndex(GetIndex());
    }

    /** 要素への安定ポインタ（全環境でGet()を最適化する） */
    typename root_vector<T>::root_pointer m_root_ptr;

    /** 要素が属する通知機能付きプールへのポインタ */
    SignalSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap関数
template<typename T>
void swap(SignalSlotPtr<T>& lhs, SignalSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化（ポインタアドレスのハッシュを使用）
namespace std {
    template<typename T>
    struct hash<SignalSlotPtr<T>> {
        size_t operator()(const SignalSlotPtr<T>& p) const {
            return hash<const T*>()(p.Get());
        }
    };
}

// SignalSlotPtr定義完了後にWeakSignalSlotPtrをインクルード
#include "WeakSignalSlotPtr.h"