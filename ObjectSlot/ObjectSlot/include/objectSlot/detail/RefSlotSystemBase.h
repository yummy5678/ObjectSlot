#pragma once

#include "SignalSlotSystemBase.h"
#include <vector>
#include <algorithm>

/**
 * @brief SlotRef対応の通知機能付きオブジェクトプール基底クラス
 *
 * SignalSlotSystemBaseを継承し、SlotRefのポインタ更新機能と
 * SlotRef経由の購読機能を追加する。
 *
 * RefEntryをスロットごとに分割管理することで、
 * RegisterRef/UnregisterRefの検索コストをO(K)に抑える。
 * （K = 同じスロットを指すSlotRefの数。通常1〜2個）
 *
 * 主な責任:
 * - SlotRefの登録・登録解除の管理（スロット単位）
 * - プール再アロケーション時の全SlotRefポインタ更新
 * - 要素削除時の該当SlotRefポインタ無効化
 * - SlotRef経由の購読登録・解除
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
     * 対応するスロットのRefEntryリストにエントリを追加する。
     * 通常1つのスロットに対して1〜2個のSlotRefしか存在しないため、
     * リストは非常に小さい。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     * @param slotIndex このSlotRefが指すスロットのインデックス
     */
    void RegisterRef(void** ptrLocation, uint32_t slotIndex) override {
        EnsureSlotCapacity(slotIndex);
        m_refEntriesPerSlot[slotIndex].push_back({ ptrLocation, slotIndex });
    }

    /**
     * @brief SlotRefの登録を解除し、対応するスロットインデックスを返す
     *
     * まずポインタ値からスロットインデックスを算出して直接検索を試みる。
     * ポインタがプール外を指すエイリアシングの場合は
     * 全スロットをフォールバック検索する。
     *
     * 購読が登録されている場合は購読も同時に解除する。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     * @return 対応するスロットインデックス。見つからない場合はINVALID_INDEX
     */
    uint32_t UnregisterRef(void** ptrLocation) override {
        // ポインタ値からスロットインデックスを算出して直接検索を試みる
        if (*ptrLocation != nullptr) {
            T* ptr = static_cast<T*>(*ptrLocation);
            T* dataBegin = this->m_data.data();
            T* dataEnd = dataBegin + this->m_data.size();

            if (ptr >= dataBegin && ptr < dataEnd) {
                uint32_t slotIndex = static_cast<uint32_t>(ptr - dataBegin);
                uint32_t result = RemoveRefEntry(slotIndex, ptrLocation);
                if (result != SlotHandle::INVALID_INDEX) {
                    return result;
                }
            }
        }

        // エイリアシング等でプール外を指す場合は全スロットを検索
        return RemoveRefEntryFullSearch(ptrLocation);
    }

    /**
     * @brief ptrLocationから対応するスロットインデックスを検索する
     *
     * 登録を解除せずにインデックスのみを返す。
     * SlotRefのコピー操作時、コピー元のスロットインデックスを
     * 取得するために使用する。
     *
     * まずポインタ値からインデックスを算出して直接検索を試み、
     * 見つからなければ全スロットを検索する。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     * @return 対応するスロットインデックス。見つからない場合はINVALID_INDEX
     */
    uint32_t FindIndexByRef(const void* ptrLocation) const override {
        const void* const* ppLocation = static_cast<const void* const*>(ptrLocation);

        // ポインタ値からスロットインデックスを算出して直接検索を試みる
        if (*ppLocation != nullptr) {
            const T* ptr = static_cast<const T*>(*ppLocation);
            const T* dataBegin = this->m_data.data();
            const T* dataEnd = dataBegin + this->m_data.size();

            if (ptr >= dataBegin && ptr < dataEnd) {
                uint32_t slotIndex = static_cast<uint32_t>(ptr - dataBegin);
                if (slotIndex < m_refEntriesPerSlot.size()) {
                    for (const auto& entry : m_refEntriesPerSlot[slotIndex]) {
                        if (static_cast<const void*>(entry.ptrLocation) == ptrLocation) {
                            return entry.slotIndex;
                        }
                    }
                }
            }
        }

        // エイリアシング等: 全スロットを検索
        for (uint32_t i = 0; i < m_refEntriesPerSlot.size(); ++i) {
            for (const auto& entry : m_refEntriesPerSlot[i]) {
                if (static_cast<const void*>(entry.ptrLocation) == ptrLocation) {
                    return entry.slotIndex;
                }
            }
        }

        return SlotHandle::INVALID_INDEX;
    }

    /**
     * @brief SlotRefから解放通知を購読する
     *
     * ptrLocationでRefEntryを特定し、そのスロットの購読リストに
     * コールバックを登録する。購読の寿命管理はSubscriptionRefが担う。
     *
     * @param ptrLocation SlotRef内のm_ptrのアドレス
     * @param callback 解放時に実行する関数
     * @return 購読情報（SlotRef側でSubscriptionRefを構築するために使用）
     */
    SlotControlBase::SubscribeRefResult SubscribeByRef(
        void** ptrLocation, std::function<void()> callback) override
    {
        RefEntry* entry = FindRefEntry(ptrLocation);
        if (entry == nullptr) {
            return {};
        }

        uint32_t subId = this->AddSubscription(entry->slotIndex, std::move(callback));
        return { entry->slotIndex, subId };
    }
    
    /// 全SlotRefを無効化した後、プールを初期化する
    void Clear() {
        for (auto& entries : m_refEntriesPerSlot) {
            for (auto& entry : entries) {
                *entry.ptrLocation = nullptr;
            }
        }
        m_refEntriesPerSlot.clear();

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
     * SlotRef内のm_ptrのアドレス、スロットインデックス、
     * および購読IDを保持する。
     * 購読IDが有効な場合、SlotRef破棄時に購読も解除される。
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
     * @param obj 格納する要素（ムーブされる）
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        T* oldData = this->m_data.data();
        SlotHandle handle = SignalSlotSystemBase<T>::AllocateSlot(std::move(obj));
        T* newData = this->m_data.data();

        EnsureSlotCapacity(handle.index);

        if (oldData != newData && oldData != nullptr) {
            UpdateAllRefPtrs(oldData, newData);
        }

        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * この要素を指している全SlotRefのポインタをnullptrに設定し、
     * RefEntryリストをクリアした後、
     * 基底クラスの削除処理（購読者通知 → オブジェクトリセット）を呼ぶ。
     *
     * 購読の解除は基底クラスのRemoveInternalで
     * 購読リスト全体がクリアされるため、個別解除は不要。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        if (handle.index < m_refEntriesPerSlot.size()) {
            for (auto& entry : m_refEntriesPerSlot[handle.index]) {
                *entry.ptrLocation = nullptr;
            }
            m_refEntriesPerSlot[handle.index].clear();
        }

        SignalSlotSystemBase<T>::RemoveInternal(handle);
    }

