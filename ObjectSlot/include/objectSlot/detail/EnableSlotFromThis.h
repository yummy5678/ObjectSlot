#pragma once

#include "SlotHandle.h"
#include "SlotControlBase.h"

// 前方宣言（メソッド本体はテンプレートの遅延実体化で解決される）
template<typename T> class ObjectSlotSystemBase;
template<typename T> class SignalSlotSystemBase;
template<typename T> class SlotPtr;
template<typename T> class SignalSlotPtr;
template<typename T> class WeakSlotPtr;
template<typename T> class WeakSignalSlotPtr;

/**
 * @brief プール管理対象のオブジェクトが自分自身へのスマートポインタを取得するための基底クラス
 *
 * std::enable_shared_from_thisに相当する機能を提供する。
 * プール（ObjectSlotSystem / SignalSlotSystem / RefSlotSystem）に
 * 格納されたオブジェクトが、自分自身を指すスマートポインタを
 * 安全に取得できるようにする。
 *
 * 使用方法:
 * 1. 管理対象の型がEnableSlotFromThis<T>を継承する
 * 2. プールのCreate()でオブジェクトが作成された後、
 *    プール内部で自動的にスロット情報が設定される
 * 3. オブジェクト内部から適切なFromThis()メソッドを呼ぶ
 *
 * 使用例:
 * @code
 *   class MyObject : public EnableSlotFromThis<MyObject> {
 *   public:
 *       void RegisterSelf() {
 *           auto self = SlotPtrFromThis();   // ObjectSlotSystem使用時
 *           // または
 *           auto self = SignalSlotPtrFromThis(); // SignalSlotSystem使用時
 *       }
 *   };
 * @endcode
 *
 * 注意事項:
 * - SlotPtrFromThis()はObjectSlotSystemで管理されている場合のみ使用可能
 * - SignalSlotPtrFromThis()はSignalSlotSystem/RefSlotSystemで管理されている場合のみ使用可能
 * - 間違ったプール型に対応するメソッドを呼ぶと未定義動作になる
 * - コピー・ムーブ時にスロット情報は転送されない
 *   （std::enable_shared_from_thisと同じ動作）
 *
 * @tparam T 管理対象の型（CRTP: 自分自身の型を渡す）
 */
template<typename T>
class EnableSlotFromThis
{
    friend class ObjectSlotSystemBase<T>;

public:
    /**
     * @brief デフォルトコンストラクタ
     */
    EnableSlotFromThis()
        : m_selfHandle(SlotHandle::Invalid())
        , m_selfControl(nullptr)
    {
    }

    /**
     * @brief コピーコンストラクタ（スロット情報は転送しない）
     *
     * コピー先は新しいスロットに配置される可能性があるため、
     * スロット情報はプール側で再設定される。
     */
    EnableSlotFromThis(const EnableSlotFromThis&)
        : m_selfHandle(SlotHandle::Invalid())
        , m_selfControl(nullptr)
    {
    }

    /**
     * @brief ムーブコンストラクタ（スロット情報は転送しない）
     *
     * ムーブ先は新しいスロットに配置される可能性があるため、
     * スロット情報はプール側で再設定される。
     */
    EnableSlotFromThis(EnableSlotFromThis&&) noexcept
        : m_selfHandle(SlotHandle::Invalid())
        , m_selfControl(nullptr)
    {
    }

    /**
     * @brief コピー代入演算子（スロット情報は変更しない）
     *
     * 自分自身のスロット位置は変わらないため、
     * スロット情報をコピー元から上書きしてはならない。
     */
    EnableSlotFromThis& operator=(const EnableSlotFromThis&) { return *this; }

    /**
     * @brief ムーブ代入演算子（スロット情報は変更しない）
     *
     * 自分自身のスロット位置は変わらないため、
     * スロット情報をムーブ元から上書きしてはならない。
     */
    EnableSlotFromThis& operator=(EnableSlotFromThis&&) noexcept { return *this; }

    virtual ~EnableSlotFromThis() = default;

protected:
    // ================================================================
    // 強参照の取得
    // ================================================================

