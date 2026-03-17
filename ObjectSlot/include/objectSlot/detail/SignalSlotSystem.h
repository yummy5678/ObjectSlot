#pragma once

#include "SignalSlotSystemBase.h"
#include "SignalSlotPtr.h"

/**
 * @brief シングルトンパターンの通知機能付きオブジェクトプール
 *
 * 型ごとに唯一のインスタンスを提供し、
 * 同じ型のオブジェクトを連続メモリに配置して管理する。
 * 要素の解放時に購読者へ逆順で通知を送る機能を持つ。
 *
 * 通知機能が不要な場合はObjectSlotSystemを使用すること。
 * SlotRefによるポリモーフィック参照が必要な場合はRefSlotSystemを使用すること。
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

    /// 新しい要素を作成しSignalSlotPtrを返す
    SignalSlotPtr<T> Create(T&& obj) {
        if (!this->CanCreate()) return SignalSlotPtr<T>();
        
        SlotHandle handle = this->AllocateSlot(std::move(obj));
        ++this->m_refCounts[handle.index];
        auto rp = this->GetRootPointer(handle.index);
        return SignalSlotPtr<T>(rp, this);
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