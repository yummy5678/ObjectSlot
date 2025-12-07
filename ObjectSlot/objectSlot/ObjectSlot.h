#pragma once

#include "ObjectSlotBase.h"
#include "SlotPtr.h"
#include "WeakSlotPtr.h"

/**
 * @brief シングルトンパターンのオブジェクトプール
 *
 * 型ごとに唯一のインスタンスを提供し、
 * 同じ型のオブジェクトを連続メモリに配置して管理する。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlot : public ObjectSlotBase<T> {
public:
    /**
     * @brief シングルトンインスタンスを取得
     * @return プールインスタンスへの参照
     */
    static ObjectSlot& GetInstance() {
        static ObjectSlot instance;
        return instance;
    }

    /**
     * @brief 新しい要素を作成
     *
     * 要素をプールに追加し、参照カウント1のSlotRefを返す。
     * 全てのSlotRefが破棄されると、要素は自動的に削除される。
     *
     * @param obj 追加する要素 (ムーブされる)
     * @return 作成された要素へのSlotRef
     */
    SlotPtr<T> Create(T&& obj) {
        // 最大容量チェック
        if (!this->CanCreate()) {
            return SlotPtr<T>();  // 無効なSlotRefを返す
        }

        // スロットを確保して要素を配置
        SlotHandle handle = this->AllocateSlot(std::move(obj));

        // 初期参照カウントを1に設定
        this->AddRef(handle);

        return SlotPtr<T>(handle, this);
    }

    // コピー禁止
    ObjectSlot(const ObjectSlot&) = delete;
    ObjectSlot& operator=(const ObjectSlot&) = delete;

    // ムーブ禁止
    ObjectSlot(ObjectSlot&&) = delete;
    ObjectSlot& operator=(ObjectSlot&&) = delete;

private:
    /**
     * @brief プライベートコンストラクタ (シングルトン)
     */
    ObjectSlot() = default;

    /**
     * @brief プライベートデストラクタ (シングルトン)
     */
    ~ObjectSlot() = default;
};