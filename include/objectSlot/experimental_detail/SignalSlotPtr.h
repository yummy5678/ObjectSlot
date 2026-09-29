#pragma once

#include "SlotHandle.h"
#include "SlotHeader.h"
#include "SlotStorage.h"
#include "SignalSlotSystemBase.h"
#include "Subscription.h"

#include <functional>
#include <utility>

template<typename T>
class SignalSlotSystem;

template<typename T>
class WeakSignalSlotPtr;

template<typename T>
class EnableSlotFromThis;

template<typename T>
class SlotRef;

class SlotControlBase;

/**
 * @class SignalSlotPtr
 * @brief SignalSlotSystem が管理する要素への4バイトの強参照ポインタ（解放通知付き）
 *
 * 【責任】
 * - 要素の所有権（参照カウント）を保持し、最後の所有者が消えた時に要素を削除させる
 * - Subscribe() で、要素の解放時に呼ばれるコールバックを登録する
 *
 * 【SlotPtrとの違い】
 * - 参照カウントが0になった時の削除が、購読者への通知を経由する
 * - それ以外（サイズ、アクセスコスト、コピー・破棄のコスト）は同じ
 *
 * 【注意事項】
 * - 購読コールバックにこのポインタ自身を参照キャプチャしてはいけない
 *   （ダングリングの原因になる）。値でキャプチャするか弱参照を使う
 *
 * @tparam T 要素の型
 */
template<typename T>
class SignalSlotPtr
{
    friend class SignalSlotSystem<T>;
    friend class WeakSignalSlotPtr<T>;
    friend class EnableSlotFromThis<T>;
    template<typename U>
    friend class SlotRef;

public:
    using Storage = SlotStorage<T>;

    /// 空のポインタを生成
    SignalSlotPtr() = default;

    /// nullptr から空のポインタを生成
    SignalSlotPtr(std::nullptr_t) {}

    /// コピー（参照カウントを増やす）
    SignalSlotPtr(const SignalSlotPtr& other)
        : m_value(other.m_value)
    {
        AddRef();
    }

    /// コピー代入（元の参照を手放し、新しい参照を得る）
    SignalSlotPtr& operator=(const SignalSlotPtr& other)
    {
        if (this != &other) {
            Release();
            m_value = other.m_value;
            AddRef();
        }
        return *this;
    }

    /// ムーブ（参照カウントは変えず、元を空にする）
    SignalSlotPtr(SignalSlotPtr&& other) noexcept
        : m_value(other.m_value)
    {
        other.m_value = SlotValue::INVALID;
    }

    /// ムーブ代入
    SignalSlotPtr& operator=(SignalSlotPtr&& other) noexcept
    {
        if (this != &other) {
            Release();
            m_value = other.m_value;
            other.m_value = SlotValue::INVALID;
        }
        return *this;
    }

    /// nullptr 代入で参照を手放す
    SignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    /// 参照を手放す
    ~SignalSlotPtr() { Release(); }

    // ================================================================
    // 要素アクセス
    // ================================================================

    /// 要素へのポインタ（検証なし）
    T* operator->() { return Storage::ElementAt(m_value); }

    /// 要素へのポインタ（検証なし、const版）
    const T* operator->() const { return Storage::ElementAt(m_value); }

    /// 要素への参照（検証なし）
    T& operator*() { return *Storage::ElementAt(m_value); }

    /// 要素への参照（検証なし、const版）
    const T& operator*() const { return *Storage::ElementAt(m_value); }

    /// 要素へのポインタ（無効なら nullptr）
    T* Get() {
        if (!IsValid()) return nullptr;
        return Storage::ElementAt(m_value);
    }

    /// 要素へのポインタ（無効なら nullptr、const版）
    const T* Get() const {
        if (!IsValid()) return nullptr;
        return Storage::ElementAt(m_value);
    }

    // ================================================================
    // 状態
    // ================================================================

    /// 有効な要素を指しているか
    bool IsValid() const { return m_value != SlotValue::INVALID; }

