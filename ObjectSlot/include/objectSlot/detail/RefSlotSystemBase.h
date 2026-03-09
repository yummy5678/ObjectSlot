#pragma once

#include "SignalSlotSystemBase.h"
#include <vector>
#include <algorithm>

/**
 * @brief SlotRef対応の通知機能付きオブジェクトプール基底クラス
 *
 * SignalSlotSystemBaseを継承し、SlotRefのポインタ更新機能を追加する。
 * プールの再アロケーション時に、登録された全てのSlotRefの
 * m_ptrを新しいアドレスに自動更新する。
 *
 * 主な責任:
 * - SlotRefの登録・登録解除の管理
 * - プール再アロケーション時の全SlotRefポインタ更新
 * - 要素削除時の該当SlotRefポインタ無効化
 *
 * 使用用途:
 * - 異なる具体型のオブジェクトを基底インターフェースで統一管理する場合のプール基底
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class RefSlotSystemBase : public SignalSlotSystemBase<T> {

public:
    virtual ~RefSlotSystemBase() = default;

    /**
     * @brief SlotRefのポインタ更新用の登録
     *
     * SlotRefが作成・コピーされる際に呼ばれ、
     * 再アロケーション時にポインタを更新するための情報を登録する。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     * @param slotIndex このSlotRefが指すスロットのインデックス
     */
    void RegisterRef(void** ptrLocation, uint32_t slotIndex) override {
        m_refEntries.push_back({ ptrLocation, slotIndex });
    }

    /**
     * @brief SlotRefのポインタ更新用の登録を解除
     *
     * SlotRefが破棄・ムーブされる際に呼ばれ、
     * 登録リストから該当エントリを削除する。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     */
    void UnregisterRef(void** ptrLocation) override {
        auto it = std::remove_if(m_refEntries.begin(), m_refEntries.end(),
            [ptrLocation](const RefEntry& entry) {
                return entry.ptrLocation == ptrLocation;
            });
        m_refEntries.erase(it, m_refEntries.end());
    }

    /// 全SlotRefを無効化した後、プールを初期化する
    void Clear() {
        for (auto& entry : m_refEntries) {
            *entry.ptrLocation = nullptr;
        }
        m_refEntries.clear();

        SignalSlotSystemBase<T>::Clear();
    }

    /// メモリを事前確保する（再アロケーション発生時はSlotRefも更新）
    void Reserve(size_t capacity) {
        T* oldData = this->m_data.data();
        SignalSlotSystemBase<T>::Reserve(capacity);
        T* newData = this->m_data.data();

        if (oldData != newData && oldData != nullptr) {
            UpdateAllRefPtrs(oldData, newData);
        }
    }

protected:
    /**
     * @brief SlotRefの登録情報
     *
     * SlotRef内のm_ptrのアドレスと、
     * そのSlotRefが指しているスロットのインデックスを保持する。
     */
    struct RefEntry {
        /** SlotRef内のm_ptrのアドレス */
        void** ptrLocation;

        /** 指しているスロットのインデックス */
        uint32_t slotIndex;
    };

    /**
     * @brief スロットを確保し、再アロケーション時はSlotRefを更新する
     *
     * 基底クラスのAllocateSlotを呼び出し、
     * m_dataの再アロケーションが発生した場合は
     * 全SlotRefのポインタを新しいアドレスに更新する。
     *
     * @param obj 格納する要素（ムーブされる）
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        T* oldData = this->m_data.data();
        SlotHandle handle = SignalSlotSystemBase<T>::AllocateSlot(std::move(obj));
        T* newData = this->m_data.data();

        if (oldData != newData && oldData != nullptr) {
            UpdateAllRefPtrs(oldData, newData);
        }

        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * この要素を指している全SlotRefのポインタをnullptrに設定した後、
     * 基底クラスの削除処理（購読者通知 → オブジェクトリセット）を呼ぶ。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        for (auto& entry : m_refEntries) {
            if (entry.slotIndex == handle.index) {
                *entry.ptrLocation = nullptr;
            }
        }

        SignalSlotSystemBase<T>::RemoveInternal(handle);
    }

private:
    /**
     * @brief 全SlotRefのポインタを新しいアドレスに更新
     *
     * m_dataの再アロケーション時に呼ばれる。
     * 各SlotRefのm_ptrを旧アドレスから新アドレスに変換する。
     *
     * @param oldData 旧m_dataの先頭アドレス
     * @param newData 新m_dataの先頭アドレス
     */
    void UpdateAllRefPtrs(T* oldData, T* newData) {
        for (auto& entry : m_refEntries) {
            if (*entry.ptrLocation != nullptr) {
                *entry.ptrLocation = static_cast<void*>(&newData[entry.slotIndex]);
            }
        }
    }

    /** 登録されたSlotRefの一覧 */
    std::vector<RefEntry> m_refEntries;
};