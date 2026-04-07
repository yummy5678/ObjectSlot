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
 * 通知チェーン中の安全性:
 * - コールバック内で別の購読者が解除されても、キャンセルフラグにより安全にスキップされる
 * - コールバック内で参照カウントが0になるオブジェクトの削除は、通知完了後に遅延実行される
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
     * キャンセル済みフラグにより、通知ループ中に解除された購読を
     * 安全にスキップできる。
     */
    struct SubscriptionEntry {
        /** 購読を識別する一意のID */
        uint32_t id = 0;

        /** 解放時に実行するコールバック */
        SubscriptionCallback callback;

        /** 通知ループ中に解除された場合にtrueになる */
        bool cancelled = false;
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
     * 通知ループ中に参照カウントが0になった場合は
     * 削除を遅延キューに追加し、通知完了後にまとめて実行する。
     * これにより再帰的なRemoveInternalの呼び出しを防止する。
     *
     * 通常の呼び出し時はExecuteRemovalに委譲し、
     * 購読者への逆順通知→購読リストクリア→基底の削除処理の順に実行する。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        // 通知ループ中なら削除を遅延させる
        if (m_notifyDepth > 0) {
            m_pendingRemovals.push_back(handle);
            return;
        }

        ExecuteRemoval(handle);
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
        subs.entries.push_back({ id, std::move(callback), false });
        return id;
    }

    /**
     * @brief 購読を削除
     *
     * 通知ループ中の場合はキャンセルフラグを立てるだけで、
     * 実際の削除は通知完了後に行われる。
     * 通知ループ外では即座にリストから削除する。
     *
     * Subscriptionのデストラクタから呼ばれる。
     *
     * @param slotIndex 購読先のスロットインデックス
     * @param subscriptionId 削除する購読のID
     */
    void RemoveSubscription(uint32_t slotIndex, uint32_t subscriptionId) {
        if (slotIndex >= m_subscriptions.size()) return;
        auto& entries = m_subscriptions[slotIndex].entries;

        if (m_notifyDepth > 0) {
            // 通知ループ中はキャンセルフラグを立てるだけ
            for (auto& entry : entries) {
                if (entry.id == subscriptionId) {
                    entry.cancelled = true;
                    return;
                }
            }
        }
        else {
            // 通知ループ外では即座に削除
            auto it = std::remove_if(entries.begin(), entries.end(),
                [subscriptionId](const SubscriptionEntry& entry) {
                    return entry.id == subscriptionId;
                });
            entries.erase(it, entries.end());
        }
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
     * 通知深度カウンタにより、コールバック内で発生する
     * 他のオブジェクトの削除を遅延させる。
     * 各コールバック呼び出し前にキャンセルフラグを確認し、
     * 通知ループ中に解除された購読は安全にスキップする。
     *
     * コールバック中に同じスロットへ新しい購読が追加された場合、
     * ベクタの再アロケーションでイテレータが無効化される危険があるため、
     * ループ開始時のサイズをキャプチャし、インデックスベースで逆順走査する。
     * 通知中に追加された購読は今回の通知では実行されない。
     *
     * 通知完了後、キャンセル済みエントリを一括削除し、
     * 遅延された削除処理をまとめて実行する。
     *
     * @param slotIndex 通知対象のスロットインデックス
     */
    void NotifySubscribers(uint32_t slotIndex) {
        if (slotIndex >= m_subscriptions.size()) return;

        auto& subs = m_subscriptions[slotIndex];
        if (subs.entries.empty()) return;

        // 通知深度を増加（リエントランシー検出用）
        ++m_notifyDepth;

        // ループ開始時のサイズをキャプチャ（通知中の追加分は対象外）
        // インデックスベースの逆順走査でイテレータ無効化を回避する
        const size_t count = subs.entries.size();
        for (size_t i = count; i > 0; --i) {
            auto& entry = subs.entries[i - 1];
            if (!entry.cancelled && entry.callback) {
                entry.callback();
            }
        }

        // 通知深度を減少
        --m_notifyDepth;

        // キャンセル済みエントリを一括削除
        auto newEnd = std::remove_if(subs.entries.begin(), subs.entries.end(),
            [](const SubscriptionEntry& entry) {
                return entry.cancelled;
            });
        subs.entries.erase(newEnd, subs.entries.end());

        // 最外の通知ループが完了したら遅延削除を実行
        if (m_notifyDepth == 0) {
            ProcessPendingRemovals();
        }
    }

    /**
     * @brief インデックス指定で購読を解除する（非テンプレート版）
     *
     * SubscriptionRefのデストラクタから呼ばれる。
     * 既存のRemoveSubscriptionに委譲する。
     */
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

private:
    /**
     * @brief 実際の削除処理を実行する
     *
     * 購読者への逆順通知を実行した後、
     * 購読リストをクリアし、基底クラスの削除処理を呼ぶ。
     *
     * @param handle 削除する要素のハンドル
     */
    void ExecuteRemoval(SlotHandle handle) {
        NotifySubscribers(handle.index);
        if (handle.index < m_subscriptions.size()) {
            m_subscriptions[handle.index] = SlotSubscriptions{};
        }
        ObjectSlotSystemBase<T>::RemoveInternal(handle);
    }

    /**
     * @brief 遅延された削除処理をまとめて実行する
     *
     * 通知ループ中にRemoveInternalが呼ばれた場合、
     * 削除対象はm_pendingRemovalsに蓄積される。
     * 最外の通知ループ完了後にこの関数が呼ばれ、
     * 蓄積された削除を順次実行する。
     *
     * 遅延削除の実行中にさらに遅延削除が発生する可能性があるため、
     * キューが空になるまでループする。
     */
    void ProcessPendingRemovals() {
        while (!m_pendingRemovals.empty()) {
            // ローカルにムーブしてからループ
            // （ExecuteRemoval中に新たなpending追加が起きても安全）
            auto pending = std::move(m_pendingRemovals);
            m_pendingRemovals.clear();

            for (auto& handle : pending) {
                // 既に削除済みかもしれないので検証する
                if (this->IsValidHandle(handle)) {
                    ExecuteRemoval(handle);
                }
            }
        }
    }

    /** 通知ループのネスト深度（0なら通知中でない） */
    uint32_t m_notifyDepth = 0;

    /** 通知ループ中に発生した遅延削除キュー */
    std::vector<SlotHandle> m_pendingRemovals;
};