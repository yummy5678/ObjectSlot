#pragma once

#include "SlotControlBase.h"
#include "SlotPtr.h"
#include "SignalSlotPtr.h"
#include "SubscriptionRef.h"

#include <type_traits>
#include <functional>
#include <utility>

/**
 * @brief 異なる型のプールの要素を、共通の基底型として扱える強参照
 *
 * 【責任】
 * - 要素の所有権（参照カウント）を保持する
 * - 要素の基底クラス部分、または要素のメンバ（エイリアシング）を指す
 * - 要素の型を知らずに参照カウント操作・解放通知の購読を行う
 *
 * 【仕組み】
 * プールの非テンプレート基底へのポインタ、スロット番号、要素の先頭から
 * 指したい場所までのバイト差の3つを持つ。要素本体へはプールの位置一覧を
 * スロット番号で引いて辿るので、Compact() で本体が動いても追従できる。
 *
 * 【使用用途】
 * - std::vector<SlotRef<IDrawable>> のように、具体型の異なる要素を一列に並べて扱う
 * - どのプール（ObjectSlotSystem / SignalSlotSystem）の要素からでも作れる
 *
 * 【注意事項】
 * - Subscribe() は通知機能のあるプールの要素に対してのみ有効
 * - エイリアシング構築で指す先は、所有する要素の内部でなければならない
 *
 * @tparam T 参照する型（要素の基底クラス、またはエイリアシング先の型）
 */
template<typename T>
class SlotRef {
public:
    /// デフォルトコンストラクタ
    SlotRef() = default;

    /// nullptrからの構築
    SlotRef(std::nullptr_t) {}

