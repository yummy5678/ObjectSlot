#pragma once

#include "ObjectSlotSystemBase.h"
#include <functional>
#include <algorithm>

// 前方宣言
template<typename T>
class SignalSlotPtr;

template<typename T>
class WeakSignalSlotPtr;

template<typename T>
class Subscription;

/**
 * @brief 通知機能付きオブジェクトプールの基底クラス
 *
 * ObjectSlotSystemBaseを継承し、購読パターンによる解放通知機能を追加する。
 * 要素が解放される際に、登録された全てのコールバックを登録の逆順に実行する。
 *
 *
 * 主な責任:
 * - 要素ごとの購読リスト管理
 * - 要素削除時の購読者への逆順通知
 * - 購読の追加・削除
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class SignalSlotSystemBase : public ObjectSlotSystemBase<T> {
    friend class SignalSlotPtr<T>;
    friend class WeakSignalSlotPtr<T>;
    friend class Subscription<T>;

public:
    /** 購読コールバックの型（引数なし） */
    using SubscriptionCallback = std::function<void()>;

    virtual ~SignalSlotSystemBase() = default;

    /// 全要素に通知した後、プールを初期化する
    using MemoryUsage = typename ObjectSlotSystemBase<T>::MemoryUsage;

    /// メモリ使用量の内訳を取得（購読リストの分を加える）
    MemoryUsage GetMemoryUsage() const {
        MemoryUsage usage = ObjectSlotSystemBase<T>::GetMemoryUsage();
        usage.subscriptionBytes = m_subscriptions.capacity() * sizeof(SlotSubscriptions);
        for (const auto& slotSubscriptions : m_subscriptions) {
            usage.subscriptionBytes += slotSubscriptions.entries.capacity() * sizeof(SubscriptionEntry);
        }
        return usage;
    }

    void Clear() {
        for (size_t i = 0; i < this->m_data.size(); ++i) {
            if (this->m_alive[i]) {
                NotifySubscribers(static_cast<uint32_t>(i));
            }
        }

        ObjectSlotSystemBase<T>::Clear();
        m_subscriptions.clear();
    }

    /// メモリを事前確保する（購読リストも含む）
    void Reserve(size_t capacity) {
        ObjectSlotSystemBase<T>::Reserve(capacity);
        if (capacity > m_subscriptions.size()) {
            m_subscriptions.reserve(capacity);
        }
    }

    /// 末尾の未使用スロットを解放する（購読リストも含む）
    void ShrinkToFit() {
        size_t oldSize = this->m_data.size();
        ObjectSlotSystemBase<T>::ShrinkToFit();
        size_t newSize = this->m_data.size();

        if (newSize < oldSize) {
            m_subscriptions.resize(newSize);
            m_subscriptions.shrink_to_fit();
        }
    }

