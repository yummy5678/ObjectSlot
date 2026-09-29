#pragma once

#include "SlotHandle.h"
#include <functional>
#include <utility>

// 前方宣言
template<typename T>
class ObjectSlotSystemBase;

class SlotControlBase;

/**
 * @brief 参照カウント方式のスマートポインタ（軽量版）
 *
 * プールへのポインタとスロット番号を持つ。要素本体へはプールの位置一覧を
 * 1回引いて辿るので、Compact() で本体が動いても有効なまま。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SlotPtr {
    friend class WeakSlotPtr<T>;

    template<typename U>
    friend class SlotRef;

public:
    /// デフォルトコンストラクタ
    SlotPtr() = default;

    /// nullptrからの構築
    SlotPtr(std::nullptr_t) {}

    /// プールとスロット番号を指定して構築（参照カウントは呼び出し側が増やしておくこと）
    SlotPtr(ObjectSlotSystemBase<T>* slot, uint32_t index)
        : m_slot(slot)
        , m_index(index)
    {
    }

    /// コピーコンストラクタ（参照カウントを増やす）
    SlotPtr(const SlotPtr& other)
        : m_slot(other.m_slot)
        , m_index(other.m_index)
    {
        AddRef();
    }

    /// コピー代入演算子
    SlotPtr& operator=(const SlotPtr& other) {
        if (this != &other) {
            Release();
            m_slot = other.m_slot;
            m_index = other.m_index;
            AddRef();
        }
        return *this;
    }

    /// ムーブコンストラクタ（参照カウントは変えず、元を空にする）
    SlotPtr(SlotPtr&& other) noexcept
        : m_slot(other.m_slot)
        , m_index(other.m_index)
    {
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    SlotPtr& operator=(SlotPtr&& other) noexcept {
        if (this != &other) {
            Release();
            m_slot = other.m_slot;
            m_index = other.m_index;
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// nullptrの代入（参照を手放す）
    SlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SlotPtr() {
        Release();
    }

    /// アロー演算子（位置一覧を1回引く。検証なし）
    T* operator->() { return m_slot->Element(m_index); }

    /// アロー演算子 (const版)
    const T* operator->() const { return m_slot->Element(m_index); }

    /// 間接参照演算子
    T& operator*() { return *m_slot->Element(m_index); }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *m_slot->Element(m_index); }

    /// 要素へのポインタを取得（無効なら nullptr）
    T* Get() { return IsValid() ? m_slot->Element(m_index) : nullptr; }

    /// 要素へのポインタを取得（無効なら nullptr、const版）
    const T* Get() const { return IsValid() ? m_slot->Element(m_index) : nullptr; }

    /// 参照が有効かどうかを判定
    bool IsValid() const { return m_slot != nullptr; }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCountByIndex(m_index);
    }

    /// 弱参照を生成
    WeakSlotPtr<T> GetWeak() const;

    /// 別のSlotPtrと内容を交換
    void Swap(SlotPtr& other) noexcept {
        std::swap(m_slot, other.m_slot);
        std::swap(m_index, other.m_index);
    }

    /// 参照を解放
    void Reset() {
        Release();
        m_slot = nullptr;
    }

    /// ハンドルを取得（スロット番号からハンドルを再構築する）
    SlotHandle GetHandle() const {
        if (!IsValid()) return SlotHandle::Invalid();
        return m_slot->HandleFromIndex(m_index);
    }

    /// スロット番号を取得
    uint32_t GetIndex() const { return m_index; }

    /// プールの非テンプレート基底を取得（SlotRef用）
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 等価比較（同じプールの同じスロットなら等しい）
    bool operator==(const SlotPtr& other) const {
        return m_slot == other.m_slot && (m_slot == nullptr || m_index == other.m_index);
    }

    /// 非等価比較
    bool operator!=(const SlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（プール、スロット番号の順）
    bool operator<(const SlotPtr& other) const {
        if (m_slot != other.m_slot) return m_slot < other.m_slot;
        return m_slot != nullptr && m_index < other.m_index;
    }

    /// 以下比較
    bool operator<=(const SlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SlotPtr& other) const { return !(*this < other); }

    /// ハッシュ値を取得
    size_t HashValue() const {
        return std::hash<const void*>()(m_slot) ^ (static_cast<size_t>(m_index) * 0x9E3779B9u);
    }

private:
    /// 参照カウントを増やす
    void AddRef() {
        if (m_slot != nullptr) {
            m_slot->AddRefByIndex(m_index);
        }
    }

    /// 参照を解放する内部処理
    void Release() {
        if (m_slot != nullptr) {
            m_slot->ReleaseRefByIndex(m_index);
        }
    }

    /** 要素が属するプールへのポインタ（無効なら nullptr） */
    ObjectSlotSystemBase<T>* m_slot = nullptr;

    /** スロット番号 */
    uint32_t m_index = 0;
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
