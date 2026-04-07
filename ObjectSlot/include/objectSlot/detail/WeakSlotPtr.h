#pragma once

#include "SlotHandle.h"
#include "SlotPtr.h"

// 前方宣言
template<typename T>
class ObjectSlotSystemBase;

/**
 * @brief 弱参照スマートポインタ（weak_ptr風）
 *
 * オブジェクトプール内の要素への弱い参照を持つ。
 * 参照カウントに影響を与えず、要素の生存確認のみ行う。
 * 要素にアクセスする場合はLock()でSlotPtrに変換する。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class WeakSlotPtr {
public:
    /// デフォルトコンストラクタ
    WeakSlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /// ハンドルとプールポインタを指定して構築
    WeakSlotPtr(SlotHandle handle, ObjectSlotSystemBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /// 参照先が有効かどうかを判定
    bool IsValid() const {
        if (m_slot == nullptr) return false;
        return m_slot->IsValidHandle(m_handle);
    }

    /**
     * @brief 参照先が有効期限切れかどうかを判定
     * @return 無効（削除済み）ならtrue
     */
    bool IsExpired() const {
        return !IsValid();
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照先の参照カウントを取得
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCount(m_handle);
    }

    /**
     * @brief 弱参照からSlotPtrを生成
     *
     * 要素がまだ有効であればSlotPtrを返す。
     * 無効であれば空のSlotPtrを返す。
     *
     * @return 有効な場合はSlotPtr、無効な場合は空のSlotPtr
     */
    SlotPtr<T> Lock() const {
        if (!IsValid()) {
            return SlotPtr<T>();
        }
        m_slot->AddRef(m_handle);
        auto rp = m_slot->GetRootPointer(m_handle.index);
        return SlotPtr<T>(rp, m_slot);
    }

    /// 弱参照をリセット
    void Reset() {
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /// 別のWeakSlotPtrと内容を交換
    void Swap(WeakSlotPtr& other) noexcept {
        std::swap(m_handle, other.m_handle);
        std::swap(m_slot, other.m_slot);
    }

    /// ハンドルを取得
    SlotHandle GetHandle() const { return m_handle; }

    /// 等価比較
    bool operator==(const WeakSlotPtr& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /// 非等価比較
    bool operator!=(const WeakSlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（コンテナのキーとして使用可能にする）
    bool operator<(const WeakSlotPtr& other) const { return m_handle < other.m_handle; }

    /// 以下比較
    bool operator<=(const WeakSlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const WeakSlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const WeakSlotPtr& other) const { return !(*this < other); }

private:
    SlotHandle m_handle;
    ObjectSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const WeakSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const WeakSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/**
 * @brief SlotPtr::GetWeak()の実装
 */
template<typename T>
WeakSlotPtr<T> SlotPtr<T>::GetWeak() const {
    if (!IsValid()) return WeakSlotPtr<T>();
    SlotHandle handle = m_slot->HandleFromIndex(GetIndex());
    return WeakSlotPtr<T>(handle, m_slot);
}

/// ADL用swap関数
template<typename T>
void swap(WeakSlotPtr<T>& lhs, WeakSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化
namespace std {
    template<typename T>
    struct hash<WeakSlotPtr<T>> {
        size_t operator()(const WeakSlotPtr<T>& p) const {
            return hash<SlotHandle>()(p.GetHandle());
        }
    };
}