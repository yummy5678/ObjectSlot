#pragma once

#include "ObjectSlotSystemBase.h"

#include <functional>
#include <algorithm>
#include <vector>

template<typename T>
class SignalSlotPtr;

template<typename T>
class WeakSignalSlotPtr;

template<typename T>
class Subscription;

/**
 * @class SignalSlotSystemBase
 * @brief 要素の解放時に購読者へ通知する機能を持つプールの基底クラス
 *
 * 【責任】
 * - スロットごとの購読リスト（コールバック）を管理する
 * - 要素の削除直前に購読者へ登録の逆順で通知する
 * - 通知ループ中に発生した削除を通知完了まで遅延し、再入による破壊を防ぐ
 *
 * 【遅延削除の扱い】
 * - コールバック内で別の要素の参照カウントが0になった場合、その削除は
 *   通知完了後にまとめて実行する
 * - 遅延中に弱参照から Lock() されて参照カウントが復活した要素は削除しない
 * - コールバック内での購読解除は「取り消し済み」の印を付けるだけにし、
 *   通知完了後に取り除く
 *
 * 【使用用途】
 * - SignalSlotSystem が継承する。利用者はそのシングルトンを使う
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class SignalSlotSystemBase : public ObjectSlotSystemBase<T> {
    friend class SignalSlotPtr<T>;
    friend class WeakSignalSlotPtr<T>;
    friend class Subscription<T>;

public:
    using SubscriptionCallback = std::function<void()>;

    /// 型ごとの通知付きプール台帳に登録する
    SignalSlotSystemBase() {
        s_signalPoolByTag[this->GetTag()] = this;
    }

    /// 台帳から外す
    virtual ~SignalSlotSystemBase() {
        s_signalPoolByTag[this->GetTag()] = nullptr;
    }

    /// タグから通知付きプールを取得（破棄済み、または通知なしプールなら nullptr）
    static SignalSlotSystemBase* SignalPoolFromTag(uint8_t tag) {
        return s_signalPoolByTag[tag];
    }

    using MemoryUsage = typename ObjectSlotSystemBase<T>::MemoryUsage;

    /// メモリ使用量の内訳を取得（購読リストの分を加える）
    MemoryUsage GetMemoryUsage() const {
        MemoryUsage usage = ObjectSlotSystemBase<T>::GetMemoryUsage();
        usage.subscriptionBytes = m_subscriptions.capacity() * sizeof(SlotSubscriptions);
        for (const auto& slotSubscriptions : m_subscriptions) {
            usage.subscriptionBytes += slotSubscriptions.entries.capacity() * sizeof(SubscriptionEntry);
        }
        usage.metadataBytes += m_pendingRemovals.capacity() * sizeof(SlotHandle);
        return usage;
    }

    /**
     * @brief 全要素へ通知した上で破棄し、プールを空にする
     *
     * 通知の中で削除が発生しても、通知完了後に遅延処理されるため安全。
     */
    void Clear() {
        const uint32_t size = this->m_storage.Size();
        for (uint32_t index = 0; index < size; ++index) {
            if (this->m_alive[index]) {
                NotifySubscribers(index);
            }
        }
        ObjectSlotSystemBase<T>::Clear();
        m_subscriptions.clear();
        m_pendingRemovals.clear();
    }

    /// 指定数の要素が入るよう領域を先に確保する
    void Reserve(size_t capacity) {
        ObjectSlotSystemBase<T>::Reserve(capacity);
        if (capacity > m_subscriptions.size()) {
            m_subscriptions.reserve(capacity);
        }
    }

    /// 末尾の空きスロットを切り詰める（購読リストも合わせて縮める）
    void ShrinkToFit() {
        const size_t oldSize = this->m_storage.Size();
        ObjectSlotSystemBase<T>::ShrinkToFit();
        const size_t newSize = this->m_storage.Size();
        if (newSize < oldSize) {
            m_subscriptions.resize(newSize);
            m_subscriptions.shrink_to_fit();
        }
    }

    /// 解放通知を購読する（型消去された経路用）
    uint32_t SubscribeByIndex(uint32_t slotIndex, std::function<void()> callback) override {
        if (slotIndex >= this->m_alive.size() || !this->m_alive[slotIndex]) {
            return SlotControlBase::INVALID_SUBSCRIPTION_ID;
        }
        return AddSubscription(slotIndex, std::move(callback));
    }

    /// 購読を解除する（型消去された経路用）
    void RemoveSubscriptionByIndex(uint32_t slotIndex, uint32_t subscriptionId) override {
        RemoveSubscription(slotIndex, subscriptionId);
    }

    /// 購読のコールバックを差し替える（型消去された経路用）
    void UpdateSubscriptionCallbackByIndex(uint32_t slotIndex, uint32_t subscriptionId, std::function<void()> callback) override {
        UpdateSubscriptionCallback(slotIndex, subscriptionId, std::move(callback));
    }

