#pragma once

#include "SlotHandle.h"
#include <functional>

// 前方宣言
template<typename T>
class ObjectSlotBase;

template<typename T>
class WeakSlotRef;

/**
 * @brief 参照カウント方式のスマートポインタ (shared_ptr風)
 *
 * オブジェクトプール内の要素への参照を管理する。
 * コピー時に参照カウントが増加し、破棄時に減少する。
 * 参照カウントが0になると、要素は自動的に削除される。
 *
 * SetOnDestroy()で破棄時のコールバックを設定できる。
 *
 * @tparam T プール内で管理される要素の型
 */
template<typename T>
class SlotRef {
    // WeakSlotRefからprivateメンバにアクセスするため
    friend class WeakSlotRef<T>;

public:
    /**
     * @brief デフォルトコンストラクタ
     *
     * 無効な状態のSlotRefを生成する。
     */
    SlotRef()
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief nullptrからの構築
     *
     * 無効な状態のSlotRefを生成する。
     */
    SlotRef(std::nullptr_t)
        : m_handle(SlotHandle::Invalid())
        , m_slot(nullptr)
    {
    }

    /**
     * @brief ハンドルとプールポインタを指定して構築
     *
     * @param handle プール内の要素を指すハンドル
     * @param slot 要素が属するプールへのポインタ
     * @note 通常はObjectSlot::Create()経由で生成される
     */
    SlotRef(SlotHandle handle, ObjectSlotBase<T>* slot)
        : m_handle(handle)
        , m_slot(slot)
    {
    }

