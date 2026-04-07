#pragma once

#include "SlotHandle.h"
#include "Subscription.h"
#include "SignalSlotPtr.h"
#include <functional>
#include <algorithm>

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

class SlotControlBase;

/**
 * @brief 通知機能付きオブジェクトプール用の弱参照スマートポインタ
 *
 * SignalSlotSystemBase内の要素への弱い参照を管理する。
 * 参照カウントに影響を与えないため、オーナーの寿命には干渉しない。
 *
 * Subscribe()で解放通知を購読できるため、
 * オーナーが解放される直前にコールバックを実行できる。
 *
 * 要素にアクセスする場合はLock()でSignalSlotPtrに変換すること。
 * Lock()で得たSignalSlotPtrが生存している間は
 * 要素の破棄が防がれるため、安全にアクセスできる。
 *
 * 主な用途:
 * - 依存リソースがオーナーの解放通知を受け取りつつ、
 *   オーナーの参照カウントには干渉しない参照を保持する場合
 * - オーナーの参照カウントが0になった時点で
 *   通知を受け取り、自身のリソースを先に解放する場合
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class WeakSignalSlotPtr
{
public:
    /// デフォルトコンストラクタ
    WeakSignalSlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /// nullptrからの構築
    WeakSignalSlotPtr(std::nullptr_t)
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief SignalSlotPtrからの変換コンストラクタ
     *
     * 強参照から弱参照を生成する。
     * 参照カウントは増加しない。
     *
     * @param other 変換元の強参照
     */
    WeakSignalSlotPtr(const SignalSlotPtr<T>& other)
        : m_handle(other.GetHandle())
        , m_slot(nullptr)
    {
        if (other.IsValid()) {
            m_slot = static_cast<SignalSlotSystemBase<T>*>(other.GetControl());
        }
    }

    /// コピーコンストラクタ（参照カウントに影響しない）
    WeakSignalSlotPtr(const WeakSignalSlotPtr& other)
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
    }

    /// コピー代入演算子（参照カウントに影響しない）
    WeakSignalSlotPtr& operator=(const WeakSignalSlotPtr& other)
    {
        m_handle = other.m_handle;
        m_slot = other.m_slot;
        return *this;
    }

    /// SignalSlotPtrからの変換代入演算子
    WeakSignalSlotPtr& operator=(const SignalSlotPtr<T>& other)
    {
        if (other.IsValid()) {
            m_handle = other.GetHandle();
            m_slot = static_cast<SignalSlotSystemBase<T>*>(other.GetControl());
        }
        else {
            m_handle = SlotHandle::Invalid();
            m_slot = nullptr;
        }
        return *this;
    }

    /// ムーブコンストラクタ
    WeakSignalSlotPtr(WeakSignalSlotPtr&& other) noexcept
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        other.m_handle = SlotHandle::Invalid();
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    WeakSignalSlotPtr& operator=(WeakSignalSlotPtr&& other) noexcept
    {
        if (this != &other) {
            m_handle = other.m_handle;
            m_slot = other.m_slot;
            other.m_handle = SlotHandle::Invalid();
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// nullptr代入演算子
    WeakSignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    /// デストラクタ（参照カウントに影響しない）
    ~WeakSignalSlotPtr() = default;

    /**
     * @brief 参照先が有効かどうかを判定
     * @return 要素がまだ生存していればtrue
     */
    bool IsValid() const
    {
        return m_slot != nullptr && m_slot->IsValidHandle(m_handle);
    }

    /**
     * @brief 参照先が無効（期限切れ）かどうかを判定
     * @return 要素が削除済みまたは未初期化ならtrue
     */
    bool IsExpired() const
    {
        return !IsValid();
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /**
     * @brief 参照先の参照カウントを取得
     * @return 有効な場合は現在の参照カウント、無効なら0
     */
    uint32_t UseCount() const
    {
        if (!IsValid()) return 0;
        return m_slot->GetRefCount(m_handle);
    }

    /**
     * @brief 弱参照からSignalSlotPtrを生成
     *
     * 要素がまだ有効であれば強参照のSignalSlotPtrを返す。
     * 無効であれば空のSignalSlotPtrを返す。
     * 返されたSignalSlotPtrが生存中は要素の破棄が防がれる。
     *
     * @return 有効な場合はSignalSlotPtr、無効な場合は空
     */
    SignalSlotPtr<T> Lock() const
    {
        if (IsExpired()) {
            return SignalSlotPtr<T>();
        }
        m_slot->AddRef(m_handle);
        auto rp = m_slot->GetRootPointer(m_handle.index);
        return SignalSlotPtr<T>(rp, m_slot);
    }

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
        if (m_slot == nullptr || !m_slot->IsValidHandle(m_handle)) {
            return Subscription<T>();
        }
        uint32_t id = m_slot->AddSubscription(m_handle.index, std::move(callback));
        return Subscription<T>(m_slot, m_handle.index, id);
    }

    /// 弱参照をリセット
    void Reset()
    {
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /// 別のWeakSignalSlotPtrと内容を交換
    void Swap(WeakSignalSlotPtr& other) noexcept
    {
        std::swap(m_handle, other.m_handle);
        std::swap(m_slot, other.m_slot);
    }

    /// ハンドルを取得
    SlotHandle GetHandle() const { return m_handle; }

    /// プールの非テンプレート基底を取得
    SlotControlBase* GetControl() const
    {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 等価比較
    bool operator==(const WeakSignalSlotPtr& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /// 非等価比較
    bool operator!=(const WeakSignalSlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（コンテナのキーとして使用可能にする）
    bool operator<(const WeakSignalSlotPtr& other) const { return m_handle < other.m_handle; }
    
    /// 以下比較
    bool operator<=(const WeakSignalSlotPtr& other) const { return !(other < *this); }
    
    /// 大なり比較
    bool operator>(const WeakSignalSlotPtr& other) const { return other < *this; }
    
    /// 以上比較
    bool operator>=(const WeakSignalSlotPtr& other) const { return !(*this < other); }

private:
    /** 要素を識別するハンドル */
    SlotHandle m_handle;

    /** 要素が属する通知機能付きプールへのポインタ */
    SignalSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const WeakSignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const WeakSignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap関数
template<typename T>
void swap(WeakSignalSlotPtr<T>& lhs, WeakSignalSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化
namespace std {
    template<typename T>
    struct hash<WeakSignalSlotPtr<T>> {
        size_t operator()(const WeakSignalSlotPtr<T>& p) const {
            return hash<SlotHandle>()(p.GetHandle());
        }
    };
}

template<typename T>
WeakSignalSlotPtr<T> SignalSlotPtr<T>::GetWeak() const {
    return WeakSignalSlotPtr<T>(*this);
}

