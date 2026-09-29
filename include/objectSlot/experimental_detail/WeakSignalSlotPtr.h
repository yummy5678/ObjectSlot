#pragma once

#include "SlotHandle.h"
#include "SlotHeader.h"
#include "SlotStorage.h"
#include "Subscription.h"
#include "SignalSlotPtr.h"

#include <functional>
#include <utility>

template<typename T>
class SignalSlotSystemBase;

class SlotControlBase;

/**
 * @class WeakSignalSlotPtr
 * @brief SignalSlotSystem の要素への8バイトの弱参照ポインタ
 *
 * 【責任】
 * - 所有権を持たずに要素を指し、要素がまだ生きているかを判定する
 * - Lock() で強参照（SignalSlotPtr）に昇格させる
 * - 所有権を持たないまま解放通知を購読する
 *
 * 【仕組み】
 * 圧縮値と、指した時点の世代番号を持つ。要素が削除されると見出しの世代番号が進むため、
 * スロットが再利用されても古い弱参照は無効と判定できる。
 *
 * @tparam T 要素の型
 */
template<typename T>
class WeakSignalSlotPtr
{
public:
    using Storage = SlotStorage<T>;

    /// 空の弱参照を生成
    WeakSignalSlotPtr() = default;

    /// nullptr から空の弱参照を生成
    WeakSignalSlotPtr(std::nullptr_t) {}

    /// 強参照から弱参照を作る
    WeakSignalSlotPtr(const SignalSlotPtr<T>& other)
    {
        if (other.IsValid()) {
            m_value = other.m_value;
            m_generation = other.Header()->generation;
        }
    }

    /// 圧縮値と世代番号から生成する（プール・EnableSlotFromThis 用）
    WeakSignalSlotPtr(uint32_t value, uint32_t generation)
        : m_value(value)
        , m_generation(generation)
    {
    }

    /// ハンドルとタグから生成する（互換用。プールが破棄済みなら空になる）
    WeakSignalSlotPtr(SlotHandle handle, uint8_t tag)
    {
        SignalSlotSystemBase<T>* pool = SignalSlotSystemBase<T>::SignalPoolFromTag(tag);
        if (pool != nullptr && handle.IsValid()) {
            m_value = pool->ValueOf(handle.index);
            m_generation = handle.generation;
        }
    }

    /// 強参照を代入する
    WeakSignalSlotPtr& operator=(const SignalSlotPtr<T>& other)
    {
        *this = WeakSignalSlotPtr(other);
        return *this;
    }

    /// nullptr 代入で空にする
    WeakSignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    // ================================================================
    // 状態
    // ================================================================

    /// 指している要素がまだ生きているか
    bool IsValid() const
    {
        return LiveHeader() != nullptr;
    }

    /// 期限切れか（IsValidの否定）
    bool IsExpired() const { return !IsValid(); }

    /// bool変換（IsValidと同じ）
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得（無効なら0）
    uint32_t UseCount() const
    {
        const SlotHeader* header = LiveHeader();
        return (header != nullptr) ? header->refCount : 0;
    }

    /// 強参照に昇格する（無効なら空のポインタ）
    SignalSlotPtr<T> Lock() const
    {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return SignalSlotPtr<T>();
        ++header->refCount;
        return SignalSlotPtr<T>(m_value);
    }

    /**
     * @brief 所有権を持たないまま解放通知を購読する
     *
     * 従属リソースの自動解放など、「相手が消えたら自分も片付ける」用途に使う。
     *
     * @param callback 解放時に呼ぶ関数
     * @return 購読の握り（無効なら空の握り）
     */
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (!IsValid()) return Subscription<T>();
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool == nullptr) return Subscription<T>();
        const uint32_t id = pool->AddSubscription(Storage::IndexFromValue(m_value), std::move(callback));
        return Subscription<T>(m_value, id);
    }

    /// 空にする
    void Reset()
    {
        m_value = SlotValue::INVALID;
        m_generation = 0;
    }

    /// 中身を入れ替える
    void Swap(WeakSignalSlotPtr& other) noexcept
    {
        std::swap(m_value, other.m_value);
        std::swap(m_generation, other.m_generation);
    }

    /// 指した時点のハンドルを取得（空なら Invalid）
    SlotHandle GetHandle() const
    {
        if (m_value == SlotValue::INVALID) return SlotHandle::Invalid();
        return SlotHandle{ Storage::IndexFromValue(m_value), m_generation };
    }

    /// 所属プールのタグを取得
    uint8_t GetTag() const { return SlotValue::Tag(m_value); }

    /// 圧縮値を取得
    uint32_t GetValue() const { return m_value; }

    /// 所属プールを型消去した形で取得（空、または破棄済みなら nullptr）
    SlotControlBase* GetControl() const
    {
        if (m_value == SlotValue::INVALID) return nullptr;
        return Pool();
    }

    // ================================================================
    // 比較
    // ================================================================

    bool operator==(const WeakSignalSlotPtr& other) const {
        return m_value == other.m_value && m_generation == other.m_generation;
    }
    bool operator!=(const WeakSignalSlotPtr& other) const { return !(*this == other); }
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }
    bool operator<(const WeakSignalSlotPtr& other) const {
        if (m_value != other.m_value) return m_value < other.m_value;
        return m_generation < other.m_generation;
    }
    bool operator<=(const WeakSignalSlotPtr& other) const { return !(other < *this); }
    bool operator>(const WeakSignalSlotPtr& other) const { return other < *this; }
    bool operator>=(const WeakSignalSlotPtr& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const {
        return std::hash<uint64_t>()((static_cast<uint64_t>(m_value) << 32) | m_generation);
    }

private:
    /// 所属プールを取得（破棄済みなら nullptr）
    SignalSlotSystemBase<T>* Pool() const {
        return SignalSlotSystemBase<T>::SignalPoolFromTag(SlotValue::Tag(m_value));
    }

    /**
     * @brief 生きている要素の見出しを取得する
     *
     * プールが破棄済み、添字が範囲外、世代番号が不一致のいずれかなら nullptr。
     *
     * @return 見出し（無効なら nullptr）
     */
    SlotHeader* LiveHeader() const {
        if (m_value == SlotValue::INVALID) return nullptr;
        SignalSlotSystemBase<T>* pool = Pool();
        if (pool == nullptr) return nullptr;
        if (Storage::IndexFromValue(m_value) >= pool->Capacity()) return nullptr;
        SlotHeader* header = Storage::HeaderAt(m_value);
        if (header->generation != m_generation) return nullptr;
        return header;
    }

    /** 圧縮値（無効なら INVALID） */
    uint32_t m_value = SlotValue::INVALID;

    /** 指した時点の世代番号 */
    uint32_t m_generation = 0;
};

template<typename T>
bool operator==(std::nullptr_t, const WeakSignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const WeakSignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap
template<typename T>
void swap(WeakSignalSlotPtr<T>& lhs, WeakSignalSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

namespace std {
    template<typename T>
    struct hash<WeakSignalSlotPtr<T>> {
        size_t operator()(const WeakSignalSlotPtr<T>& p) const {
            return p.HashValue();
        }
    };
}

template<typename T>
WeakSignalSlotPtr<T> SignalSlotPtr<T>::GetWeak() const {
    return WeakSignalSlotPtr<T>(*this);
}
