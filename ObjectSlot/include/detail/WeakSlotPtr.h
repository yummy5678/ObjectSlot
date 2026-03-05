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
    /**
     * @brief デフォルトコンストラクタ
     */
    WeakSlotPtr()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief ハンドルとプールポインタを指定して構築
     */
    WeakSlotPtr(SlotHandle handle, ObjectSlotSystemBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /**
     * @brief 参照先が有効期限切れかどうかを判定
     * @return 無効（削除済み）ならtrue
     */
    bool IsExpired() const {
        if (m_slot == nullptr) return true;
        return !m_slot->IsValidHandle(m_handle);
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
        if (IsExpired()) {
            return SlotPtr<T>();
        }
        // SlotPtrの構築で参照カウントは増加しないため、手動で増加
        m_slot->AddRef(m_handle);
        return SlotPtr<T>(m_handle, m_slot);
    }

    /**
     * @brief 弱参照をリセット
     */
    void Reset() {
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /**
     * @brief ハンドルを取得
     */
    SlotHandle GetHandle() const { return m_handle; }

    /// 等価比較
    bool operator==(const WeakSlotPtr& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /// 非等価比較
    bool operator!=(const WeakSlotPtr& other) const { return !(*this == other); }

private:
    SlotHandle m_handle;
    ObjectSlotSystemBase<T>* m_slot;
};

/**
 * @brief SlotPtr::GetWeak()の実装
 */
template<typename T>
WeakSlotPtr<T> SlotPtr<T>::GetWeak() const {
    return WeakSlotPtr<T>(m_handle, m_slot);
}