    /**
     * @brief コピーコンストラクタ
     *
     * 参照カウントを増加させる。
     *
     * @param other コピー元のSlotRef
     */
    SlotRef(const SlotRef& other)
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->AddRef(m_handle);
        }
    }

    /**
     * @brief コピー代入演算子
     *
     * 現在の参照を解放し、新しい参照をコピーする。
     *
     * @param other コピー元のSlotRef
     * @return 自身への参照
     */
    SlotRef& operator=(const SlotRef& other) {
        if (this != &other) {
            // 現在の参照を解放
            Release();

            // 新しい参照をコピー
            m_handle = other.m_handle;
            m_slot = other.m_slot;

            // 参照カウントを増加
            if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
                m_slot->AddRef(m_handle);
            }
        }
        return *this;
    }

    /**
     * @brief ムーブコンストラクタ
     *
     * 参照カウントは変化しない (所有権の移動のみ)。
     *
     * @param other ムーブ元のSlotRef
     */
    SlotRef(SlotRef&& other) noexcept
        : m_handle(other.m_handle)
        , m_slot(other.m_slot)
    {
        // ムーブ元を無効化
        other.m_handle = SlotHandle::Invalid();
        other.m_slot = nullptr;
    }

    /**
     * @brief ムーブ代入演算子
     *
     * 現在の参照を解放し、所有権を移動する。
     *
     * @param other ムーブ元のSlotRef
     * @return 自身への参照
     */
    SlotRef& operator=(SlotRef&& other) noexcept {
        if (this != &other) {
            // 現在の参照を解放
            Release();

            // 所有権を移動
            m_handle = other.m_handle;
            m_slot = other.m_slot;

            // ムーブ元を無効化
            other.m_handle = SlotHandle::Invalid();
            other.m_slot = nullptr;
        }
        return *this;
    }

    /**
     * @brief nullptr代入演算子
     *
     * 参照を解放してリセットする。Reset()と同等。
     *
     * @return 自身への参照
     */
    SlotRef& operator=(std::nullptr_t) noexcept {
        Reset();
        return *this;
    }

    /**
     * @brief デストラクタ
     *
     * 参照カウントを減少させ、0になったら要素を削除する。
     */
    ~SlotRef() {
        Release();
    }

    /**
     * @brief アロー演算子
     * @return 要素へのポインタ
     */
    T* operator->() {
        return Get();
    }

    /**
     * @brief アロー演算子 (const版)
     * @return 要素へのconstポインタ
     */
    const T* operator->() const {
        return Get();
    }

    /**
     * @brief 間接参照演算子
     * @return 要素への参照
     */
    T& operator*() {
        return *Get();
    }

    /**
     * @brief 間接参照演算子 (const版)
     * @return 要素へのconst参照
     */
    const T& operator*() const {
        return *Get();
    }

    /**
     * @brief 要素へのポインタを取得
     * @return 有効な場合は要素へのポインタ、無効な場合はnullptr
     */
    T* Get() {
        if (!IsValid()) {
            return nullptr;
        }
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 要素へのポインタを取得 (const版)
     * @return 有効な場合は要素へのconstポインタ、無効な場合はnullptr
     */
    const T* Get() const {
        if (!IsValid()) {
            return nullptr;
        }
        return m_slot->Get(m_handle);
    }

    /**
     * @brief 参照が有効かどうかを判定
     * @return プールとハンドルが有効であればtrue
     */
    bool IsValid() const {
        return m_slot != nullptr && m_slot->IsValidHandle(m_handle);
    }

    /**
     * @brief bool変換演算子
     * @return IsValid()の結果
     */
    explicit operator bool() const {
        return IsValid();
    }

    /**
     * @brief 参照カウントを取得
     * @return 現在の参照カウント、無効な場合は0
     */
    uint32_t UseCount() const {
        if (!IsValid()) {
            return 0;
        }
        return m_slot->GetRefCount(m_handle);
    }

    /**
     * @brief 弱参照を生成
     * @return このSlotRefに対応するWeakSlotRef
     */
    WeakSlotRef<T> GetWeak() const;

    /**
     * @brief 参照を解放
     *
     * 参照カウントを減少させ、ハンドルを無効化する。
     * 参照カウントが0になった場合、要素はプールから削除される。
     */
    void Reset() {
        Release();
        m_handle = SlotHandle::Invalid();
        m_slot = nullptr;
    }

    /**
     * @brief ハンドルを取得
     * @return 内部で保持しているSlotHandle
     */
    SlotHandle GetHandle() const {
        return m_handle;
    }

    /**
     * @brief 破棄時コールバックを設定
     *
     * 参照カウントが0になった時に呼び出されるコールバックを設定する。
     * コピーされたSlotRef間で共有される (要素ごとに1つ)。
     *
     * @param callback 破棄時に実行する関数
     */
    void SetOnDestroy(std::function<void()> callback) {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->SetOnDestroyCallback(m_handle, std::move(callback));
        }
    }

    /**
     * @brief 破棄時コールバックを解除
     */
    void ClearOnDestroy() {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->ClearOnDestroyCallback(m_handle);
        }
    }

    /**
     * @brief 他のSlotRefとの等価比較
     */
    bool operator==(const SlotRef& other) const {
        return m_handle == other.m_handle && m_slot == other.m_slot;
    }

    /**
     * @brief 他のSlotRefとの非等価比較
     */
    bool operator!=(const SlotRef& other) const {
        return !(*this == other);
    }

    /**
     * @brief nullptrとの等価比較
     * @return 無効な場合はtrue
     */
    bool operator==(std::nullptr_t) const noexcept {
        return !IsValid();
    }

    /**
     * @brief nullptrとの非等価比較
     * @return 有効な場合はtrue
     */
    bool operator!=(std::nullptr_t) const noexcept {
        return IsValid();
    }

private:
    /**
     * @brief 参照を解放する内部処理
     *
     * 有効な参照であれば参照カウントを減少させる。
     * 参照カウントが0になった場合、プール側で要素が削除される。
     */
    void Release() {
        if (m_slot != nullptr && m_slot->IsValidHandle(m_handle)) {
            m_slot->ReleaseRef(m_handle);
        }
    }

    /** 要素を識別するハンドル */
    SlotHandle m_handle;

    /** 要素が属するプールへのポインタ */
    ObjectSlotBase<T>* m_slot;
};

/**
 * @brief nullptrが左辺の場合の等価比較
 */
template<typename T>
bool operator==(std::nullptr_t, const SlotRef<T>& rhs) noexcept {
    return rhs == nullptr;
}

/**
 * @brief nullptrが左辺の場合の非等価比較
 */
template<typename T>
bool operator!=(std::nullptr_t, const SlotRef<T>& rhs) noexcept {
    return rhs != nullptr;
}