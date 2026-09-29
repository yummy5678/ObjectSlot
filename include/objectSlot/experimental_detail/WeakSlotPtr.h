#pragma once

#include "SlotHandle.h"
#include "SlotHeader.h"
#include "SlotStorage.h"
#include "SlotPtr.h"

#include <functional>
#include <utility>

template<typename T>
class ObjectSlotSystemBase;

/**
 * @class WeakSlotPtr
 * @brief ObjectSlotSystem の要素への8バイトの弱参照ポインタ
 *
 * 【責任】
 * - 所有権を持たずに要素を指し、要素がまだ生きているかを判定する
 * - Lock() で強参照（SlotPtr）に昇格させる
 *
 * 【仕組み】
 * 圧縮値と、指した時点の世代番号を持つ。要素が削除されると見出しの世代番号が進むため、
 * スロットが再利用されても古い弱参照は無効と判定できる。
 *
 * 【注意事項】
 * - IsValid() / Lock() はプールの生存と範囲も確認する。
 *   ShrinkToFit で切り詰められた範囲を指す弱参照も安全に無効と判定される
 *
 * @tparam T 要素の型
 */
template<typename T>
class WeakSlotPtr {
public:
    using Storage = SlotStorage<T>;

    /// 空の弱参照を生成
    WeakSlotPtr() = default;

    /// nullptr から空の弱参照を生成
    WeakSlotPtr(std::nullptr_t) {}

    /// 強参照から弱参照を作る
    WeakSlotPtr(const SlotPtr<T>& other) {
        if (other.IsValid()) {
            m_value = other.m_value;
            m_generation = other.Header()->generation;
        }
    }

    /// 圧縮値と世代番号から生成する（プール・EnableSlotFromThis 用）
    WeakSlotPtr(uint32_t value, uint32_t generation)
        : m_value(value)
        , m_generation(generation)
    {
    }

    /// ハンドルとタグから生成する（互換用。プールが破棄済みなら空になる）
    WeakSlotPtr(SlotHandle handle, uint8_t tag) {
        ObjectSlotSystemBase<T>* pool = ObjectSlotSystemBase<T>::PoolFromTag(tag);
        if (pool != nullptr && handle.IsValid()) {
            m_value = pool->ValueOf(handle.index);
            m_generation = handle.generation;
        }
    }

    /// 強参照を代入する
    WeakSlotPtr& operator=(const SlotPtr<T>& other) {
        *this = WeakSlotPtr(other);
        return *this;
    }

    /// nullptr 代入で空にする
    WeakSlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    // ================================================================
    // 状態
    // ================================================================

    /**
     * @brief 指している要素がまだ生きているか
     *
     * プールの生存、添字の範囲、世代番号の一致を順に確認する。
     */
    bool IsValid() const {
        const SlotHeader* header = LiveHeader();
        return header != nullptr;
    }

    /// 期限切れか（IsValidの否定）
    bool IsExpired() const { return !IsValid(); }

    /// bool変換（IsValidと同じ）
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得（無効なら0）
    uint32_t UseCount() const {
        const SlotHeader* header = LiveHeader();
        return (header != nullptr) ? header->refCount : 0;
    }

    /// 強参照に昇格する（無効なら空のポインタ）
    SlotPtr<T> Lock() const {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return SlotPtr<T>();
        ++header->refCount;
        return SlotPtr<T>(m_value);
    }

    /// 空にする
    void Reset() {
        m_value = SlotValue::INVALID;
        m_generation = 0;
    }

    /// 中身を入れ替える
    void Swap(WeakSlotPtr& other) noexcept {
        std::swap(m_value, other.m_value);
        std::swap(m_generation, other.m_generation);
    }

    /// 指した時点のハンドルを取得（空なら Invalid）
    SlotHandle GetHandle() const {
        if (m_value == SlotValue::INVALID) return SlotHandle::Invalid();
        return SlotHandle{ Storage::IndexFromValue(m_value), m_generation };
    }

    /// 所属プールのタグを取得
    uint8_t GetTag() const { return SlotValue::Tag(m_value); }

    /// 圧縮値を取得
    uint32_t GetValue() const { return m_value; }

    // ================================================================
    // 比較
    // ================================================================

    bool operator==(const WeakSlotPtr& other) const {
        return m_value == other.m_value && m_generation == other.m_generation;
    }
    bool operator!=(const WeakSlotPtr& other) const { return !(*this == other); }
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }
    bool operator<(const WeakSlotPtr& other) const {
        if (m_value != other.m_value) return m_value < other.m_value;
        return m_generation < other.m_generation;
    }
    bool operator<=(const WeakSlotPtr& other) const { return !(other < *this); }
    bool operator>(const WeakSlotPtr& other) const { return other < *this; }
    bool operator>=(const WeakSlotPtr& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const {
        return std::hash<uint64_t>()((static_cast<uint64_t>(m_value) << 32) | m_generation);
    }

private:
    /**
     * @brief 生きている要素の見出しを取得する
     *
     * プールが破棄済み、添字が範囲外、世代番号が不一致のいずれかなら nullptr。
     *
     * @return 見出し（無効なら nullptr）
     */
    SlotHeader* LiveHeader() const {
        if (m_value == SlotValue::INVALID) return nullptr;
        ObjectSlotSystemBase<T>* pool = ObjectSlotSystemBase<T>::PoolFromTag(SlotValue::Tag(m_value));
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
bool operator==(std::nullptr_t, const WeakSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const WeakSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

template<typename T>
WeakSlotPtr<T> SlotPtr<T>::GetWeak() const {
    return WeakSlotPtr<T>(*this);
}

/// ADL用swap
template<typename T>
void swap(WeakSlotPtr<T>& lhs, WeakSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

namespace std {
    template<typename T>
    struct hash<WeakSlotPtr<T>> {
        size_t operator()(const WeakSlotPtr<T>& p) const {
            return p.HashValue();
        }
    };
}
