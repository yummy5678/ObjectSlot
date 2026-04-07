#pragma once

#include "ObjectSlotSystemBase.h"
#include "SlotPtr.h"
#include "WeakSlotPtr.h"

/**
 * @brief シングルトンパターンのオブジェクトプール
 *
 * 型ごとに唯一のインスタンスを提供し、
 * 同じ型のオブジェクトを連続メモリに配置して管理する。
 *
 * Create()で軽量なSlotPtrを、
 * CreateSignal()で通知機能付きのSystemを返す。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystem : public ObjectSlotSystemBase<T> {
public:
    /**
     * @brief シングルトンインスタンスを取得
     * @return プールインスタンスへの参照
     */
    static ObjectSlotSystem& GetInstance() {
        static ObjectSlotSystem instance;
        return instance;
    }

    /**
     * @brief 新しい要素を作成（軽量版）
     *
     * 通知機能なしのSlotPtrを返す。
     *
     * @param obj 追加する要素 (ムーブされる)
     * @return 作成された要素へのSlotPtr
     */
    SlotPtr<T> Create(T&& obj) {
        if (!this->CanCreate()) return SlotPtr<T>();
        
        SlotHandle handle = this->AllocateSlot(std::move(obj));
        ++this->m_refCounts[handle.index];
        auto rp = this->GetRootPointer(handle.index);
        return SlotPtr<T>(rp, this);
    }

    // コピー禁止
    ObjectSlotSystem(const ObjectSlotSystem&) = delete;
    ObjectSlotSystem& operator=(const ObjectSlotSystem&) = delete;

    // ムーブ禁止
    ObjectSlotSystem(ObjectSlotSystem&&) = delete;
    ObjectSlotSystem& operator=(ObjectSlotSystem&&) = delete;

private:
    ObjectSlotSystem() = default;
    ~ObjectSlotSystem() = default;
};