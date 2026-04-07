#pragma once

#include "SlotHandle.h"
#include "thirdparty/rootVector/RootVector.h"
#include <functional>

// 前方宣言
template<typename T>
class ObjectSlotSystemBase;

template<typename T>
class WeakSlotPtr;

class SlotControlBase;

/**
 * @brief 参照カウント方式のスマートポインタ（軽量版）
 *
 * root_pointerを内部に持つことで、全環境でGet()のコストを最小化する。
 * ネイティブ環境ではGet()はゼロコスト（生ポインタ返却）、
 * フォールバック環境ではインデックス経由で安全にアクセスする。
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
    SlotPtr()
        : m_root_ptr()
        , m_slot(nullptr)
    {
    }

    /// nullptrからの構築
    SlotPtr(std::nullptr_t)
        : m_root_ptr()
        , m_slot(nullptr)
    {
    }

    /// root_pointerとプールポインタを指定して構築
    SlotPtr(typename root_vector<T>::root_pointer ptr, ObjectSlotSystemBase<T>* slot)
        : m_root_ptr(ptr)
        , m_slot(slot)
    {
    }

    /// コピーコンストラクタ
    SlotPtr(const SlotPtr& other)
        : m_root_ptr(other.m_root_ptr)
        , m_slot(other.m_slot)
    {
        if (m_root_ptr && m_slot != nullptr) {
            m_slot->AddRefByIndex(GetIndex());
        }
    }

    /// コピー代入演算子
    SlotPtr& operator=(const SlotPtr& other) {
        if (this != &other) {
            Release();
            m_root_ptr = other.m_root_ptr;
            m_slot = other.m_slot;
            if (m_root_ptr && m_slot != nullptr) {
                m_slot->AddRefByIndex(GetIndex());
            }
        }
        return *this;
    }

    /// ムーブコンストラクタ
    SlotPtr(SlotPtr&& other) noexcept
        : m_root_ptr(other.m_root_ptr)
        , m_slot(other.m_slot)
    {
        other.m_root_ptr.reset();
        other.m_slot = nullptr;
    }

    /// ムーブ代入演算子
    SlotPtr& operator=(SlotPtr&& other) noexcept {
        if (this != &other) {
            Release();
            m_root_ptr = other.m_root_ptr;
            m_slot = other.m_slot;
            other.m_root_ptr.reset();
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// nullptr代入演算子
    SlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SlotPtr() {
        Release();
    }

    /// アロー演算子（ゼロコスト）
    T* operator->() { return m_root_ptr.get(); }

    /// アロー演算子 (const版)
    const T* operator->() const { return m_root_ptr.get(); }

    /// 間接参照演算子
    T& operator*() { return *m_root_ptr; }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *m_root_ptr; }

    /// 要素へのポインタを取得（ゼロコスト）
    T* Get() { return m_root_ptr.get(); }

    /// 要素へのポインタを取得（ゼロコスト、const版）
    const T* Get() const { return m_root_ptr.get(); }

    /// 参照が有効かどうかを判定
    bool IsValid() const {
        return static_cast<bool>(m_root_ptr) && m_slot != nullptr;
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照カウントを取得
    uint32_t UseCount() const {
        if (!IsValid()) return 0;
        return m_slot->GetRefCountByIndex(GetIndex());
    }

    /// 弱参照を生成
    WeakSlotPtr<T> GetWeak() const;

    /// 別のSlotPtrと内容を交換
    void Swap(SlotPtr& other) noexcept {
        std::swap(m_root_ptr, other.m_root_ptr);
        std::swap(m_slot, other.m_slot);
    }

    /// 参照を解放
    void Reset() {
        Release();
        m_root_ptr.reset();
        m_slot = nullptr;
    }

    /// ハンドルを取得（インデックスからハンドルを再構築する）
    SlotHandle GetHandle() const {
        if (!IsValid()) return SlotHandle::Invalid();
        return m_slot->HandleFromIndex(GetIndex());
    }

    /// プールの非テンプレート基底を取得（SlotRef用）
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 等価比較（ポインタアドレスで比較）
    bool operator==(const SlotPtr& other) const {
        return m_root_ptr.get() == other.m_root_ptr.get();
    }

    /// 非等価比較
    bool operator!=(const SlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（ポインタアドレスで比較）
    bool operator<(const SlotPtr& other) const { return m_root_ptr.get() < other.m_root_ptr.get(); }

    /// 以下比較
    bool operator<=(const SlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SlotPtr& other) const { return !(*this < other); }

private:
    /// スロットインデックスをポインタ演算で算出
    uint32_t GetIndex() const {
        return static_cast<uint32_t>(m_root_ptr.get() - m_slot->DataPtr());
    }

    /// 参照を解放する内部処理
    void Release() {
        if (m_root_ptr && m_slot != nullptr) {
            m_slot->ReleaseRefByIndex(GetIndex());
        }
    }

    /** 要素への安定ポインタ（全環境でGet()を最適化する） */
    typename root_vector<T>::root_pointer m_root_ptr;

    /** 要素が属するプールへのポインタ */
    ObjectSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap関数
template<typename T>
void swap(SlotPtr<T>& lhs, SlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化（ポインタアドレスのハッシュを使用）
namespace std {
    template<typename T>
    struct hash<SlotPtr<T>> {
        size_t operator()(const SlotPtr<T>& p) const {
            return hash<const T*>()(p.Get());
        }
    };
}

// SlotPtr定義完了後にWeakSlotPtrをインクルード
#include "WeakSlotPtr.h"