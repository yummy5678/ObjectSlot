#pragma once

#include "SlotControlBase.h"
#include "EnableSlotFromThis.h"
#include "thirdparty/rootVector/RootVector.h"
#include <type_traits>

// 前方宣言
template<typename T>
class SlotPtr;

template<typename T>
class WeakSlotPtr;

/**
 * @brief オブジェクトプールの基底クラス（軽量版）
 *
 * SlotControlBaseを継承し、型依存のデータストレージを追加する。
 * 同じ型のオブジェクトをメモリ上に連続配置して管理する。
 *
 * 主な特徴:
 * - メモリ連続配置によるキャッシュ効率の向上
 * - 世代番号による削除済みハンドルの検出
 * - 参照カウントによる自動削除
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
class ObjectSlotSystemBase : public SlotControlBase {
    friend class SlotPtr<T>;
    friend class WeakSlotPtr<T>;

public:
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
     * @brief インデックスから要素への直接ポインタを取得
     *
     * RootVectorのアドレスは固定のため、この戻り値は生涯有効。
     * SlotPtrの構築時にキャッシュポインタとして使用する。
     *
     * @param index スロットインデックス
     * @return 要素への直接ポインタ
     */
    T* GetPtrByIndex(uint32_t index) {
        return &m_data[index];
    }

    /**
     * @brief 生ポインタからスロットインデックスを算出
     *
     * @param rawPtr 要素への生ポインタ
     * @return スロットインデックス
     */
    uint32_t IndexFromRawPtr(void* rawPtr) const override {
        T* ptr = static_cast<T*>(rawPtr);
        return static_cast<uint32_t>(ptr - m_data.data());
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
        if (!m_data.is_initialized()) {
            m_data.Init(capacity);
        }
        m_data.reserve(capacity);

        m_generations.reserve(capacity);
        m_alive.reserve(capacity);
        m_refCounts.reserve(capacity);
    }

    /**
     * @brief 末尾の未使用スロットを解放してメモリを縮小
     */
    void ShrinkToFit() {
        // m_dataはshrink_to_fitでデコミット（RootVector側で対応）
        m_data.shrink_to_fit();

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

    /**
     * @brief m_dataの先頭アドレスを取得（SlotRef用）
     */
    T* DataPtr() { return m_data.data(); }

protected:
    /**
     * @brief 新しい要素用のスロットを確保
     *
     * @param obj 格納する要素 (ムーブされる)
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        SlotHandle handle;

        // RootVectorが未初期化なら自動初期化（デフォルト最大容量）
        if (!m_data.is_initialized()) {
            constexpr size_t DEFAULT_MAX_CAPACITY = 65536;
            size_t maxCap = (m_maxCapacity > 0) ? m_maxCapacity : DEFAULT_MAX_CAPACITY;
            m_data.Init(maxCap);
        }

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

            m_data.emplace_back(std::move(obj));
            m_generations.push_back(0);
            m_alive.push_back(true);
            m_refCounts.push_back(0);
        }

        if constexpr (std::is_base_of_v<EnableSlotFromThis<T>, T>) {
            m_data[handle.index].InitSlotFromThis(handle, this);
        }

        ++m_count;
        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        m_alive[handle.index] = false;
        ++m_generations[handle.index];
        m_refCounts[handle.index] = 0;

        m_data[handle.index] = T{};

        m_freeList.push(handle.index);
        --m_count;
    }

    /** 要素の連続配置ストレージ */
    RootVector<T> m_data;
};