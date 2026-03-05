#pragma once

#include "SlotHandle.h"
#include <vector>
#include <queue>
#include <cassert>

// 前方宣言
template<typename T>
class SlotPtr;

template<typename T>
class WeakSlotPtr;

/**
 * @brief オブジェクトプールの基底クラス（軽量版）
 *
 * 同じ型のオブジェクトをメモリ上に連続配置して管理する。
 * 参照カウント方式でオブジェクトのライフタイムを管理する。
 *
 * 主な特徴:
 * - メモリ連続配置によるキャッシュ効率の向上
 * - 世代番号による削除済みハンドルの検出
 * - 参照カウントによる自動削除
 *
 * 通知機能が必要な場合はSignalSlotBaseを使用すること。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystemBase {
    friend class SlotPtr<T>;
    friend class WeakSlotPtr<T>;

public:
    /**
     * @brief 仮想デストラクタ
     */
    virtual ~ObjectSlotSystemBase() = default;

    /**
     * @brief ハンドルから要素を取得
     *
     * @param handle 取得する要素のハンドル
     * @return 有効な場合は要素へのポインタ、無効な場合はnullptr
     */
    T* Get(SlotHandle handle) {
        if (!IsValidHandle(handle)) {
            return nullptr;
        }
        return &m_data[handle.index];
    }

    /**
     * @brief ハンドルから要素を取得 (const版)
     *
     * @param handle 取得する要素のハンドル
     * @return 有効な場合は要素へのconstポインタ、無効な場合はnullptr
     */
    const T* Get(SlotHandle handle) const {
        if (!IsValidHandle(handle)) {
            return nullptr;
        }
        return &m_data[handle.index];
    }

    /**
     * @brief ハンドルが有効かどうかを検証
     *
     * 以下の条件を全て満たす場合に有効と判定:
     * - インデックスが配列範囲内
     * - 要素が生存状態
     * - 世代番号が一致
     *
     * @param handle 検証するハンドル
     * @return 有効な場合はtrue
     */
    bool IsValidHandle(SlotHandle handle) const {
        if (handle.index >= m_data.size()) {
            return false;
        }
        if (!m_alive[handle.index]) {
            return false;
        }
        if (m_generations[handle.index] != handle.generation) {
            return false;
        }
        return true;
    }

    /**
     * @brief 指定ハンドルの参照カウントを取得
     *
     * @param handle 対象のハンドル
     * @return 参照カウント、無効な場合は0
     */
    uint32_t GetRefCount(SlotHandle handle) const {
        if (!IsValidHandle(handle)) {
            return 0;
        }
        return m_refCounts[handle.index];
    }

    /**
     * @brief 有効な要素数を取得
     * @return 現在プール内に存在する有効な要素の数
     */
    size_t Count() const {
        return m_count;
    }

    /**
     * @brief プールの総容量を取得
     * @return 確保済みスロット数 (削除済み含む)
     */
    size_t Capacity() const {
        return m_data.size();
    }

    /**
     * @brief 最大容量を設定
     *
     * 0を指定すると無制限になる。
     *
     * @param maxCapacity 最大容量 (0で無制限)
     */
    void SetMaxCapacity(size_t maxCapacity) {
        m_maxCapacity = maxCapacity;
    }

    /**
     * @brief 最大容量を取得
     * @return 最大容量 (0は無制限)
     */
    size_t GetMaxCapacity() const {
        return m_maxCapacity;
    }

    /**
     * @brief 新しい要素を追加可能か判定
     * @return 追加可能ならtrue
     */
    bool CanCreate() const {
        if (m_maxCapacity == 0) {
            return true;
        }
        return m_count < m_maxCapacity;
    }

    /**
     * @brief 全ての有効な要素に対して処理を実行
     *
     * @tparam Func 関数オブジェクトの型
     * @param func 各要素に適用する関数 (SlotHandle, T&) を受け取る
     */
    template<typename Func>
    void ForEach(Func&& func) {
        for (size_t i = 0; i < m_data.size(); ++i) {
            if (m_alive[i]) {
                SlotHandle h{ static_cast<uint32_t>(i), m_generations[i] };
                func(h, m_data[i]);
            }
        }
    }

    /**
     * @brief 全ての有効な要素に対して処理を実行 (const版)
     *
     * @tparam Func 関数オブジェクトの型
     * @param func 各要素に適用する関数 (SlotHandle, const T&) を受け取る
     */
    template<typename Func>
    void ForEach(Func&& func) const {
        for (size_t i = 0; i < m_data.size(); ++i) {
            if (m_alive[i]) {
                SlotHandle h{ static_cast<uint32_t>(i), m_generations[i] };
                func(h, m_data[i]);
            }
        }
    }

    /**
     * @brief プール内の全要素を削除
     *
     * @warning 残っているSlotPtrは無効になる
     */
    void Clear() {
        m_data.clear();
        m_generations.clear();
        m_alive.clear();
        m_refCounts.clear();
        m_freeList = std::queue<uint32_t>();
        m_count = 0;
    }

    /**
     * @brief 指定した数の要素分のメモリを事前確保
     *
     * @param capacity 確保する要素数
     */
    void Reserve(size_t capacity) {
        if (capacity > m_data.size()) {
            m_data.reserve(capacity);
            m_generations.reserve(capacity);
            m_alive.reserve(capacity);
            m_refCounts.reserve(capacity);
        }
    }

    /**
     * @brief 末尾の未使用スロットを解放してメモリを縮小
     *
     * 末尾から連続する削除済みスロットを解放する。
     * 有効な要素のインデックスは変わらないため、
     * 既存のSlotHandleは有効なまま。
     *
     * @note フリーリストの再構築が必要なため、
     *       大量の要素がある場合はコストがかかる
     */
    void ShrinkToFit() {
        size_t newSize = m_data.size();
        while (newSize > 0 && !m_alive[newSize - 1]) {
            --newSize;
        }

        if (newSize == m_data.size()) {
            return;
        }

        m_data.resize(newSize);
        m_data.shrink_to_fit();

        m_generations.resize(newSize);
        m_generations.shrink_to_fit();

        m_alive.resize(newSize);
        m_alive.shrink_to_fit();

        m_refCounts.resize(newSize);
        m_refCounts.shrink_to_fit();

        std::queue<uint32_t> newFreeList;
        while (!m_freeList.empty()) {
            uint32_t index = m_freeList.front();
            m_freeList.pop();
            if (index < newSize) {
                newFreeList.push(index);
            }
        }
        m_freeList = std::move(newFreeList);
    }

