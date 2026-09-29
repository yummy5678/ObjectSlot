#pragma once

#include <cstdint>
#include <cstddef>

/**
 * @brief 圧縮値（4バイトのスロット識別子）の構成を定義する定数群
 *
 * 【責任】
 * - 圧縮値のビット配置（タグ4ビット＋オフセット28ビット）を一箇所で定義する
 * - タグとオフセットの合成・分解を提供する
 *
 * 【圧縮値とは】
 * SlotPtr等が持つ4バイトの値で、「どのプールの、どの位置か」を表す。
 * 上位4ビットがプールを識別するタグ、下位28ビットがプール内のバイトオフセット。
 * 値の解釈（実アドレスへの変換）はSlotStorageが環境ごとに行う。
 *
 * 【使用用途】
 * - SlotStorage、各ポインタ型、プールが共通で参照する
 */
struct SlotValue {
    SlotValue() = delete;

    /** プール内オフセットに使うビット数（2^28 = 256MB） */
    static constexpr uint32_t OFFSET_BITS = 28;

    /** タグに使うビット数（32 - 28 = 4） */
    static constexpr uint32_t TAG_BITS = 32 - OFFSET_BITS;

    /** タグが取りうる値の個数（16） */
    static constexpr uint32_t TAG_COUNT = 1u << TAG_BITS;

    /** 「タグ未設定」を表す値（この番号はプールに割り当てない） */
    static constexpr uint8_t INVALID_TAG = static_cast<uint8_t>(TAG_COUNT - 1);

    /** オフセット部分を取り出すマスク */
    static constexpr uint32_t OFFSET_MASK = (1u << OFFSET_BITS) - 1;

    /** 1プールが使えるバイト数の上限（256MB） */
    static constexpr size_t REGION_BYTES = static_cast<size_t>(1) << OFFSET_BITS;

    /** 「無効」を表す圧縮値（タグ15の末尾。決して有効なスロットにならない） */
    static constexpr uint32_t INVALID = UINT32_MAX;

    /// タグとオフセットから圧縮値を合成する
    static constexpr uint32_t Make(uint8_t tag, uint32_t offset) {
        return (static_cast<uint32_t>(tag) << OFFSET_BITS) | (offset & OFFSET_MASK);
    }

    /// 圧縮値からタグを取り出す
    static constexpr uint8_t Tag(uint32_t value) {
        return static_cast<uint8_t>(value >> OFFSET_BITS);
    }

    /// 圧縮値からプール内オフセットを取り出す
    static constexpr uint32_t Offset(uint32_t value) {
        return value & OFFSET_MASK;
    }
};

/**
 * @brief 要素ごとの管理情報（見出し）
 *
 * 【責任】
 * - 要素1つ分の参照カウントと世代番号を保持する
 * - 要素の位置（圧縮値）と所属プール（全体番号）を保持し、
 *   型を知らないSlotRefからでも参照カウント操作と削除通知を可能にする
 *
 * 【どこに置かれるか】
 * 要素本体とは別の並び（SlotStorage の見出し領域）に、本体と同じ添字で置かれる。
 * ポインタが持つ圧縮値から「基底＋定数＋添字×16」で届くため、
 * コピー・破棄・弱参照の確認をプール本体に触れずに行える。
 * 本体の並びに見出しが挟まらないので、要素を順に読む処理は見出しを読み飛ばさずに済む。
 *
 * 【使用用途】
 * - SlotStorageが要素と対で確保する。利用者が直接触ることはない
 */
struct SlotHeader {
    /** 参照カウント（0なら空きスロットか削除待ち） */
    uint32_t refCount = 0;

    /** 世代番号（削除のたびに増える。古い弱参照やハンドルの検出用） */
    uint32_t generation = 0;

    /** このスロット自身を表す圧縮値（所属プールへ位置を伝えるため） */
    uint32_t selfValue = SlotValue::INVALID;

    /** 所属プールの全体番号（SlotControlBase::PoolFromId で引く） */
    uint32_t poolId = UINT32_MAX;
};

static_assert(sizeof(SlotHeader) == 16, "SlotHeaderは16バイトである前提でレイアウトを決めている。");
