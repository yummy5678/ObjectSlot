#pragma once

#include "SlotHandle.h"
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
 * オブジェクトプール内の要素への参照を管理する。
 * コピー時に参照カウントが増加し、破棄時に減少する。
 * 参照カウントが0になると、要素は自動的に削除される。
 *
 * RootVectorによりプールのベースアドレスが固定されるため、
 * 内部に要素への直接ポインタをキャッシュしている。
 * Get()はキャッシュポインタを返すだけのゼロコスト操作。
 *
 * 通知機能が不要な場合に使用する。
 * 通知機能が必要な場合はSignalSlotPtrを使用すること。
 * 基底型で持ち回したい場合はSlotRefに変換すること。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SlotPtr {
    friend class WeakSlotPtr<T>;

    // SlotRefが全てのSlotPtr<U>のprivateメンバにアクセスするため
    template<typename U>
    friend class SlotRef;

public:
    /**
     * @brief デフォルトコンストラクタ
     */
    SlotPtr()
        : m_ptr(nullptr)
        , m_slot(nullptr)
    {
    }

    /**
     * @brief nullptrからの構築
     */
    SlotPtr(std::nullptr_t)
        : m_ptr(nullptr)
        , m_slot(nullptr)
    {
    }

    /**
     * @brief 要素ポインタとプールポインタを指定して構築
     *
     * プール側のCreate()から呼ばれる。
     * ptrはRootVector内の要素を直接指しており、
     * プールが存続する限りアドレスは変わらない。
     *
     * @param ptr 要素への直接ポインタ
     * @param slot 要素が属するプールへのポインタ
     */
    SlotPtr(T* ptr, ObjectSlotSystemBase<T>* slot)
        : m_ptr(ptr)
        , m_slot(slot)
    {
    }

    /**
     * @brief コピーコンストラクタ
     *
     * ポインタをコピーし、参照カウントを増加させる。
     * インデックスはポインタ演算で算出する。
     */
    SlotPtr(const SlotPtr& other)
        : m_ptr(other.m_ptr)
        , m_slot(other.m_slot)
    {
        if (m_ptr != nullptr && m_slot != nullptr) {
            m_slot->AddRefByIndex(GetIndex());
        }
    }

    /**
     * @brief コピー代入演算子
     */
    SlotPtr& operator=(const SlotPtr& other) {
        if (this != &other) {
            Release();
            m_ptr = other.m_ptr;
            m_slot = other.m_slot;
            if (m_ptr != nullptr && m_slot != nullptr) {
                m_slot->AddRefByIndex(GetIndex());
            }
        }
        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     *
     * ポインタの所有権を移転する。
     * 参照カウントは変化しない。
     */
    SlotPtr(SlotPtr&& other) noexcept
        : m_ptr(other.m_ptr)
        , m_slot(other.m_slot)
    {
        other.m_ptr = nullptr;
        other.m_slot = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     */
    SlotPtr& operator=(SlotPtr&& other) noexcept {
        if (this != &other) {
            Release();
            m_ptr = other.m_ptr;
            m_slot = other.m_slot;
            other.m_ptr = nullptr;
            other.m_slot = nullptr;
        }
        return *this;
    }

    /**
     * @brief nullptr代入演算子
     */
    SlotPtr& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /**
     * @brief デストラクタ
     */
    ~SlotPtr() {
        Release();
    }

    /// アロー演算子
    T* operator->() { return m_ptr; }

    /// アロー演算子 (const版)
    const T* operator->() const { return m_ptr; }

    /// 間接参照演算子
    T& operator*() { return *m_ptr; }

    /// 間接参照演算子 (const版)
    const T& operator*() const { return *m_ptr; }

    /**
     * @brief 要素へのポインタを取得
     *
     * 内部のキャッシュポインタをそのまま返す。
     * RootVectorによりアドレスは固定されているため、
     * ハンドル検証や配列アクセスは不要。
     *
     * @return 要素への直接ポインタ。無効な場合はnullptr
     */
    T* Get() { return m_ptr; }

    /**
     * @brief 要素へのポインタを取得 (const版)
     */
    const T* Get() const { return m_ptr; }

    /**
     * @brief 参照が有効かどうかを判定
     *
     * ポインタがnullでなければ有効と見なす。
     * 要素削除時にRemoveInternal内でm_ptrがnullptr化されることはないため、
     * SlotPtrが生きている間は要素も必ず生きている（参照カウント保証）。
     *
     * @return 有効な場合true
     */
    bool IsValid() const {
        return m_ptr != nullptr;
    }

    /**
     * @brief bool変換演算子
     */
    explicit operator bool() const { return IsValid(); }

    /**
     * @brief 参照カウントを取得
     *
     * ポインタ演算でインデックスを算出してからプール側に問い合わせる。
     *
     * @return 現在の参照カウント。無効な場合は0
     */
    uint32_t UseCount() const {
        if (m_ptr == nullptr || m_slot == nullptr) return 0;
        return m_slot->GetRefCount(GetHandle());
    }

    /**
     * @brief 弱参照を生成
     *
     * ポインタ演算でハンドルを再構築してWeakSlotPtrに渡す。
     *
     * @return 弱参照
     */
    WeakSlotPtr<T> GetWeak() const;

    /// 別のSlotPtrと内容を交換
    void Swap(SlotPtr& other) noexcept {
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_slot, other.m_slot);
    }

    /**
     * @brief 参照を解放
     */
    void Reset() {
        Release();
        m_ptr = nullptr;
        m_slot = nullptr;
    }

    /**
     * @brief ハンドルを取得
     *
     * 内部のポインタからインデックスを算出し、
     * プール側の世代番号と組み合わせてハンドルを再構築する。
     * 頻繁に呼ぶ用途には向かない。
     *
     * @return スロットハンドル。無効な場合はSlotHandle::Invalid()
     */
    SlotHandle GetHandle() const {
        if (m_ptr == nullptr || m_slot == nullptr) return SlotHandle::Invalid();
        return m_slot->HandleFromIndex(GetIndex());
    }

    /**
     * @brief プールの非テンプレート基底を取得（SlotRef用）
     */
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /// 等価比較（ポインタアドレスで比較）
    bool operator==(const SlotPtr& other) const {
        return m_ptr == other.m_ptr;
    }

    /// 非等価比較
    bool operator!=(const SlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（ポインタアドレスで比較。コンテナのキーとして使用可能にする）
    bool operator<(const SlotPtr& other) const { return m_ptr < other.m_ptr; }

    /// 以下比較
    bool operator<=(const SlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SlotPtr& other) const { return !(*this < other); }

private:
    /**
     * @brief スロットインデックスをポインタ演算で算出
     *
     * m_ptrとプールの先頭アドレスの差分からインデックスを求める。
     * 参照カウント操作やハンドル構築など、
     * インデックスが必要な場面でのみ呼ばれる。
     *
     * @return スロットインデックス
     */
    uint32_t GetIndex() const {
        return static_cast<uint32_t>(m_ptr - m_slot->DataPtr());
    }

    /**
     * @brief 参照を解放する内部処理
     *
     * ポインタ演算でインデックスを算出し、
     * プール側の参照カウントを減少させる。
     * 参照カウントが0になるとRemoveInternalが呼ばれ要素が削除される。
     */
    void Release() {
        if (m_ptr != nullptr && m_slot != nullptr) {
            m_slot->ReleaseRefByIndex(GetIndex());
        }
    }

    /** 要素への直接ポインタ（Get()はこれを返すだけ） */
    T* m_ptr;

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
// WeakSlotPtr.h内の#include "SlotPtr.h"は#pragma onceで無視される
#include "WeakSlotPtr.h"