    /**
     * @brief 自分自身へのSlotPtrを取得
     *
     * ObjectSlotSystemで管理されている場合に使用する。
     * SignalSlotSystem/RefSlotSystemで管理されている場合は
     * SignalSlotPtrFromThis()を使用すること。
     *
     * @return 自分自身を指すSlotPtr。未登録の場合は空のSlotPtr
     */
    SlotPtr<T> SlotPtrFromThis()
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return SlotPtr<T>();
        }
        m_selfControl->AddRefByIndex(m_selfHandle.index);
        return SlotPtr<T>(m_selfHandle, static_cast<ObjectSlotSystemBase<T>*>(m_selfControl));
    }

    /**
     * @brief 自分自身へのSlotPtrを取得 (const版)
     */
    SlotPtr<T> SlotPtrFromThis() const
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return SlotPtr<T>();
        }
        m_selfControl->AddRefByIndex(m_selfHandle.index);
        return SlotPtr<T>(m_selfHandle, static_cast<ObjectSlotSystemBase<T>*>(m_selfControl));
    }

    /**
     * @brief 自分自身へのSignalSlotPtrを取得
     *
     * SignalSlotSystem/RefSlotSystemで管理されている場合に使用する。
     * ObjectSlotSystemで管理されている場合は
     * SlotPtrFromThis()を使用すること。
     *
     * @return 自分自身を指すSignalSlotPtr。未登録の場合は空
     */
    SignalSlotPtr<T> SignalSlotPtrFromThis()
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return SignalSlotPtr<T>();
        }
        m_selfControl->AddRefByIndex(m_selfHandle.index);
        return SignalSlotPtr<T>(m_selfHandle, static_cast<SignalSlotSystemBase<T>*>(m_selfControl));
    }

    /**
     * @brief 自分自身へのSignalSlotPtrを取得 (const版)
     */
    SignalSlotPtr<T> SignalSlotPtrFromThis() const
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return SignalSlotPtr<T>();
        }
        m_selfControl->AddRefByIndex(m_selfHandle.index);
        return SignalSlotPtr<T>(m_selfHandle, static_cast<SignalSlotSystemBase<T>*>(m_selfControl));
    }

    // ================================================================
    // 弱参照の取得
    // ================================================================

    /**
     * @brief 自分自身へのWeakSlotPtrを取得
     *
     * ObjectSlotSystemで管理されている場合に使用する。
     * 参照カウントに影響を与えない。
     *
     * @return 自分自身を指すWeakSlotPtr。未登録の場合は空
     */
    WeakSlotPtr<T> WeakSlotPtrFromThis() const
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return WeakSlotPtr<T>();
        }
        return WeakSlotPtr<T>(m_selfHandle, static_cast<ObjectSlotSystemBase<T>*>(m_selfControl));
    }

    /**
     * @brief 自分自身へのWeakSignalSlotPtrを取得
     *
     * SignalSlotSystem/RefSlotSystemで管理されている場合に使用する。
     * 参照カウントに影響を与えない。
     *
     * @return 自分自身を指すWeakSignalSlotPtr。未登録の場合は空
     */
    WeakSignalSlotPtr<T> WeakSignalSlotPtrFromThis() const
    {
        if (m_selfControl == nullptr || !m_selfControl->IsValidHandle(m_selfHandle)) {
            return WeakSignalSlotPtr<T>();
        }
        // 一時的に強参照を作り、そこから弱参照に変換する
        m_selfControl->AddRefByIndex(m_selfHandle.index);
        SignalSlotPtr<T> temp(m_selfHandle, static_cast<SignalSlotSystemBase<T>*>(m_selfControl));
        WeakSignalSlotPtr<T> weak(temp);
        return weak;
        // temp破棄時にReleaseRefが呼ばれ参照カウントは元に戻る
    }

private:
    /**
     * @brief プール側からスロット情報を設定する
     *
     * ObjectSlotSystemBase::AllocateSlotから呼ばれる。
     * オブジェクトがプールに配置された後に、
     * 自分がどのプールのどのスロットにいるかを記録する。
     *
     * @param handle 配置されたスロットのハンドル
     * @param control 所属するプールの制御ブロック
     */
    void InitSlotFromThis(SlotHandle handle, SlotControlBase* control)
    {
        m_selfHandle = handle;
        m_selfControl = control;
    }

    /** 自分自身のスロットハンドル */
    SlotHandle m_selfHandle;

    /** 所属するプールの制御ブロック */
    SlotControlBase* m_selfControl;
};