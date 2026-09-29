#pragma once

#include "SlotControlBase.h"
#include "SlotHeader.h"
#include "SlotStorage.h"
#include "SlotPtr.h"
#include "SignalSlotPtr.h"
#include "SubscriptionRef.h"

#include <type_traits>
#include <functional>
#include <utility>

/**
 * @class SlotRef
 * @brief 異なる型のプールの要素を、共通の基底型として扱える強参照
 *
 * 【責任】
 * - 要素の所有権（参照カウント）を保持する
 * - 要素の基底クラス部分、または要素のメンバ（エイリアシング）を指す
 * - 要素の型を知らずに参照カウント操作・解放通知の購読を行う
 *
 * 【仕組み】
 * 要素の見出しへのポインタ、要素の圧縮値、要素の先頭から指したい場所までの
 * バイト差の3つを持つ（16バイト）。
 * 位置表は区画の先頭にあるので、項目のアドレスは「区画の先頭＋番号×4」で要素の型を
 * 知らなくても求まる。区画の先頭は見出しのアドレスの上位ビットから分かる。
 * 項目は動かないので、Compact() で要素本体が動いても追従できる。
 * アクセスは「項目を読む → 基底に足す → バイト差を足す」の順で、SlotPtr と同じ手間。
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
    /// 空の参照を生成
    SlotRef() = default;

    /// nullptr から空の参照を生成
    SlotRef(std::nullptr_t) {}

    /// SlotPtr から生成（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SlotPtr<U>& other) {
        if (other.IsValid()) {
            Bind<U>(other.Header(), other.GetValue(), other.Element(), static_cast<T*>(other.Element()));
        }
    }

    /// SignalSlotPtr から生成（U は T の派生型であること）
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SignalSlotPtr<U>& other) {
        if (other.IsValid()) {
            Bind<U>(other.Header(), other.GetValue(), other.Element(), static_cast<T*>(other.Element()));
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
            Bind<U>(owner.Header(), owner.GetValue(), owner.Element(), aliasPtr);
        }
    }

    /// エイリアシング構築（SignalSlotPtr 版）
    template<typename U>
    SlotRef(const SignalSlotPtr<U>& owner, T* aliasPtr) {
        if (aliasPtr != nullptr && owner.IsValid()) {
            Bind<U>(owner.Header(), owner.GetValue(), owner.Element(), aliasPtr);
        }
    }

    /// コピー（参照カウントを増やす）
    SlotRef(const SlotRef& other)
        : m_header(other.m_header)
        , m_value(other.m_value)
        , m_adjust(other.m_adjust)
    {
        AddRef();
    }

    /// コピー代入
    SlotRef& operator=(const SlotRef& other) {
        if (this != &other) {
            Release();
            CopyFields(other);
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

    /// ムーブ（参照カウントは変えず、元を空にする）
    SlotRef(SlotRef&& other) noexcept {
        CopyFields(other);
        other.ClearFields();
    }

    /// ムーブ代入
    SlotRef& operator=(SlotRef&& other) noexcept {
        if (this != &other) {
            Release();
            CopyFields(other);
            other.ClearFields();
        }
        return *this;
    }

    /// nullptr 代入で参照を手放す
    SlotRef& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// 参照を手放す
    ~SlotRef() {
        Release();
    }

    // ================================================================
    // 要素アクセス
    // ================================================================

    /// 要素へのポインタ（検証なし）
    T* operator->() { return Resolve(); }

    /// 要素へのポインタ（検証なし、const版）
    const T* operator->() const { return Resolve(); }

    /// 要素への参照（検証なし）
    T& operator*() { return *Resolve(); }

    /// 要素への参照（検証なし、const版）
    const T& operator*() const { return *Resolve(); }

    /// 要素へのポインタ（無効なら nullptr）
    T* Get() { return IsValid() ? Resolve() : nullptr; }

    /// 要素へのポインタ（無効なら nullptr、const版）
    const T* Get() const { return IsValid() ? Resolve() : nullptr; }

    // ================================================================
    // 状態
    // ================================================================

    /// 有効な要素を指しているか
    bool IsValid() const { return m_header != nullptr; }

    /// bool変換（IsValidと同じ）
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得（無効なら0）
    uint32_t UseCount() const {
        return (m_header != nullptr) ? m_header->refCount : 0;
    }

    /// 参照を手放して空にする
    void Reset() {
        Release();
        ClearFields();
    }

    /// 中身を入れ替える
    void Swap(SlotRef& other) noexcept {
        SlotRef temporary(std::move(other));
        other.CopyFields(*this);
        this->CopyFields(temporary);
        temporary.ClearFields();
    }

    /// 所属プールを型消去した形で取得（無効、または破棄済みなら nullptr）
    SlotControlBase* GetControl() const {
        if (m_header == nullptr) return nullptr;
        return SlotControlBase::PoolFromId(m_header->poolId);
    }

    /**
     * @brief 要素の解放時に呼ばれるコールバックを登録する
     *
     * 通知機能のないプール（ObjectSlotSystem）の要素に対しては空の握りを返す。
     */
    SubscriptionRef Subscribe(std::function<void()> callback)
    {
        SlotControlBase* pool = GetControl();
        if (pool == nullptr) return SubscriptionRef();

        const uint32_t slotIndex = pool->IndexFromValue(m_header->selfValue);
        const uint32_t id = pool->SubscribeByIndex(slotIndex, std::move(callback));
        if (id == SlotControlBase::INVALID_SUBSCRIPTION_ID) return SubscriptionRef();

        return SubscriptionRef(m_header->poolId, slotIndex, id);
    }

    // ================================================================
    // 比較（同じ要素の同じ場所を指していれば等しい）
    // ================================================================

    bool operator==(const SlotRef& other) const { return m_header == other.m_header && m_adjust == other.m_adjust; }
    bool operator!=(const SlotRef& other) const { return !(*this == other); }
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }
    bool operator<(const SlotRef& other) const {
        if (m_header != other.m_header) return m_header < other.m_header;
        return m_adjust < other.m_adjust;
    }
    bool operator<=(const SlotRef& other) const { return !(other < *this); }
    bool operator>(const SlotRef& other) const { return other < *this; }
    bool operator>=(const SlotRef& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const {
        return std::hash<const void*>()(m_header) ^ (static_cast<size_t>(static_cast<uint32_t>(m_adjust)) * 0x9E3779B9u);
    }

private:
    /// 要素の見出し・圧縮値・バイト差を設定し、参照カウントを増やす
    template<typename U>
    void Bind(SlotHeader* header, uint32_t value, U* element, T* target) {
        m_header = header;
        m_value = value;
        m_adjust = static_cast<int32_t>(reinterpret_cast<const char*>(target) - reinterpret_cast<const char*>(element));
        AddRef();
    }

    /// 現在の要素のアドレスを求める（検証なし）
    T* Resolve() const {
        // 見出しは本体と同じ区画にあり、区画は256MB境界に揃っているので
        // 見出しのアドレスから区画の先頭が分かる。位置表は区画の先頭にある
        const uintptr_t regionBase = reinterpret_cast<uintptr_t>(m_header) & ~static_cast<uintptr_t>(SlotValue::REGION_BYTES - 1);
        const uint32_t* entry = reinterpret_cast<const uint32_t*>(regionBase + static_cast<size_t>(SlotValue::Offset(m_value)) * sizeof(uint32_t));
        unsigned char* body = reinterpret_cast<unsigned char*>(regionBase + SlotValue::Offset(*entry));
        return reinterpret_cast<T*>(body + m_adjust);
    }

    /// 他の SlotRef から中身を写す（参照カウントは触らない）
    void CopyFields(const SlotRef& other) {
        m_header = other.m_header;
        m_value = other.m_value;
        m_adjust = other.m_adjust;
    }

    /// 空の状態にする（参照カウントは触らない）
    void ClearFields() {
        m_header = nullptr;
        m_value = SlotValue::INVALID;
        m_adjust = 0;
    }

    /// 参照カウントを増やす（削除済みの見出しには何もしない）
    void AddRef() {
        if (m_header == nullptr) return;
        if (m_header->refCount != 0) {
            ++m_header->refCount;
        }
    }

    /// 参照カウントを減らし、0になれば所属プールに削除を依頼する
    void Release() {
        if (m_header == nullptr) return;
        if (m_header->refCount == 0) return;
        --m_header->refCount;
        if (m_header->refCount == 0) {
            SlotControlBase* pool = SlotControlBase::PoolFromId(m_header->poolId);
            if (pool != nullptr) {
                pool->OnRefCountReachedZero(m_header);
            }
        }
    }

    /** 要素の見出しへのポインタ */
    SlotHeader* m_header = nullptr;

    /** 要素の圧縮値（位置表の項目を求めるのに使う） */
    uint32_t m_value = SlotValue::INVALID;

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
