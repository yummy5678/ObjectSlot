#pragma once

#include "SlotControlBase.h"
#include "SlotPtr.h"
#include "SubscriptionRef.h"
#include <type_traits>
#include <algorithm>
#include <functional>

// 前方宣言
template<typename T>
class SignalSlotPtr;

/**
 * @brief ポリモーフィック対応の参照カウント付きスマートポインタ
 *
 * 基底型のインターフェースとして要素を参照できる。
 * 内部にキャッシュした生ポインタを使うため、
 * Get()のアクセスコストはゼロ。
 *
 * コストが発生するのは生成・破棄・プール再アロケーション時のみ。
 *
 * エイリアシングコンストラクタにより、所有権を共有しつつ
 * メンバ変数等の別のオブジェクトを指すことも可能。
 *
 * 主な用途:
 * - SlotPtr<Derived>をSlotRef<Base>として統一的に扱う
 * - 異なる具体型のオブジェクトを基底インターフェースで管理する
 * - 所有権を共有しつつメンバ変数を直接参照する（エイリアシング）
 *
 * @tparam T 参照先の型（基底型を含む）
 */
template<typename T>
class SlotRef {
public:
    /// デフォルトコンストラクタ
    SlotRef()
        : m_ptr(nullptr)
        , m_control(nullptr)
    {
    }

    /// nullptrからの構築
    SlotRef(std::nullptr_t)
        : m_ptr(nullptr)
        , m_control(nullptr)
    {
    }

