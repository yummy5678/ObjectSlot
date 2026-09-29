#pragma once

#include <cstddef>
#include <cstdint>
#include "SlotHandle.h"

/**
 * @brief 従来実装（detail/）の特性をまとめた定数群
 *
 * 【責任】
 * - 実装ごとに異なる事実（ポインタのサイズ、対応機能）を一箇所で公開する
 * - 利用側が OBJECT_SLOT_USE_EXPERIMENTAL を直接見なくても、
 *   どちらの実装が動いているかを問い合わせられるようにする
 *
 * 【使用用途】
 * - テストやベンチマークが、実装に依存する期待値や表示をこの定数から取る
 * - 新実装側にも同名の構造体があり、同じメンバを持つ
 */
struct SlotTraits {
    SlotTraits() = delete;

    /** 実装の名前（表示用） */
    static constexpr const char* IMPLEMENTATION_NAME = "detail / 従来";

    /** 圧縮値（4バイトのタグ＋オフセット）を使う実装か */
    static constexpr bool IS_COMPACT = false;

    /** 1プールが使える領域のバイト数（共有領域方式のみ。それ以外は0） */
    static constexpr size_t REGION_BYTES = 0;

    /** Clear() / ShrinkToFit() の後に残った古いポインタを触っても安全か */
    static constexpr bool SUPPORTS_STALE_POINTER_SAFETY = false;

    /** 要素の型にデフォルトコンストラクタが必要か */
    static constexpr bool REQUIRES_DEFAULT_CONSTRUCTOR = true;

    /** 強参照（SlotPtr / SignalSlotPtr）のバイト数 */
    static constexpr size_t STRONG_POINTER_BYTES = sizeof(void*) * 2;

    /** 弱参照（WeakSlotPtr / WeakSignalSlotPtr）のバイト数（ハンドル8 ＋ プールポインタ。64ビットで16、32ビットで12） */
    static constexpr size_t WEAK_POINTER_BYTES = 8 + sizeof(void*);

    /** Subscription のバイト数（プールポインタ ＋ 添字4 ＋ ID4） */
    static constexpr size_t SUBSCRIPTION_BYTES = sizeof(void*) + 8;

    /** SubscriptionRef のバイト数（プールポインタ ＋ 添字4 ＋ ID4） */
    static constexpr size_t SUBSCRIPTION_REF_BYTES = sizeof(void*) + 8;

    /** 各要素の直前に置く見出しのバイト数（従来実装にはない） */
    static constexpr size_t HEADER_BYTES = 0;

    /** ObjectSlotSystem の要素1個あたりの管理データ（理論値） */
    static constexpr size_t OBJECT_POOL_OVERHEAD_BYTES = 4 + 4 + sizeof(void*) + 4 + 4 + 4;
    static constexpr const char* OBJECT_POOL_OVERHEAD_NOTE = "世代番号4 + 参照カウント4 + 本体の位置8 + 本体の添字4 + 生存一覧4 + 逆引き4 + 生死1bit";

    /** SignalSlotSystem の要素1個あたりの管理データ（理論値。vector の分は利用側で足す） */
    static constexpr size_t SIGNAL_POOL_OVERHEAD_BYTES = OBJECT_POOL_OVERHEAD_BYTES + 4;
    static constexpr const char* SIGNAL_POOL_OVERHEAD_NOTE = "上記 + 購読リスト（次ID4 + vector）";

    /** RefSlotSystem の要素1個あたりの管理データ（SignalSlotSystem の別名なので同じ） */
    static constexpr size_t REF_POOL_OVERHEAD_BYTES = SIGNAL_POOL_OVERHEAD_BYTES;
    static constexpr const char* REF_POOL_OVERHEAD_NOTE = "SignalSlotSystem と同じ（登録簿は不要）";

    /** RefSlotSystem が SignalSlotSystem とは別のプール型か（別名ではないか） */
    static constexpr bool HAS_SEPARATE_REF_POOL = false;
};
