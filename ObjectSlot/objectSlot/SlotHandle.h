#pragma once

#include <cstdint>
#include <functional>

/**
 * @brief オブジェクトプール内の要素を識別するためのハンドル
 *
 * インデックスと世代番号の組み合わせで要素を一意に識別する。
 * 世代番号により、削除済みスロットの再利用時に
 * 古いハンドルが無効であることを検出できる。
 */
struct SlotHandle {
    /** プール内のインデックス */
    uint32_t index = 0;

    /** 世代番号 (スロット再利用時にインクリメントされる) */
    uint32_t generation = 0;

    /**
     * @brief 等価比較演算子
     */
    bool operator==(const SlotHandle& other) const {
        return index == other.index && generation == other.generation;
    }

    /**
     * @brief 非等価比較演算子
     */
    bool operator!=(const SlotHandle& other) const {
        return !(*this == other);
    }

    /** 無効なインデックスを表す定数 */
    static constexpr uint32_t INVALID_INDEX = UINT32_MAX;

    /**
     * @brief ハンドルが有効かどうかを判定
     * @return インデックスが無効値でなければtrue
     */
    bool IsValid() const {
        return index != INVALID_INDEX;
    }

    /**
     * @brief 無効なハンドルを生成
     * @return 無効状態のSlotHandle
     */
    static SlotHandle Invalid() {
        return { INVALID_INDEX, 0 };
    }
};

/**
 * @brief std::unordered_map等でSlotHandleをキーとして使用するためのハッシュ特殊化
 */
namespace std {
    template<>
    struct hash<SlotHandle> {
        size_t operator()(const SlotHandle& h) const {
            return hash<uint64_t>()(
                (static_cast<uint64_t>(h.index) << 32) | h.generation
                );
        }
    };
}