    /// bool変換（IsValidと同じ）
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得（無効なら0）
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return Header()->refCount;
    }

    /// 弱参照を作る
    WeakSignalSlotPtr<T> GetWeak() const;

    /// 中身を入れ替える
    void Swap(SignalSlotPtr& other) noexcept {
        std::swap(m_value, other.m_value);
    }

    /// 参照を手放して空にする
    void Reset() {
        Release();
        m_value = SlotValue::INVALID;
    }

    /// 要素のハンドルを取得（無効なら Invalid）
    SlotHandle GetHandle() const {
        if (!IsValid()) return SlotHandle::Invalid();
        return SlotHandle{ Storage::IndexFromValue(m_value), Header()->generation };
    }

    /// 所属プールのタグを取得
    uint8_t GetTag() const { return SlotValue::Tag(m_value); }

    /// 圧縮値を取得（デバッグ・並べ替え用）
    uint32_t GetValue() const { return m_value; }

    /// 所属プールを型消去した形で取得（無効なら nullptr）
    SlotControlBase* GetControl() const {
        if (!IsValid()) return nullptr;
        return Pool();
    }

    /**
     * @brief 要素の解放時に呼ばれるコールバックを登録する
     *
     * @param callback 解放時に呼ぶ関数
     * @return 購読の握り（破棄で自動解除）。無効なポインタなら空の握り
     */
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (!IsValid()) return Subscription<T>();
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool == nullptr) return Subscription<T>();
        const uint32_t id = pool->AddSubscription(Storage::IndexFromValue(m_value), std::move(callback));
        return Subscription<T>(m_value, id);
    }

    // ================================================================
    // 比較
    // ================================================================

    bool operator==(const SignalSlotPtr& other) const { return m_value == other.m_value; }
    bool operator!=(const SignalSlotPtr& other) const { return !(*this == other); }
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }
    bool operator<(const SignalSlotPtr& other) const { return m_value < other.m_value; }
    bool operator<=(const SignalSlotPtr& other) const { return !(other < *this); }
    bool operator>(const SignalSlotPtr& other) const { return other < *this; }
    bool operator>=(const SignalSlotPtr& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const { return std::hash<uint32_t>()(m_value); }

private:
    /// 圧縮値から生成する（参照カウントは呼び出し側が増やしておくこと）
    explicit SignalSlotPtr(uint32_t value)
        : m_value(value)
    {
    }

    /// 見出しを取得（検証なし）
    SlotHeader* Header() const { return Storage::HeaderAt(m_value); }

    /// 要素へのポインタを取得（検証なし。SlotRef の構築用に非constで返す）
    T* Element() const { return Storage::ElementAt(m_value); }


    /// 所属プールを取得（破棄済みなら nullptr）
    SignalSlotSystemBase<T>* Pool() const {
        return SignalSlotSystemBase<T>::SignalPoolFromTag(SlotValue::Tag(m_value));
    }

    /// 参照カウントを増やす（削除済みの見出しには何もしない）
    void AddRef() {
        if (!IsValid()) return;
        SlotHeader* header = Header();
        if (header->refCount != 0) {
            ++header->refCount;
        }
    }

    /// 参照カウントを減らし、0になれば所属プールに削除を依頼する
    void Release() {
        if (!IsValid()) return;
        SlotHeader* header = Header();
        if (header->refCount == 0) return;
        --header->refCount;
        if (header->refCount == 0) {
            SignalSlotSystemBase<T>* pool = Pool();
            if (pool != nullptr) {
                pool->OnRefCountReachedZeroByValue(m_value);
            }
        }
    }

    /** 圧縮値（タグ4ビット＋オフセット28ビット） */
    uint32_t m_value = SlotValue::INVALID;
};

template<typename T>
bool operator==(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap
template<typename T>
void swap(SignalSlotPtr<T>& lhs, SignalSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

namespace std {
    template<typename T>
    struct hash<SignalSlotPtr<T>> {
        size_t operator()(const SignalSlotPtr<T>& p) const {
            return p.HashValue();
        }
    };
}

#include "WeakSignalSlotPtr.h"