protected:
    /**
     * @brief 購読エントリ
     *
     * 1つの購読を識別するIDとコールバックのペア。
     */
    struct SubscriptionEntry {
        /** 購読を識別する一意のID */
        uint32_t id = 0;

        /** 解放時に実行するコールバック */
        SubscriptionCallback callback;
    };

    /**
     * @brief 1つのスロットに紐づく購読リスト
     *
     * 次に発行するIDとエントリのリストを持つ。
     */
    struct SlotSubscriptions {
        /** 次に発行する購読ID */
        uint32_t nextId = 0;

        /** 購読エントリのリスト */
        std::vector<SubscriptionEntry> entries;
    };

    /**
     * @brief スロットを確保し、購読リストも初期化する
     *
     * 基底クラスのAllocateSlotを呼び出した後、
     * 対応するインデックスの購読リストを初期化する。
     *
     * @param obj 格納する要素（ムーブされる）
     * @return 確保されたスロットのハンドル
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
     * @brief 要素を削除する内部処理
     *
     * 購読者への逆順通知を実行した後、購読リストをクリアし、
     * 基底クラスの削除処理を呼び、最後に OnSlotRemoved で派生クラスへ知らせる。
     *
     * 通知の中で弱参照から Lock() されて参照カウントが復活した要素は削除しない。
     * また、通知の中で既に削除された（世代番号が進んだ）要素も二重に削除しない。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        NotifySubscribers(handle.index);

        if (!this->IsValidHandle(handle)) return;
        if (this->m_refCounts[handle.index] != 0) return;

        if (handle.index < m_subscriptions.size()) {
            m_subscriptions[handle.index] = SlotSubscriptions{};
        }
        ObjectSlotSystemBase<T>::RemoveInternal(handle);
        OnSlotRemoved(handle.index);
    }

    /**
     * @brief 要素が実際に削除された直後に呼ばれるフック
     *
     * 派生クラスが削除後の後処理を行いたい場合はここを上書きする。
     *
     * @param slotIndex 削除された要素のスロット添字
     */
    virtual void OnSlotRemoved(uint32_t slotIndex) {
        (void)slotIndex;
    }

    /**
     * @brief 購読を追加
     *
     * 指定スロットの購読リストにコールバックを追加し、
     * 一意の購読IDを返す。
     *
     * @param slotIndex 購読先のスロットインデックス
     * @param callback 解放時に実行するコールバック
     * @return 購読を識別するID
     */
    uint32_t AddSubscription(uint32_t slotIndex, SubscriptionCallback callback) {
        auto& subs = m_subscriptions[slotIndex];
        uint32_t id = subs.nextId++;
        subs.entries.push_back({ id, std::move(callback) });
        return id;
    }

    /**
     * @brief 購読を削除
     *
     * 指定スロットの購読リストから、指定IDの購読を削除する。
     * Subscriptionのデストラクタから呼ばれる。
     *
     * @param slotIndex 購読先のスロットインデックス
     * @param subscriptionId 削除する購読のID
     */
    void RemoveSubscription(uint32_t slotIndex, uint32_t subscriptionId) {
        if (slotIndex >= m_subscriptions.size()) return;
        auto& entries = m_subscriptions[slotIndex].entries;
        auto it = std::remove_if(entries.begin(), entries.end(),
            [subscriptionId](const SubscriptionEntry& entry) {
                return entry.id == subscriptionId;
            });
        entries.erase(it, entries.end());
    }


    /**
     * @brief 購読のコールバックを差し替え
     *
     * 指定IDの購読コールバックを新しい関数に更新する。
     * 購読IDは変更せず、実行される関数だけを入れ替える。
     *
     * 主な用途:
     * - ムーブ操作でキャプチャ済みのthisポインタを
     *   新しいインスタンスのアドレスに差し替える場合
     *
     * @param slotIndex 購読先のスロットインデックス
     * @param subscriptionId 差し替え対象の購読ID
     * @param newCallback 新しいコールバック関数
     */
    void UpdateSubscriptionCallback(uint32_t slotIndex, uint32_t subscriptionId, SubscriptionCallback newCallback) {
        if (slotIndex >= m_subscriptions.size()) return;
        auto& entries = m_subscriptions[slotIndex].entries;
        for (auto& entry : entries) {
            if (entry.id == subscriptionId) {
                entry.callback = std::move(newCallback);
                return;
            }
        }
    }

    /**
     * @brief 指定スロットの全購読者に逆順で通知
     *
     * コールバック実行中にリストが変更される可能性があるため、
     * リストをムーブしてから逆順に実行する。
     *
     * @param slotIndex 通知対象のスロットインデックス
     */
    void NotifySubscribers(uint32_t slotIndex) {
        if (slotIndex >= m_subscriptions.size()) return;

        auto entries = std::move(m_subscriptions[slotIndex].entries);
        m_subscriptions[slotIndex].entries.clear();

        for (auto it = entries.rbegin(); it != entries.rend(); ++it) {
            if (it->callback) {
                it->callback();
            }
        }
    }

    /**
     * @brief インデックス指定で購読を解除する（非テンプレート版）
     *
     * SubscriptionRefのデストラクタから呼ばれる。
     * 既存のRemoveSubscriptionに委譲する。
     */
    /// 解放通知を購読する（型消去された経路用）
    uint32_t SubscribeByIndex(uint32_t slotIndex, std::function<void()> callback) override {
        if (slotIndex >= this->m_alive.size() || !this->m_alive[slotIndex]) {
            return SlotControlBase::INVALID_SUBSCRIPTION_ID;
        }
        return AddSubscription(slotIndex, std::move(callback));
    }

    void RemoveSubscriptionByIndex(uint32_t slotIndex, uint32_t subscriptionId) override {
        RemoveSubscription(slotIndex, subscriptionId);
    }
    
    /**
     * @brief インデックス指定で購読コールバックを差し替える（非テンプレート版）
     *
     * SubscriptionRefのUpdateCallbackから呼ばれる。
     * 既存のUpdateSubscriptionCallbackに委譲する。
     */
    void UpdateSubscriptionCallbackByIndex(uint32_t slotIndex, uint32_t subscriptionId, std::function<void()> callback) override {
        UpdateSubscriptionCallback(slotIndex, subscriptionId, std::move(callback));
    }

    /** 各スロットの購読リスト */
    std::vector<SlotSubscriptions> m_subscriptions;
};