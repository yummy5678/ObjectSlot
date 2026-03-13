#pragma once

#include <functional>
#include "SlotHandle.h"
#include "Subscription.h"

// 前方宣言
template<typename T>
class SignalSlotSystemBase;

template<typename T>
class WeakSignalSlotPtr;

class SlotControlBase;

/**
 * @brief 通知機能付き参照カウント方式スマートポインタ
 *
 * SignalSlotSystemBase内の要素への参照を管理し、
 * 購読パターンによる解放通知機能を持つ。
 *
 * RootVectorによりプールのベースアドレスが固定されるため、
 * 内部に要素への直接ポインタをキャッシュしている。
 * Get()はキャッシュポインタを返すだけのゼロコスト操作。
 *
 * Subscribe()で登録されたコールバックは、
 * この要素の参照カウントが0になり解放される時に実行される。
 * 購読者はSubscribe()が返すSubscriptionオブジェクトを保持し、
 * Subscriptionの破棄で購読が自動解除される。
 *
 * 主な用途:
 * - 解放順序の制御が必要なリソースの管理
 *
 * 基底型で持ち回したい場合はSlotRefに変換すること。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SignalSlotPtr
{
    // SlotRefが全てのSignalSlotPtr<U>のprivateメンバにアクセスするため
    template<typename U>
    friend class SlotRef;

public:
    /// デフォルトコンストラクタ
    SignalSlotPtr()
        : m_ptr(nullptr)
        , m_slot(nullptr)
    {
    }

    /// nullptrからの構築
    SignalSlotPtr(std::nullptr_t)
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
     * @param slot 要素が属する通知機能付きプールへのポインタ
     */
    SignalSlotPtr(T* ptr, SignalSlotSystemBase<T>* slot)
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
    SignalSlotPtr(const SignalSlotPtr& other)
        : m_ptr(other.m_ptr)
        , m_slot(other.m_slot)
    {
        if (m_ptr != nullptr && m_slot != nullptr)
            m_slot->AddRefByIndex(GetIndex());
    }

    /**
     * @brief コピー代入演算子
     */
    SignalSlotPtr& operator=(const SignalSlotPtr& other)
    {
        if (this != &other) {
            Release();
            m_ptr = other.m_ptr;
            m_slot = other.m_slot;
            if (m_ptr != nullptr && m_slot != nullptr)
                m_slot->AddRefByIndex(GetIndex());
        }
        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     *
     * ポインタの所有権を移転する。
     * 参照カウントは変化しない。
     */
    SignalSlotPtr(SignalSlotPtr&& other) noexcept
        : m_ptr(other.m_ptr)
        , m_slot(other.m_slot)
    {
        other.m_ptr = nullptr;
        other.m_slot = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     */
    SignalSlotPtr& operator=(SignalSlotPtr&& other) noexcept
    {
        if (this != &other) {
            Release();
            m_ptr = other.m_ptr;
            m_slot = other.m_slot;
            other.m_ptr = nullptr;
            other.m_slot = nullptr;
        }
        return *this;
    }

    /// nullptr代入演算子
    SignalSlotPtr& operator=(std::nullptr_t) noexcept
    {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SignalSlotPtr() { Release(); }

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
     * @brief 弱参照を生成
     *
     * @return 弱参照
     */
    WeakSignalSlotPtr<T> GetWeak() const;

    /// 別のSignalSlotPtrと内容を交換
    void Swap(SignalSlotPtr& other) noexcept {
        std::swap(m_ptr, other.m_ptr);
        std::swap(m_slot, other.m_slot);
    }

    /**
     * @brief 参照が有効かどうかを判定
     *
     * ポインタがnullでなければ有効と見なす。
     * SlotPtrが生きている間は要素も必ず生きている（参照カウント保証）。
     *
     * @return 有効な場合true
     */
    bool IsValid() const {
        return m_ptr != nullptr;
    }

    /// bool変換演算子
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

    /// 参照を解放
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

    /// プールの非テンプレート基底を取得（SlotRef用）
    SlotControlBase* GetControl() const {
        return static_cast<SlotControlBase*>(m_slot);
    }

    /**
     * @brief 解放通知の購読を登録
     *
     * この要素の参照カウントが0になり解放される時に
     * 実行されるコールバックを登録する。
     * 返されるSubscriptionオブジェクトが破棄されると購読は自動解除される。
     *
     * ポインタ演算でスロットインデックスを算出し、
     * プール側のAddSubscriptionに委譲する。
     *
     * @param callback 解放時に実行する関数
     * @return 購読オブジェクト（購読者側で保持すること）
     */
    Subscription<T> Subscribe(std::function<void()> callback)
    {
        if (m_ptr == nullptr || m_slot == nullptr)
            return Subscription<T>();

        uint32_t index = GetIndex();
        uint32_t id = m_slot->AddSubscription(index, std::move(callback));
        return Subscription<T>(m_slot, index, id);
    }

    /// 等価比較（ポインタアドレスで比較）
    bool operator==(const SignalSlotPtr& other) const {
        return m_ptr == other.m_ptr;
    }

    /// 非等価比較
    bool operator!=(const SignalSlotPtr& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（ポインタアドレスで比較。コンテナのキーとして使用可能にする）
    bool operator<(const SignalSlotPtr& other) const { return m_ptr < other.m_ptr; }

    /// 以下比較
    bool operator<=(const SignalSlotPtr& other) const { return !(other < *this); }

    /// 大なり比較
    bool operator>(const SignalSlotPtr& other) const { return other < *this; }

    /// 以上比較
    bool operator>=(const SignalSlotPtr& other) const { return !(*this < other); }


private:
    /**
     * @brief スロットインデックスをポインタ演算で算出
     *
     * m_ptrとプールの先頭アドレスの差分からインデックスを求める。
     * 参照カウント操作、Subscribe、ハンドル構築など、
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
     * 参照カウントが0になるとNotifySubscribers → RemoveInternalの順に実行される。
     */
    void Release() {
        if (m_ptr != nullptr && m_slot != nullptr)
            m_slot->ReleaseRefByIndex(GetIndex());
    }

    /** 要素への直接ポインタ（Get()はこれを返すだけ） */
    T* m_ptr;

    /** 要素が属する通知機能付きプールへのポインタ */
    SignalSlotSystemBase<T>* m_slot;
};

template<typename T>
bool operator==(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SignalSlotPtr<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap関数
template<typename T>
void swap(SignalSlotPtr<T>& lhs, SignalSlotPtr<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化（ポインタアドレスのハッシュを使用）
namespace std {
    template<typename T>
    struct hash<SignalSlotPtr<T>> {
        size_t operator()(const SignalSlotPtr<T>& p) const {
            return hash<const T*>()(p.Get());
        }
    };
}


// SignalSlotPtr定義完了後にWeakSignalSlotPtrをインクルード
// WeakSignalSlotPtr.h内の#include "SignalSlotPtr.h"は#pragma onceで無視される
#include "WeakSignalSlotPtr.h"