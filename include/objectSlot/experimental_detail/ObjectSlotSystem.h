#pragma once

#include "ObjectSlotSystemBase.h"
#include "SlotPtr.h"
#include "WeakSlotPtr.h"

/**
 * @class ObjectSlotSystem
 * @brief シングルトンパターンの軽量オブジェクトプール
 *
 * 【責任】
 * - 型ごとに唯一のインスタンスを提供し、同じ型の要素をアドレス不変の領域に配置する
 * - Create() で要素を生成し、SlotPtr を返す
 *
 * 【使用用途】
 * - 解放通知が不要な要素の管理。通知が必要な場合は SignalSlotSystem を使う
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystem : public ObjectSlotSystemBase<T> {
public:
    /// シングルトンインスタンスを取得
    static ObjectSlotSystem& GetInstance() {
        static ObjectSlotSystem instance;
        return instance;
    }

    /// 新しい要素を作成し SlotPtr を返す（生成上限に達していれば空のポインタ）
    SlotPtr<T> Create(T&& obj) {
        if (!this->CanCreate()) return SlotPtr<T>();

        SlotHandle handle = this->AllocateSlot(std::move(obj));
        SlotHeader* header = this->m_storage.Header(handle.index);   // 仮想関数を通さず直接引く
        ++header->refCount;
        return SlotPtr<T>(header->selfValue);
    }

    ObjectSlotSystem(const ObjectSlotSystem&) = delete;
    ObjectSlotSystem& operator=(const ObjectSlotSystem&) = delete;
    ObjectSlotSystem(ObjectSlotSystem&&) = delete;
    ObjectSlotSystem& operator=(ObjectSlotSystem&&) = delete;

private:
    ObjectSlotSystem() = default;
    ~ObjectSlotSystem() = default;
};
