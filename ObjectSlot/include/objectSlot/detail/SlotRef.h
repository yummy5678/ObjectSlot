#pragma once

#include "SlotControlBase.h"
#include "SlotPtr.h"
#include <type_traits>

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
 * 主な用途:
 * - SlotPtr<VBuffer>をSlotRef<IVBuffer>として統一的に扱う
 * - 異なる具体型のバッファを基底インターフェースで管理する
 *
 * @tparam T 参照先の型（基底型を含む）
 */
template<typename T>
class SlotRef {
public:
    /**
     * @brief デフォルトコンストラクタ
     */
    SlotRef()
        : m_ptr(nullptr)
        , m_control(nullptr)
    {
    }

    /**
     * @brief nullptrからの構築
     */
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
     * @brief コピーコンストラクタ
     */
    SlotRef(const SlotRef& other)
        : m_ptr(other.m_ptr)
        , m_control(other.m_control)
    {
        if (m_ptr != nullptr && m_control != nullptr) {
            uint32_t index = m_control->IndexFromRawPtr(
                const_cast<T*>(m_ptr));
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
                uint32_t index = m_control->IndexFromRawPtr(
                    const_cast<T*>(m_ptr));
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
     */
    SlotRef(SlotRef&& other) noexcept
        : m_ptr(other.m_ptr)
        , m_control(other.m_control)
    {
        if (m_ptr != nullptr && m_control != nullptr) {
            m_control->UnregisterRef(
                reinterpret_cast<void**>(&other.m_ptr));
            uint32_t index = m_control->IndexFromRawPtr(
                const_cast<T*>(m_ptr));
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
                m_control->UnregisterRef(
                    reinterpret_cast<void**>(&other.m_ptr));
                uint32_t index = m_control->IndexFromRawPtr(
                    const_cast<T*>(m_ptr));
                m_control->RegisterRef(
                    reinterpret_cast<void**>(&m_ptr), index);
            }

            other.m_ptr = nullptr;
            other.m_control = nullptr;
        }
        return *this;
    }

    /**
     * @brief nullptr代入演算子
     */
    SlotRef& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /**
     * @brief デストラクタ
     */
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

    /**
     * @brief 要素へのポインタを取得
     * @return 有効な場合は要素へのポインタ、無効な場合はnullptr
     */
    T* Get() { return m_ptr; }

    /**
     * @brief 要素へのポインタを取得 (const版)
     */
    const T* Get() const { return m_ptr; }

    /**
     * @brief 参照が有効かどうかを判定
     */
    bool IsValid() const {
        return m_ptr != nullptr;
    }

    /**
     * @brief bool変換演算子
     */
    explicit operator bool() const { return IsValid(); }

    /**
     * @brief 参照を解放
     */
    void Reset() {
        Release();
        m_ptr = nullptr;
        m_control = nullptr;
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

private:
    /**
     * @brief 参照を解放する内部処理
     *
     * プールからの登録解除と参照カウント減少を行う。
     */
    void Release() {
        if (m_ptr != nullptr && m_control != nullptr) {
            uint32_t index = m_control->IndexFromRawPtr(
                const_cast<T*>(m_ptr));
            m_control->UnregisterRef(
                reinterpret_cast<void**>(&m_ptr));
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