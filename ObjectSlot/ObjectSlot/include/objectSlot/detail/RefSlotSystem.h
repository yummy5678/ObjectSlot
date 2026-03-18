#pragma once

#include "RefSlotSystemBase.h"
#include "SignalSlotPtr.h"
#include "SlotRef.h"

/**
 * @brief SlotRef対応のシングルトンプール
 *
 * SignalSlotPtr（購読通知）とSlotRef（ポリモーフィック参照）の
 * 両方に対応した最上位のプールクラス。
 *
 * SlotRefを使って異なる具体型のオブジェクトを
 * 基底インターフェース型で統一的に管理できる。
 * プール再アロケーション時にSlotRefのポインタが自動更新される。
 *
 * 通知機能だけで良い場合はSignalSlotSystemを使用すること。
 * 通知もポリモーフィック参照も不要な場合はObjectSlotSystemを使用すること。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class RefSlotSystem : public RefSlotSystemBase<T> {
public:
    /// シングルトンインスタンスを取得
    static RefSlotSystem& GetInstance() {
        static RefSlotSystem instance;
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
    RefSlotSystem(const RefSlotSystem&) = delete;
    RefSlotSystem& operator=(const RefSlotSystem&) = delete;
    RefSlotSystem(RefSlotSystem&&) = delete;
    RefSlotSystem& operator=(RefSlotSystem&&) = delete;

private:
    RefSlotSystem() = default;
    ~RefSlotSystem() = default;
};