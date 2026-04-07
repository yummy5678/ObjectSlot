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
 * root_vectorにより要素をメモリ上に連続配置して管理する。
 * ネイティブ環境では要素のアドレスが生涯変わらない。
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
     */
    T* Get(SlotHandle handle) {
        if (!IsValidHandle(handle)) return nullptr;
        return &m_data.get(handle.index);
    }

    /**
     * @brief ハンドルから要素を取得 (const版)
     */
    const T* Get(SlotHandle handle) const {
        if (!IsValidHandle(handle)) return nullptr;
        return &m_data.get(handle.index);
    }

    /**
     * @brief インデックスから要素への安定ポインタを取得
     *
     * ネイティブ環境ではゼロコストの生ポインタ、
     * フォールバック環境ではインデックス経由の安定ポインタを返す。
     * SlotPtr/SignalSlotPtrの内部で使用する。
     *
     * @param index スロットインデックス
     * @return 要素への安定ポインタ
     */
    typename root_vector<T>::root_pointer GetRootPointer(uint32_t index) {
        return m_data.get_root_pointer(static_cast<size_t>(index));
    }

    /**
     * @brief 生ポインタからスロットインデックスを算出
     */
    uint32_t IndexFromRawPtr(void* rawPtr) const override {
        T* ptr = static_cast<T*>(rawPtr);
        return static_cast<uint32_t>(ptr - m_data.data());
    }

    /**
     * @brief m_dataの先頭アドレスを取得（インデックス算出用）
     */
    T* DataPtr() { return m_data.data(); }

    /// m_dataの先頭アドレスを取得（const版）
    const T* DataPtr() const { return m_data.data(); }

    /**
     * @brief 全ての有効な要素に対して処理を実行
     */
    template<typename Func>
    void ForEach(Func&& func) {
        for (size_t i = 0; i < m_data.size(); ++i) {
            if (m_alive[i]) {
                SlotHandle h{ static_cast<uint32_t>(i), m_generations[i] };
                func(h, m_data.get(i));
            }
        }
    }

    /**
     * @brief 全ての有効な要素に対して処理を実行 (const版)
     */
    template<typename Func>
    void ForEach(Func&& func) const {
        for (size_t i = 0; i < m_data.size(); ++i) {
            if (m_alive[i]) {
                SlotHandle h{ static_cast<uint32_t>(i), m_generations[i] };
                func(h, m_data.get(i));
            }
        }
    }

    /**
     * @brief プール内の全要素を削除
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
     */
    void Reserve(size_t capacity) {
        m_data.reserve(capacity);
        m_generations.reserve(capacity);
        m_alive.reserve(capacity);
        m_refCounts.reserve(capacity);
    }

    /**
     * @brief 末尾の未使用スロットを解放してメモリを縮小
     */
    void ShrinkToFit() {
        size_t newSize = m_data.size();
        while (newSize > 0 && !m_alive[newSize - 1]) {
            --newSize;
        }

        if (newSize == m_data.size()) return;

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
     * フリーリストにスロットがある場合はplacement newで再構築し、
     * ない場合はpush_backで末尾に追加する。
     * フリーリストのスロットはRemoveInternalでデストラクタ呼び出し済みのため、
     * ムーブ代入ではなくplacement newで構築する必要がある。
     *
     * @param obj 格納する要素（ムーブされる）
     * @return 確保されたスロットのハンドル
     */
    SlotHandle AllocateSlot(T&& obj) {
        SlotHandle handle;

        if (!m_freeList.empty()) {
            handle.index = m_freeList.front();
            m_freeList.pop();
            handle.generation = m_generations[handle.index];

            new (&m_data.get(handle.index)) T(std::move(obj));
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

        if constexpr (std::is_base_of_v<EnableSlotFromThis<T>, T>) {
            m_data.get(handle.index).InitSlotFromThis(handle, this);
        }

        ++m_count;
        return handle;
    }

    /**
     * @brief 要素を削除する内部処理
     *
     * 要素のデストラクタを呼び出してスロットを無効化する。
     * デフォルトTの構築は行わない（次のAllocateSlotでplacement newするため）。
     *
     * @param handle 削除する要素のハンドル
     */
    void RemoveInternal(SlotHandle handle) override {
        m_alive[handle.index] = false;
        ++m_generations[handle.index];
        m_refCounts[handle.index] = 0;

        m_data.get(handle.index).~T();

        m_freeList.push(handle.index);
        --m_count;
    }

    /** 要素の連続配置ストレージ（ネイティブ環境ではアドレス不変） */
    root_vector<T> m_data;
};