    /// SlotPtr から生成（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SlotPtr<U>& other) {
        if (other.IsValid()) {
            U* element = const_cast<U*>(other.Get());
            Bind(other.GetControl(), other.GetIndex(), element, static_cast<T*>(element));
        }
    }

    /// SignalSlotPtr から生成（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SignalSlotPtr<U>& other) {
        if (other.IsValid()) {
            U* element = const_cast<U*>(other.Get());
            Bind(other.GetControl(), other.GetIndex(), element, static_cast<T*>(element));
        }
    }

    /**
     * @brief エイリアシング構築（所有権は owner の要素、指す先は aliasPtr）
     *
     * @param owner    所有権を共有する要素へのポインタ
     * @param aliasPtr 実際に指すアドレス（owner の要素の内部であること）
     */
    template<typename U>
    SlotRef(const SlotPtr<U>& owner, T* aliasPtr) {
        if (aliasPtr != nullptr && owner.IsValid()) {
            Bind(owner.GetControl(), owner.GetIndex(), const_cast<U*>(owner.Get()), aliasPtr);
        }
    }

    /// エイリアシング構築（SignalSlotPtr 版）
    template<typename U>
    SlotRef(const SignalSlotPtr<U>& owner, T* aliasPtr) {
        if (aliasPtr != nullptr && owner.IsValid()) {
            Bind(owner.GetControl(), owner.GetIndex(), const_cast<U*>(owner.Get()), aliasPtr);
        }
    }

    /// コピーコンストラクタ（参照カウントを増やす）
    SlotRef(const SlotRef& other)
        : m_control(other.m_control)
        , m_index(other.m_index)
        , m_adjust(other.m_adjust)
    {
        AddRef();
    }

    /// コピー代入演算子
    SlotRef& operator=(const SlotRef& other) {
        if (this != &other) {
            Release();
            m_control = other.m_control;
            m_index = other.m_index;
            m_adjust = other.m_adjust;
            AddRef();
        }
        return *this;
    }

    /// SlotPtr を代入（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef& operator=(const SlotPtr<U>& other) {
        *this = SlotRef(other);
        return *this;
    }

    /// SignalSlotPtr を代入（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef& operator=(const SignalSlotPtr<U>& other) {
        *this = SlotRef(other);
        return *this;
    }

    /// ムーブコンストラクタ（参照カウントは変えず、元を空にする）
    SlotRef(SlotRef&& other) noexcept
        : m_control(other.m_control)
        , m_index(other.m_index)
        , m_adjust(other.m_adjust)
    {
        other.m_control = nullptr;
    }

    /// ムーブ代入演算子
    SlotRef& operator=(SlotRef&& other) noexcept {
        if (this != &other) {
            Release();
            m_control = other.m_control;
            m_index = other.m_index;
            m_adjust = other.m_adjust;
            other.m_control = nullptr;
        }
        return *this;
    }

    /// nullptrの代入（参照を手放す）
    SlotRef& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SlotRef() {
        Release();
    }

    /// アロー演算子（位置一覧を1回引く。検証なし）
    T* operator->() { return Resolve(); }

    /// アロー演算子 (const版)
    const T* operator->() const { return Resolve(); }

    /// 間接参照演算子
    T& operator*() { return *Resolve(); }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *Resolve(); }

    /// 要素へのポインタを取得（無効なら nullptr）
    T* Get() { return IsValid() ? Resolve() : nullptr; }

    /// 要素へのポインタを取得（無効なら nullptr、const版）
    const T* Get() const { return IsValid() ? Resolve() : nullptr; }

    /// 参照が有効かどうかを判定
    bool IsValid() const { return m_control != nullptr; }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得
    uint32_t UseCount() const {
        return IsValid() ? m_control->GetRefCountByIndex(m_index) : 0;
    }

    /// 参照を解放
    void Reset() {
        Release();
        m_control = nullptr;
    }

    /// 別のSlotRefと内容を交換
    void Swap(SlotRef& other) noexcept {
        std::swap(m_control, other.m_control);
        std::swap(m_index, other.m_index);
        std::swap(m_adjust, other.m_adjust);
    }

    /// プールの非テンプレート基底を取得
    SlotControlBase* GetControl() const { return m_control; }

    /// スロット番号を取得
    uint32_t GetIndex() const { return m_index; }

    /**
     * @brief 要素の解放時に呼ばれるコールバックを登録する
     *
     * 通知機能のないプール（ObjectSlotSystem）の要素に対しては空の握りを返す。
     *
     * @param callback 解放時に呼ぶ関数
     * @return 購読の握り（破棄で自動解除）
     */
    SubscriptionRef Subscribe(std::function<void()> callback)
    {
        if (!IsValid()) return SubscriptionRef();
        const uint32_t id = m_control->SubscribeByIndex(m_index, std::move(callback));
        if (id == SlotControlBase::INVALID_SUBSCRIPTION_ID) return SubscriptionRef();
        return SubscriptionRef(m_control, m_index, id);
    }

    /// 等価比較（同じプールの同じスロットの同じ場所なら等しい）
    bool operator==(const SlotRef& other) const {
        if (m_control != other.m_control) return false;
        return m_control == nullptr || (m_index == other.m_index && m_adjust == other.m_adjust);
    }

    /// 非等価比較
    bool operator!=(const SlotRef& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（プール、スロット番号、バイト差の順）
    bool operator<(const SlotRef& other) const {
        if (m_control != other.m_control) return m_control < other.m_control;
        if (m_control == nullptr) return false;
        if (m_index != other.m_index) return m_index < other.m_index;
        return m_adjust < other.m_adjust;
    }

    /// 以下比較
    bool operator<=(const SlotRef& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SlotRef& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SlotRef& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const {
        return std::hash<const void*>()(m_control)
            ^ (static_cast<size_t>(m_index) * 0x9E3779B9u)
            ^ (static_cast<size_t>(static_cast<uint32_t>(m_adjust)) * 0x85EBCA6Bu);
    }

private:
    /// プール・スロット番号・バイト差を設定し、参照カウントを増やす
    template<typename U>
    void Bind(SlotControlBase* control, uint32_t index, U* element, T* target) {
        m_control = control;
        m_index = index;
        m_adjust = static_cast<int32_t>(reinterpret_cast<const char*>(target) - reinterpret_cast<const char*>(element));
        AddRef();
    }

    /// 現在の要素のアドレスを求める（検証なし）
    T* Resolve() const {
        char* element = static_cast<char*>(m_control->LocationData()[m_index]);
        return reinterpret_cast<T*>(element + m_adjust);
    }

    /// 参照カウントを増やす
    void AddRef() {
        if (m_control != nullptr) {
            m_control->AddRefByIndex(m_index);
        }
    }

    /// 参照カウントを減らし、0になれば削除する
    void Release() {
        if (m_control != nullptr) {
            m_control->ReleaseRefByIndex(m_index);
        }
    }

    /** 要素が属するプールの非テンプレート基底（無効なら nullptr） */
    SlotControlBase* m_control = nullptr;

    /** スロット番号 */
    uint32_t m_index = 0;

    /** 要素の先頭から指したい場所までのバイト差（基底クラス部分やメンバのオフセット） */
    int32_t m_adjust = 0;
};

template<typename T>
bool operator==(std::nullptr_t, const SlotRef<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SlotRef<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap
template<typename T>
void swap(SlotRef<T>& lhs, SlotRef<T>& rhs) noexcept { lhs.Swap(rhs); }

namespace std {
    template<typename T>
    struct hash<SlotRef<T>> {
        size_t operator()(const SlotRef<T>& r) const {
            return r.HashValue();
        }
    };
}
