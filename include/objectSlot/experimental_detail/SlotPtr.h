#pragma once

#include "SlotHandle.h"
#include "SlotHeader.h"
#include "SlotStorage.h"
#include "ObjectSlotSystemBase.h"

#include <functional>
#include <utility>

template<typename T>
class ObjectSlotSystem;

template<typename T>
class WeakSlotPtr;

template<typename T>
class EnableSlotFromThis;

template<typename T>
class SlotRef;

class SlotControlBase;

/**
 * @class SlotPtr
 * @brief ObjectSlotSystem が管理する要素への4バイトの強参照ポインタ
 *
 * 【責任】
 * - 要素の所有権（参照カウント）を保持し、最後の所有者が消えた時に要素を削除させる
 * - 圧縮値（タグ＋オフセット）から要素と見出しへ辿る
 *
 * 【速度の特性】
 * - operator-> は位置表を1回読んで「基底＋値」を足すだけ（プールには触らない）
 * - コピー・破棄は要素の直前にある見出しの参照カウントを直接増減する。
 *   プール本体に触れるのは参照カウントが0になった時だけ
 *
 * 【注意事項】
 * - 参照カウントは非atomic。スレッド間で共有する場合は外部同期が必要
 * - operator-> と operator* は検証なし。無効なポインタで呼ぶと未定義動作。
 *   検証が必要なら Get() を使う
 * - プールの Clear() 後に残った古いポインタは、コピー・破棄しても何も起こさない
 *
 * @tparam T 要素の型
 */
template<typename T>
class SlotPtr {
    friend class ObjectSlotSystem<T>;
    friend class WeakSlotPtr<T>;
    friend class EnableSlotFromThis<T>;
    template<typename U>
    friend class SlotRef;

public:
    using Storage = SlotStorage<T>;

    /// 空のポインタを生成
    SlotPtr() = default;

    /// nullptr から空のポインタを生成
    SlotPtr(std::nullptr_t) {}

    /// コピー（参照カウントを増やす）
    SlotPtr(const SlotPtr& other)
        : m_value(other.m_value)
    {
        AddRef();
    }

    /// コピー代入（元の参照を手放し、新しい参照を得る）
    SlotPtr& operator=(const SlotPtr& other) {
        if (this != &other) {
            Release();
            m_value = other.m_value;
            AddRef();
        }
        return *this;
    }

    /// ムーブ（参照カウントは変えず、元を空にする）
    SlotPtr(SlotPtr&& other) noexcept
        : m_value(other.m_value)
    {
        other.m_value = SlotValue::INVALID;
    }

    /// ムーブ代入
    SlotPtr& operator=(SlotPtr&& other) noexcept {
        if (this != &other) {
            Release();
            m_value = other.m_value;
            other.m_value = SlotValue::INVALID;
        }
        return *this;
    }

    /// nullptr 代入で参照を手放す
    SlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// 参照を手放す
    ~SlotPtr() {
        Release();
    }

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
    WeakSlotPtr<T> GetWeak() const;

    /// 参照を手放して空にする
    void Reset() {
        Release();
        m_value = SlotValue::INVALID;
    }

    /// 中身を入れ替える
    void Swap(SlotPtr& other) noexcept {
        std::swap(m_value, other.m_value);
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

    // ================================================================
    // 比較
    // ================================================================

    bool operator==(const SlotPtr& other) const { return m_value == other.m_value; }
    bool operator!=(const SlotPtr& other) const { return !(*this == other); }
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }
    bool operator<(const SlotPtr& other) const { return m_value < other.m_value; }
    bool operator<=(const SlotPtr& other) const { return !(other < *this); }
    bool operator>(const SlotPtr& other) const { return other < *this; }
    bool operator>=(const SlotPtr& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const { return std::hash<uint32_t>()(m_value); }

private:
    /// 圧縮値から生成する（参照カウントは呼び出し側が増やしておくこと）
    explicit SlotPtr(uint32_t value)
        : m_value(value)
    {
    }

    /// 見出しを取得（検証なし）
    SlotHeader* Header() const { return Storage::HeaderAt(m_value); }

    /// 要素へのポインタを取得（検証なし。SlotRef の構築用に非constで返す）
    T* Element() const { return Storage::ElementAt(m_value); }


    /// 所属プールを取得（破棄済みなら nullptr）
    ObjectSlotSystemBase<T>* Pool() const {
        return ObjectSlotSystemBase<T>::PoolFromTag(SlotValue::Tag(m_value));
    }

    /**
     * @brief 参照カウントを増やす
     *
     * 参照カウントが0の見出しは「削除済み（Clear後など）」を意味するため、
     * その場合は何もしない。
     */
    void AddRef() {
        if (!IsValid()) return;
        SlotHeader* header = Header();
        if (header->refCount != 0) {
            ++header->refCount;
        }
    }

    /**
     * @brief 参照カウントを減らし、0になれば所属プールに削除を依頼する
     *
     * 削除済みの見出し（参照カウント0）に対しては何もしない。
     */
    void Release() {
        if (!IsValid()) return;
        SlotHeader* header = Header();
        if (header->refCount == 0) return;
        --header->refCount;
        if (header->refCount == 0) {
            ObjectSlotSystemBase<T>* pool = Pool();
            if (pool != nullptr) {
                pool->OnRefCountReachedZeroByValue(m_value);
            }
        }
    }

    /** 圧縮値（タグ4ビット＋オフセット28ビット） */
    uint32_t m_value = SlotValue::INVALID;
};

template<typename T>
bool operator==(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap
template<typename T>
void swap(SlotPtr<T>& lhs, SlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

namespace std {
    template<typename T>
    struct hash<SlotPtr<T>> {
        size_t operator()(const SlotPtr<T>& p) const {
            return p.HashValue();
        }
    };
}

#include "WeakSlotPtr.h"
