#pragma once

#include "SignalSlotSystem.h"
#include "SlotRef.h"

/**
 * @brief SignalSlotSystem の別名（互換用）
 *
 * 以前は SlotRef を使うために専用のプール（登録簿付き）が必要だったが、
 * SlotRef がスロット番号で要素を追うようになり、どのプールの要素でも指せるようになった。
 * 既存コードのために名前だけ残している。新しいコードでは SignalSlotSystem を直接使うこと。
 *
 * @tparam T 管理する要素の型
 */
template<typename T>
using RefSlotSystem = SignalSlotSystem<T>;
