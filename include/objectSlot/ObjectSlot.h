#pragma once

#include <cstdint>

// ============================================================
// 実装の切り替え
// ============================================================
// OBJECT_SLOT_USE_EXPERIMENTAL を定義してからこのヘッダを含めると、
// experimental_detail/ の新しい実装（4バイトのポインタ、見出し内蔵）が使われる。
// 定義しなければ detail/ の従来の実装が使われる。
//
// 定義する場所は次のどちらか。
//   - このヘッダを含める前に #define OBJECT_SLOT_USE_EXPERIMENTAL
//   - プロジェクトのプリプロセッサ定義（Visual Studio: プロパティ → C/C++ → プリプロセッサ）
//
// 新しい実装は「型ごとに4GBの仮想アドレスを予約する」ことを前提にしているため、
// OS の仮想メモリ機能が使える 64 ビット環境でしか動かない。
// それ以外の環境（WebAssembly、32 ビット）では、マクロが定義されていても
// 自動的に従来の実装に切り替わる。外側の API は同じなので利用側のコードは変わらない。
//
// 両実装は同じクラス名（SlotPtr など）を使うため、1つの実行ファイルの中で
// 混在させることはできない。翻訳単位ごとに定義を変えると ODR 違反になる。
//
// どちらの実装が動いているかは SlotTraits（SlotTraits.h）で問い合わせられる。
// 利用側のコードでこのマクロを直接分岐に使う必要はない。

/** 新しい実装が動く環境か（OS 仮想メモリが使える 64 ビット環境） */
#if !defined(__EMSCRIPTEN__) && (UINTPTR_MAX > 0xFFFFFFFFu) && (defined(_WIN32) || defined(__linux__) || defined(__APPLE__))
    #define OBJECT_SLOT_EXPERIMENTAL_SUPPORTED
#endif

#if defined(OBJECT_SLOT_USE_EXPERIMENTAL) && defined(OBJECT_SLOT_EXPERIMENTAL_SUPPORTED)
    #include "experimental_detail/ObjectSlotSystem.h"
    #include "experimental_detail/SignalSlotSystem.h"
    #include "experimental_detail/RefSlotSystem.h"
    #include "experimental_detail/SlotRef.h"
    #include "experimental_detail/SubscriptionRef.h"
    #include "experimental_detail/EnableSlotFromThis.h"
    #include "experimental_detail/SlotTraits.h"
#else
    #if defined(OBJECT_SLOT_USE_EXPERIMENTAL)
        #pragma message("[ObjectSlot] この環境では実験的な実装を使えないため、従来の実装に切り替えます。")
    #endif
    #include "detail/ObjectSlotSystem.h"
    #include "detail/SignalSlotSystem.h"
    #include "detail/RefSlotSystem.h"
    #include "detail/SlotRef.h"
    #include "detail/SubscriptionRef.h"
    #include "detail/EnableSlotFromThis.h"
    #include "detail/SlotTraits.h"
#endif