protected:
    /// 購読1件分の記録
    struct SubscriptionEntry {
        uint32_t id = 0;                 ///< スロット内で一意な購読ID
        SubscriptionCallback callback;   ///< 解放時に呼ぶ関数
        bool cancelled = false;          ///< 通知中に解除された印
    };

    /// スロット1つ分の購読リスト
    struct SlotSubscriptions {
        uint32_t nextId = 0;                      ///< 次に発行する購読ID
        std::vector<SubscriptionEntry> entries;   ///< 登録順の購読
    };

    /**
     * @brief スロットを確保し、購読リストも用意する
     *
     * @param obj 構築元の要素（ムーブされる）
     * @return 確保したスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        SlotHandle handle = ObjectSlotSystemBase<T>::AllocateSlot(std::move(obj));
        if (handle.index < m_subscriptions.size()) {
            m_subscriptions[handle.index] = SlotSubscriptions{};
        }
        else {
            m_subscriptions.push_back(SlotSubscriptions{});
        }
        return handle;
    }

    /**
     * @brief 要素を削除する（通知ループ中なら遅延する）
     *
     * 通知中に削除を実行すると、走査中の購読リストや要素が壊れるため、
     * 通知の深さが0でない間は削除待ちに積んで戻る。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        if (m_notifyDepth > 0) {
            m_pendingRemovals.push_back(handle);
            return;
        }
        ExecuteRemoval(handle);
    }

    /// 購読を追加し、購読IDを返す
    uint32_t AddSubscription(uint32_t slotIndex, SubscriptionCallback callback) {
        SlotSubscriptions& subs = m_subscriptions[slotIndex];
        const uint32_t id = subs.nextId++;
        subs.entries.push_back({ id, std::move(callback), false });
        return id;
    }

    /**
     * @brief 購読を解除する
     *
     * 通知中は印を付けるだけにし、通知完了後に取り除く。
     *
     * @param slotIndex スロットの添字
     * @param subscriptionId 購読ID
     */
    void RemoveSubscription(uint32_t slotIndex, uint32_t subscriptionId) {
        if (slotIndex >= m_subscriptions.size()) return;
        auto& entries = m_subscriptions[slotIndex].entries;

        if (m_notifyDepth > 0) {
            for (auto& entry : entries) {
                if (entry.id == subscriptionId) {
                    entry.cancelled = true;
                    return;
                }
            }
        }
        else {
            auto it = std::remove_if(entries.begin(), entries.end(),
                [subscriptionId](const SubscriptionEntry& entry) {
                    return entry.id == subscriptionId;
                });
            entries.erase(it, entries.end());
        }
    }

    /// 購読のコールバックを差し替える
    void UpdateSubscriptionCallback(uint32_t slotIndex, uint32_t subscriptionId, SubscriptionCallback newCallback) {
        if (slotIndex >= m_subscriptions.size()) return;
        for (auto& entry : m_subscriptions[slotIndex].entries) {
            if (entry.id == subscriptionId) {
                entry.callback = std::move(newCallback);
                return;
            }
        }
    }

    /**
     * @brief 購読者へ登録の逆順で通知する
     *
     * 通知の深さを増やしてから呼び、完了後に取り消し済みの購読を取り除く。
     * 深さが0に戻ったら、通知中に遅延された削除をまとめて実行する。
     *
     * @param slotIndex 通知するスロットの添字
     */
    void NotifySubscribers(uint32_t slotIndex) {
        if (slotIndex >= m_subscriptions.size()) return;
        SlotSubscriptions& subs = m_subscriptions[slotIndex];
        if (subs.entries.empty()) return;

        ++m_notifyDepth;

        const size_t count = subs.entries.size();
        for (size_t i = count; i > 0; --i) {
            SubscriptionEntry& entry = subs.entries[i - 1];
            if (!entry.cancelled && entry.callback) {
                entry.callback();
            }
        }

        --m_notifyDepth;

        auto newEnd = std::remove_if(subs.entries.begin(), subs.entries.end(),
            [](const SubscriptionEntry& entry) { return entry.cancelled; });
        subs.entries.erase(newEnd, subs.entries.end());

        if (m_notifyDepth == 0) {
            ProcessPendingRemovals();
        }
    }

    /**
     * @brief 要素が実際に削除された直後に呼ばれるフック
     *
     * 即時削除・遅延削除のどちらの経路でも必ず呼ばれる。
     * 派生クラスが削除後の後処理を行いたい場合はここを上書きする。
     *
     * @param slotIndex 削除された要素のスロット添字
     */
    virtual void OnSlotRemoved(uint32_t slotIndex) {
        (void)slotIndex;
    }

    /** スロットごとの購読リスト */
    std::vector<SlotSubscriptions> m_subscriptions;

