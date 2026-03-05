#pragma once

#include "SignalSlotSystemBase.h"
#include"SignalSlotPtr.h"

/**
 * @brief シングルトンパターンの通知機能付きオブジェクトプール
 *
 * 型ごとに唯一のインスタンスを提供し、
 * 同じ型のオブジェクトを連続メモリに配置して管理する。
 * 要素の解放時に購読者へ通知を送る機能を持つ。
 *
 * 通知機能が不要な場合はObjectSlotを使用すること。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class SignalSlotSystem : public SignalSlotSystemBase<T> {
public:
    /**
     * @brief シングルトンインスタンスを取得
     * @return プールインスタンスへの参照
     */
    static SignalSlotSystem& GetInstance() {
        static SignalSlotSystem instance;
        return instance;
    }

    /**
     * @brief 新しい要素を作成
     *
     * @param obj 追加する要素 (ムーブされる)
     * @return 作成された要素へのSignalSlotSystemPtr
     */
    SignalSlotPtr<T> Create(T&& obj) {
        if (!this->CanCreate()) {
            return SignalSlotPtr<T>();
        }

        SlotHandle handle = this->AllocateSlot(std::move(obj));
        this->AddRef(handle);
        return SignalSlotPtr<T>(handle, this);
    }

    // コピー・ムーブ禁止
    SignalSlotSystem(const SignalSlotSystem&) = delete;
    SignalSlotSystem& operator=(const SignalSlotSystem&) = delete;
    SignalSlotSystem(SignalSlotSystem&&) = delete;
    SignalSlotSystem& operator=(SignalSlotSystem&&) = delete;

private:
    SignalSlotSystem() = default;
    ~SignalSlotSystem() = default;
};