    /**
     * @brief SlotPtrからの変換コンストラクタ
     *
     * SlotPtr<U>からSlotRef<T>への変換を行う。
     * UがTの派生クラスである場合のみコンパイル可能。
     *
     * @tparam U 元のSlotPtrの要素型（Tの派生型）
     * @param other 変換元のSlotPtr
     */
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SlotPtr<U>& other)
        : m_ptr(nullptr)
        , m_control(nullptr)
    {
        if (other.IsValid()) {
            U* rawPtr = const_cast<U*>(other.Get());
            m_ptr = static_cast<T*>(rawPtr);
            m_control = other.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }
    }

    /**
     * @brief SignalSlotPtrからの変換コンストラクタ
     *
     * SignalSlotPtr<U>からSlotRef<T>への変換を行う。
     * UがTの派生クラスである場合のみコンパイル可能。
     *
     * @tparam U 元のSignalSlotPtrの要素型（Tの派生型）
     * @param other 変換元のSignalSlotPtr
     */
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef(const SignalSlotPtr<U>& other)
        : m_ptr(nullptr)
        , m_control(nullptr)
    {
        if (other.IsValid()) {
            U* rawPtr = const_cast<U*>(other.Get());
            m_ptr = static_cast<T*>(rawPtr);
            m_control = other.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }
    }

    /**
     * @brief SlotPtrからのエイリアシングコンストラクタ
     *
     * ownerの所有権（参照カウント）を共有しつつ、
     * aliasPtrが指す別のオブジェクト（メンバ変数等）を参照する。
     *
     * aliasPtrはownerが管理する要素のメンバや
     * 関連オブジェクトを想定している。
     * ownerの参照カウントが0になるとaliasPtrも無効になるため、
     * aliasPtrの生存期間がownerに依存することを保証すること。
     *
     * 注意: aliasPtrはプール内の連続メモリ上にあるとは限らないため、
     * プール再アロケーション時にポインタが自動更新されない場合がある。
     * 要素の追加・削除で再アロケーションが発生しない状況で使用すること。
     *
     * @tparam U 元のSlotPtrの要素型
     * @param owner 所有権を共有するSlotPtr
     * @param aliasPtr 実際に参照するポインタ
     */
    template<typename U>
    SlotRef(const SlotPtr<U>& owner, T* aliasPtr)
        : m_ptr(aliasPtr)
        , m_control(nullptr)
    {
        if (aliasPtr != nullptr && owner.IsValid()) {
            U* rawPtr = const_cast<U*>(owner.Get());
            m_control = owner.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }
    }

    /**
     * @brief SignalSlotPtrからのエイリアシングコンストラクタ
     *
     * ownerの所有権（参照カウント）を共有しつつ、
     * aliasPtrが指す別のオブジェクトを参照する。
     * 詳細はSlotPtr版のエイリアシングコンストラクタを参照。
     *
     * @tparam U 元のSignalSlotPtrの要素型
     * @param owner 所有権を共有するSignalSlotPtr
     * @param aliasPtr 実際に参照するポインタ
     */
    template<typename U>
    SlotRef(const SignalSlotPtr<U>& owner, T* aliasPtr)
        : m_ptr(aliasPtr)
        , m_control(nullptr)
    {
        if (aliasPtr != nullptr && owner.IsValid()) {
            U* rawPtr = const_cast<U*>(owner.Get());
            m_control = owner.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }
    }

    /**
     * @brief コピーコンストラクタ
     *
     * プール側の登録情報からスロットインデックスを取得する。
     * RefSlotSystem以外のプールでは登録情報がないため、
     * ポインタ演算にフォールバックする。
     */
    SlotRef(const SlotRef& other)
        : m_ptr(other.m_ptr)
        , m_control(other.m_control)
    {
        if (m_ptr != nullptr && m_control != nullptr) {
            uint32_t index = ResolveIndex(&other.m_ptr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }
    }

    /**
     * @brief コピー代入演算子
     */
    SlotRef& operator=(const SlotRef& other) {
        if (this != &other) {
            Release();

            m_ptr = other.m_ptr;
            m_control = other.m_control;

            if (m_ptr != nullptr && m_control != nullptr) {
                uint32_t index = ResolveIndex(&other.m_ptr);
                m_control->AddRefByIndex(index);
                m_control->RegisterRef(
                    reinterpret_cast<void**>(&m_ptr), index);
            }
        }
        return *this;
    }

    /**
     * @brief SlotPtrからの変換代入演算子
     */
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef& operator=(const SlotPtr<U>& other) {
        Release();

        if (other.IsValid()) {
            U* rawPtr = const_cast<U*>(other.Get());
            m_ptr = static_cast<T*>(rawPtr);
            m_control = other.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }

        return *this;
    }

    /**
     * @brief SignalSlotPtrからの変換代入演算子
     */
    template<typename U, std::enable_if_t<std::is_base_of_v<T, U>, int> = 0>
    SlotRef& operator=(const SignalSlotPtr<U>& other) {
        Release();

        if (other.IsValid()) {
            U* rawPtr = const_cast<U*>(other.Get());
            m_ptr = static_cast<T*>(rawPtr);
            m_control = other.GetControl();

            uint32_t index = m_control->IndexFromRawPtr(rawPtr);
            m_control->AddRefByIndex(index);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }

        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     *
     * プール側の登録を解除して得たインデックスで
     * 新しいポインタを再登録する。参照カウントは変化しない。
     */
    SlotRef(SlotRef&& other) noexcept
        : m_ptr(other.m_ptr)
        , m_control(other.m_control)
    {
        if (m_ptr != nullptr && m_control != nullptr) {
            uint32_t index = UnregisterWithFallback(
                other.m_control, &other.m_ptr, other.m_ptr);
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), index);
        }

        other.m_ptr = nullptr;
        other.m_control = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     */
    SlotRef& operator=(SlotRef&& other) noexcept {
        if (this != &other) {
            Release();

            m_ptr = other.m_ptr;
            m_control = other.m_control;

            if (m_ptr != nullptr && m_control != nullptr) {
                uint32_t index = UnregisterWithFallback(
                    other.m_control, &other.m_ptr, other.m_ptr);
                m_control->RegisterRef(
                    reinterpret_cast<void**>(&m_ptr), index);
            }

            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }
        return *this;
    }

    /// nullptr代入演算子
    SlotRef& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /// デストラクタ
    ~SlotRef() {
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

    /// 要素へのポインタを取得
    T* Get() { return m_ptr; }

    /// 要素へのポインタを取得 (const版)
    const T* Get() const { return m_ptr; }

    /// 参照が有効かどうかを判定
    bool IsValid() const {
        return m_ptr != nullptr;
    }

    /// bool変換演算子
    explicit operator bool() const { return IsValid(); }

    /// 参照を解放
    void Reset() {
        Release();
        m_ptr = nullptr;
        m_control = nullptr;
    }

    /**
     * @brief 解放通知の購読を登録
     *
     * この要素が解放される時に実行されるコールバックを登録する。
     * 返されるSubscriptionRefオブジェクトが破棄されると購読は自動解除される。
     *
     * RefSlotSystem使用時のみ動作する。
     * ObjectSlotSystem/SignalSlotSystemではプール側に登録情報がないため、
     * 空のSubscriptionRefが返る。
     *
     * @param callback 解放時に実行する関数
     * @return 購読オブジェクト（購読者側で保持すること）
     */
    SubscriptionRef Subscribe(std::function<void()> callback)
    {
        if (m_ptr == nullptr || m_control == nullptr) {
            return SubscriptionRef();
        }
    
        auto result = m_control->SubscribeByRef(
            reinterpret_cast<void**>(&m_ptr), std::move(callback));
        
        if (result.slotIndex == SlotHandle::INVALID_INDEX) {
            return SubscriptionRef();
        }
    
        return SubscriptionRef(m_control, result.slotIndex, result.subscriptionId);
    }

    /**
     * @brief 別のSlotRefと内容を交換
     *
     * 双方のプール登録を解除してインデックスを取得し、
     * ポインタ交換後に新しいアドレスで再登録する。
     */
    void Swap(SlotRef& other) noexcept {
        if (this == &other) return;

        uint32_t thisIndex = SlotHandle::INVALID_INDEX;
        uint32_t otherIndex = SlotHandle::INVALID_INDEX;

        if (m_ptr != nullptr && m_control != nullptr) {
            thisIndex = UnregisterWithFallback(
                m_control, &m_ptr, m_ptr);
        }
        if (other.m_ptr != nullptr && other.m_control != nullptr) {
            otherIndex = UnregisterWithFallback(
                other.m_control, &other.m_ptr, other.m_ptr);
        }

        std::swap(m_ptr, other.m_ptr);
        std::swap(m_control, other.m_control);

        if (m_ptr != nullptr && m_control != nullptr) {
            m_control->RegisterRef(
                reinterpret_cast<void**>(&m_ptr), otherIndex);
        }
        if (other.m_ptr != nullptr && other.m_control != nullptr) {
            other.m_control->RegisterRef(
                reinterpret_cast<void**>(&other.m_ptr), thisIndex);
        }
    }

    /// 等価比較
    bool operator==(const SlotRef& other) const {
        return m_ptr == other.m_ptr;
    }

    /// 非等価比較
    bool operator!=(const SlotRef& other) const { return !(*this == other); }

    /// nullptrとの等価比較
    bool operator==(std::nullptr_t) const noexcept { return !IsValid(); }

    /// nullptrとの非等価比較
    bool operator!=(std::nullptr_t) const noexcept { return IsValid(); }

    /// 小なり比較（生ポインタのアドレス順。コンテナのキーとして使用可能にする）
    bool operator<(const SlotRef& other) const { return m_ptr < other.m_ptr; }
    
    /// 以下比較
    bool operator<=(const SlotRef& other) const { return !(other < *this); }
    
    /// 大なり比較
    bool operator>(const SlotRef& other) const { return other < *this; }
    
    /// 以上比較
    bool operator>=(const SlotRef& other) const { return !(*this < other); }

private:
    /**
     * @brief コピー元のポインタ位置からスロットインデックスを解決する
     *
     * RefSlotSystemBaseのFindIndexByRefで登録情報から検索し、
     * 見つからない場合はIndexFromRawPtrでポインタ演算にフォールバックする。
     *
     * RefSlotSystem以外のプール（ObjectSlotSystem, SignalSlotSystem）では
     * 登録情報が存在しないため、常にフォールバック経路を使用する。
     * エイリアシングSlotRefはRefSlotSystemでのみ使用するため、
     * フォールバック経路でポインタ演算しても安全である。
     *
     * @param otherPtrAddr コピー元のm_ptrのアドレス
     * @return スロットインデックス
     */
    uint32_t ResolveIndex(const T* const* otherPtrAddr) const {
        uint32_t index = m_control->FindIndexByRef(otherPtrAddr);
        if (index == SlotHandle::INVALID_INDEX) {
            index = m_control->IndexFromRawPtr(const_cast<T*>(*otherPtrAddr));
        }
        return index;
    }

    /**
     * @brief プール登録を解除し、スロットインデックスを返す
     *
     * UnregisterRefで登録解除を試み、登録情報がない場合は
     * ポインタ演算でインデックスを算出するフォールバック。
     * 解除判定とインデックス算出を一括で行うヘルパー。
     *
     * @param control 対象のプール制御ブロック
     * @param ptrAddr 登録解除するm_ptrのアドレス
     * @param ptr フォールバック用の要素ポインタ
     * @return スロットインデックス
     */
    static uint32_t UnregisterWithFallback(
        SlotControlBase* control, T** ptrAddr, T* ptr)
    {
        uint32_t index = control->UnregisterRef(
            reinterpret_cast<void**>(ptrAddr));
        if (index == SlotHandle::INVALID_INDEX) {
            index = control->IndexFromRawPtr(const_cast<T*>(ptr));
        }
        return index;
    }

    /**
     * @brief 参照を解放する内部処理
     *
     * プール側の登録解除でスロットインデックスを取得し、
     * そのインデックスの参照カウントを減少させる。
     * RefSlotSystem以外のプールでは登録情報がないため、
     * ポインタ演算にフォールバックしてインデックスを算出する。
     */
    void Release() {
        if (m_ptr != nullptr && m_control != nullptr) {
            uint32_t index = UnregisterWithFallback(
                m_control, &m_ptr, m_ptr);
            m_control->ReleaseRefByIndex(index);
        }
    }

    /** 要素への直接ポインタ（Get()はこれを返すだけ） */
    T* m_ptr;

    /** プールの非テンプレート基底へのポインタ */
    SlotControlBase* m_control;
};

template<typename T>
bool operator==(std::nullptr_t, const SlotRef<T>& rhs) noexcept { return rhs == nullptr; }

template<typename T>
bool operator!=(std::nullptr_t, const SlotRef<T>& rhs) noexcept { return rhs != nullptr; }

/// ADL用swap関数
template<typename T>
void swap(SlotRef<T>& lhs, SlotRef<T>& rhs) noexcept { lhs.Swap(rhs); }

/// std::hashの特殊化（生ポインタのハッシュを使用）
namespace std {
    template<typename T>
    struct hash<SlotRef<T>> {
        size_t operator()(const SlotRef<T>& r) const {
            return hash<const T*>()(r.Get());
        }
    };
}