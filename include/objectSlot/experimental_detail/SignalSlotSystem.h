#pragma once

#include "SignalSlotSystemBase.h"
#include "SignalSlotPtr.h"
#include "WeakSignalSlotPtr.h"

/**
 * @class SignalSlotSystem
 * @brief シングルトンパターンの通知機能付きオブジェクトプール
 *
 * 【責任】
 * - 型ごとに唯一のインスタンスを提供し、同じ型の要素をアドレス不変の領域に配置する
 * - Create() で要素を生成し、SignalSlotPtr を返す
 * - 要素の解放時に購読者へ逆順で通知する
 *
 * 【使用用途】
 * - 解放通知が必要な要素、および SlotRef で基底型として扱いたい要素の管理
 * - 通知が不要なら ObjectSlotSystem の方が軽い
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class SignalSlotSystem : public SignalSlotSystemBase<T> {
public:
    /// シングルトンインスタンスを取得
    static SignalSlotSystem& GetInstance() {
        static SignalSlotSystem instance;
        return instance;
    }

    /// 新しい要素を作成し SignalSlotPtr を返す（生成上限に達していれば空のポインタ）
    SignalSlotPtr<T> Create(T&& obj) {
        if (!this->CanCreate()) return SignalSlotPtr<T>();

        SlotHandle handle = this->AllocateSlot(std::move(obj));
        SlotHeader* header = this->m_storage.Header(handle.index);   // 仮想関数を通さず直接引く
        ++header->refCount;
        return SignalSlotPtr<T>(header->selfValue);
    }

    SignalSlotSystem(const SignalSlotSystem&) = delete;
    SignalSlotSystem& operator=(const SignalSlotSystem&) = delete;
    SignalSlotSystem(SignalSlotSystem&&) = delete;
    SignalSlotSystem& operator=(SignalSlotSystem&&) = delete;

private:
    SignalSlotSystem() = default;
    ~SignalSlotSystem() = default;
};