private:
    /**
     * @brief 実際の削除処理を実行する
     *
     * 購読者へ通知した後、購読リストを空にし、基底クラスの削除処理を呼ぶ。
     * 最後に OnSlotRemoved で派生クラスへ知らせる。
     * 通知の中で弱参照から Lock() されて参照カウントが復活した要素は削除しない。
     *
     * @param handle 削除する要素のハンドル
     */
    void ExecuteRemoval(SlotHandle handle) {
        NotifySubscribers(handle.index);

        // 解放通知は1回きり。要素が復活しても購読は引き継がない
        if (handle.index < m_subscriptions.size()) {
            m_subscriptions[handle.index] = SlotSubscriptions{};
        }

        // 通知の中で削除済みになった、または弱参照から復活した要素は削除しない
        if (!this->IsValidHandle(handle)) return;
        if (this->HeaderAt(handle.index)->refCount != 0) return;

        ObjectSlotSystemBase<T>::RemoveInternal(handle);
        OnSlotRemoved(handle.index);
    }

    /**
     * @brief 通知中に遅延された削除をまとめて実行する
     *
     * 遅延中に再確保された（世代番号が変わった）要素と、
     * 弱参照から復活して参照カウントが0でなくなった要素は削除しない。
     * 実行中にさらに遅延が発生する可能性があるため、空になるまで繰り返す。
     */
    void ProcessPendingRemovals() {
        while (!m_pendingRemovals.empty()) {
            std::vector<SlotHandle> pending = std::move(m_pendingRemovals);
            m_pendingRemovals.clear();

            for (const SlotHandle& handle : pending) {
                if (!this->IsValidHandle(handle)) continue;
                if (this->HeaderAt(handle.index)->refCount != 0) continue;
                ExecuteRemoval(handle);
            }
        }
    }

    /** 通知ループの入れ子深さ */
    uint32_t m_notifyDepth = 0;

    /** 通知中に発生した削除待ち */
    std::vector<SlotHandle> m_pendingRemovals;

    /** タグ → 通知付きプール（型ごと） */
    static inline SignalSlotSystemBase* s_signalPoolByTag[ObjectSlotSystemBase<T>::TAG_COUNT] = {};
};