private:
    /**
     * @brief スロットごとのRefEntryリストの容量を確保する
     *
     * 指定インデックスまでリストが存在することを保証する。
     *
     * @param slotIndex 必要なスロットインデックス
     */
    void EnsureSlotCapacity(uint32_t slotIndex) {
        if (slotIndex >= m_refEntriesPerSlot.size()) {
            m_refEntriesPerSlot.resize(slotIndex + 1);
        }
    }

    /**
     * @brief 指定スロットのRefEntryリストからエントリを削除する
     *
     * 見つかった場合はスロットインデックスを返す。
     * 見つからない場合はINVALID_INDEXを返す。
     * 購読の解除はSubscriptionRefが担うため、ここでは行わない。
     *
     * @param slotIndex 検索するスロットのインデックス
     * @param ptrLocation 削除するエントリのptrLocation
     * @return スロットインデックス。見つからない場合はINVALID_INDEX
     */
    uint32_t RemoveRefEntry(uint32_t slotIndex, void** ptrLocation) {
        if (slotIndex >= m_refEntriesPerSlot.size()) {
            return SlotHandle::INVALID_INDEX;
        }

        auto& entries = m_refEntriesPerSlot[slotIndex];
        for (auto it = entries.begin(); it != entries.end(); ++it) {
            if (it->ptrLocation == ptrLocation) {
                entries.erase(it);
                return slotIndex;
            }
        }

        return SlotHandle::INVALID_INDEX;
    }

    /**
     * @brief 全スロットからptrLocationに一致するRefEntryを検索して削除する
     *
     * エイリアシングSlotRefのようにポインタがプール外を指す場合、
     * ポインタ値からスロットインデックスを算出できないため、
     * 全スロットを走査する必要がある。
     * エイリアシングは稀なケースのため、頻繁には呼ばれない。
     *
     * @param ptrLocation 削除するエントリのptrLocation
     * @return スロットインデックス。見つからない場合はINVALID_INDEX
     */
    uint32_t RemoveRefEntryFullSearch(void** ptrLocation) {
        for (uint32_t i = 0; i < m_refEntriesPerSlot.size(); ++i) {
            uint32_t result = RemoveRefEntry(i, ptrLocation);
            if (result != SlotHandle::INVALID_INDEX) {
                return result;
            }
        }
        return SlotHandle::INVALID_INDEX;
    }

    /**
     * @brief ptrLocationに一致するRefEntryへのポインタを返す
     *
     * SubscribeByRefで使用する。
     * まずポインタ値からスロットを特定し、見つからなければ全スロットを検索する。
     *
     * @param ptrLocation 検索するSlotRef内のm_ptrのアドレス
     * @return 見つかったRefEntryへのポインタ。見つからない場合はnullptr
     */
    RefEntry* FindRefEntry(void** ptrLocation) {
        // ポインタ値からスロットインデックスを算出して直接検索
        if (*ptrLocation != nullptr) {
            T* ptr = static_cast<T*>(*ptrLocation);
            T* dataBegin = this->m_data.data();
            T* dataEnd = dataBegin + this->m_data.size();

            if (ptr >= dataBegin && ptr < dataEnd) {
                uint32_t slotIndex = static_cast<uint32_t>(ptr - dataBegin);
                if (slotIndex < m_refEntriesPerSlot.size()) {
                    for (auto& entry : m_refEntriesPerSlot[slotIndex]) {
                        if (entry.ptrLocation == ptrLocation) {
                            return &entry;
                        }
                    }
                }
            }
        }

        // エイリアシング等: 全スロットを検索
        for (auto& entries : m_refEntriesPerSlot) {
            for (auto& entry : entries) {
                if (entry.ptrLocation == ptrLocation) {
                    return &entry;
                }
            }
        }

        return nullptr;
    }

    /**
     * @brief 全SlotRefのポインタを新しいアドレスに更新
     *
     * m_dataの再アロケーション時に呼ばれる。
     * 全スロットの全エントリを走査して更新する。
     * 再アロケーションは稀なため、全走査でも問題ない。
     *
     * @param oldData 旧m_dataの先頭アドレス
     * @param newData 新m_dataの先頭アドレス
     */
    void UpdateAllRefPtrs(T* oldData, T* newData) {
        for (auto& entries : m_refEntriesPerSlot) {
            for (auto& entry : entries) {
                if (*entry.ptrLocation != nullptr) {
                    *entry.ptrLocation = static_cast<void*>(&newData[entry.slotIndex]);
                }
            }
        }
    }

    /** スロットごとのRefEntryリスト */
    std::vector<std::vector<RefEntry>> m_refEntriesPerSlot;
};