protected:
    /**
     * @brief 新しい要素用のスロットを確保
     *
     * @param obj 格納する要素 (ムーブされる)
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        SlotHandle handle;

        if (!m_freeList.empty()) {
            handle.index = m_freeList.front();
            m_freeList.pop();
            handle.generation = m_generations[handle.index];

            m_data[handle.index] = std::move(obj);
            m_alive[handle.index] = true;
            m_refCounts[handle.index] = 0;
        }
        else {
            handle.index = static_cast<uint32_t>(m_data.size());
            handle.generation = 0;

            m_data.push_back(std::move(obj));
            m_generations.push_back(0);
            m_alive.push_back(true);
            m_refCounts.push_back(0);
        }

        ++m_count;
        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * 要素をデフォルト状態にリセットしてメンバのデストラクタを発動させた後、
     * スロットを再利用可能にする。
     * 世代番号をインクリメントして古いハンドルを無効化する。
     *
     * @param handle 削除する要素のハンドル
     */
    virtual void RemoveInternal(SlotHandle handle) {
        m_alive[handle.index] = false;
        ++m_generations[handle.index];
        m_refCounts[handle.index] = 0;

        // 要素をデフォルト状態にリセット
        // （メンバのデストラクタを実質的に発動させる）
        m_data[handle.index] = T{};

        m_freeList.push(handle.index);
        --m_count;
    }

    /**
     * @brief 参照カウントを増加
     * @param handle 対象のハンドル
     */
    void AddRef(SlotHandle handle) {
        if (IsValidHandle(handle)) {
            ++m_refCounts[handle.index];
        }
    }

    /**
     * @brief 参照カウントを減少
     *
     * カウントが0になった場合、要素を削除する。
     *
     * @param handle 対象のハンドル
     */
    void ReleaseRef(SlotHandle handle) {
        if (IsValidHandle(handle)) {
            assert(m_refCounts[handle.index] > 0);
            --m_refCounts[handle.index];

            if (m_refCounts[handle.index] == 0) {
                RemoveInternal(handle);
            }
        }
    }

    /** 要素の連続配置ストレージ */
    std::vector<T> m_data;

    /** 各スロットの世代番号 */
    std::vector<uint32_t> m_generations;

    /** 各スロットの生存フラグ */
    std::vector<bool> m_alive;

    /** 各スロットの参照カウント */
    std::vector<uint32_t> m_refCounts;

    /** 再利用可能なスロットのインデックス */
    std::queue<uint32_t> m_freeList;

    /** 有効な要素数 */
    size_t m_count = 0;

    /** 最大容量 (0は無制限) */
    size_t m_maxCapacity = 0;
};