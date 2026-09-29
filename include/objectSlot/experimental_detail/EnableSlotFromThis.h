#pragma once

#include "SlotHeader.h"
#include "SlotStorage.h"

template<typename T> class ObjectSlotSystemBase;
template<typename T> class SlotPtr;
template<typename T> class SignalSlotPtr;
template<typename T> class WeakSlotPtr;
template<typename T> class WeakSignalSlotPtr;

/**
 * @class EnableSlotFromThis
 * @brief 要素の内部から自分自身へのポインタを取得できるようにする基底クラス
 *
 * 【責任】
 * - プールが要素を生成した時に、その要素自身の圧縮値を受け取って保持する
 * - 保持した圧縮値から SlotPtr / SignalSlotPtr / 弱参照を作る
 *
 * 【使用用途】
 * - std::enable_shared_from_this と同じ目的。要素の型 T が
 *   EnableSlotFromThis<T> を継承していると、プールが自動的に初期化する
 * - SlotPtrFromThis() は ObjectSlotSystem に置いた要素で、
 *   SignalSlotPtrFromThis() は SignalSlotSystem に置いた要素で使う
 *
 * 【注意事項】
 * - コンストラクタの中では、まだプールから圧縮値を受け取っていないため無効なポインタが返る
 * - コピー・ムーブしても圧縮値は引き継がない（要素ごとに位置が違うため）
 * - 持つのは4バイトの圧縮値だけで、仮想関数表は持たない
 *
 * @tparam T 要素の型（継承する側の型）
 */
template<typename T>
class EnableSlotFromThis
{
    friend class ObjectSlotSystemBase<T>;

public:
    /// 無効な状態で生成する
    EnableSlotFromThis() = default;

    /// コピーしても自分の位置は引き継がない
    EnableSlotFromThis(const EnableSlotFromThis&) {}

    /// ムーブしても自分の位置は引き継がない
    EnableSlotFromThis(EnableSlotFromThis&&) noexcept {}

    /// 代入しても自分の位置は変えない
    EnableSlotFromThis& operator=(const EnableSlotFromThis&) { return *this; }

    /// ムーブ代入しても自分の位置は変えない
    EnableSlotFromThis& operator=(EnableSlotFromThis&&) noexcept { return *this; }

protected:
    /// 基底クラス経由での破棄は想定しないため、仮想デストラクタは持たない
    ~EnableSlotFromThis() = default;

    /// 自分自身への SlotPtr を取得（無効なら空のポインタ）
    SlotPtr<T> SlotPtrFromThis() const {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return SlotPtr<T>();
        ++header->refCount;
        return SlotPtr<T>(m_selfValue);
    }

    /// 自分自身への SignalSlotPtr を取得（無効なら空のポインタ）
    SignalSlotPtr<T> SignalSlotPtrFromThis() const {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return SignalSlotPtr<T>();
        ++header->refCount;
        return SignalSlotPtr<T>(m_selfValue);
    }

    /// 自分自身への WeakSlotPtr を取得（無効なら空の弱参照）
    WeakSlotPtr<T> WeakSlotPtrFromThis() const {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return WeakSlotPtr<T>();
        return WeakSlotPtr<T>(m_selfValue, header->generation);
    }

    /// 自分自身への WeakSignalSlotPtr を取得（無効なら空の弱参照）
    WeakSignalSlotPtr<T> WeakSignalSlotPtrFromThis() const {
        SlotHeader* header = LiveHeader();
        if (header == nullptr) return WeakSignalSlotPtr<T>();
        return WeakSignalSlotPtr<T>(m_selfValue, header->generation);
    }

private:
    /// プールが生成時に呼び、自分の圧縮値を設定する
    void InitSlotFromThis(uint32_t selfValue) {
        m_selfValue = selfValue;
    }

    /// 自分の見出しを取得（未初期化、または参照されていない状態なら nullptr）
    SlotHeader* LiveHeader() const {
        if (m_selfValue == SlotValue::INVALID) return nullptr;
        SlotHeader* header = SlotStorage<T>::HeaderAt(m_selfValue);
        if (header->refCount == 0) return nullptr;
        return header;
    }

    /** 自分自身を表す圧縮値（未初期化なら INVALID） */
    uint32_t m_selfValue = SlotValue::INVALID;
};
