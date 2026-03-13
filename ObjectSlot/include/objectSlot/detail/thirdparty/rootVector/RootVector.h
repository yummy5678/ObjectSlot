#pragma once

#include "VirtualMemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <new>
#include <type_traits>
#include <utility>
#include <iterator>
#include <initializer_list>
#include <stdexcept>
#include <algorithm>

/**
 * @class RootVector
 * @brief 仮想アドレスを固定したまま要素を追加できるコンテナ
 *
 * 【責任】
 * - 構築時に仮想アドレス空間を予約し、要素追加に応じて物理メモリをコミットする
 * - 全環境で要素へのポインタの安定性を保証する
 * - std::vectorと同等のインデックスアクセス・イテレーションを提供する
 * - 要素の構築/破棄をplacement new/デストラクタ呼び出しで正しく行う
 *
 * 【使用用途】
 * - std::vectorの代替として、要素のポインタ安定性が必要な場面で使用する
 * - 再アロケーションが発生しないため、格納した要素のアドレスを外部に安全に公開できる
 * - 要素への生ポインタや参照を長期間保持する設計で、ポインタ無効化を防ぎたい場合に適する
 *
 * 【std::vectorとの主な違い】
 * - 使用前にInit()で最大要素数を指定する必要がある（後から拡張不可）
 * - 単一引数コンストラクタはmax_capacityの予約のみ行い、要素を構築しない
 *   std::vector(n) と同じ動作にはresize(n) を続けて呼ぶこと
 * - insert/eraseは要素を移動するため、移動先アドレスに以前と異なる
 *   オブジェクトが配置される点に注意（アドレス自体は不変だが内容が変わる）
 *
 * 【その他の注意事項】
 * - 構築時に指定した最大要素数を超えての拡張はできない（論理上限として管理）
 * - コピー禁止（仮想アドレス空間を共有すると二重解放になるため）
 * - フォールバック環境では最大要素数分のメモリが起動時に全量確保される
 *
 * @tparam T 格納する要素の型
 */
template<typename T>
class RootVector
{
public:
	// ================================================================
	// 型定義
	// ================================================================

	using value_type      = T;
	using size_type       = size_t;
	using difference_type = ptrdiff_t;
	using reference       = T&;
	using const_reference = const T&;
	using pointer         = T*;
	using const_pointer   = const T*;
	using iterator        = T*;
	using const_iterator  = const T*;
	using reverse_iterator       = std::reverse_iterator<iterator>;
	using const_reverse_iterator = std::reverse_iterator<const_iterator>;

	// ================================================================
	// コンストラクタ / デストラクタ
	// ================================================================

	/**
	 * @brief デフォルトコンストラクタ（未初期化状態で生成）
	 *
	 * この状態ではメモリは確保されない。
	 * 使用前にInit()を呼んで仮想アドレス空間を予約する必要がある。
	 */
	RootVector() = default;

	/**
	 * @brief 最大要素数を指定して初期化するコンストラクタ
	 *
	 * 指定された最大要素数分の仮想アドレス空間を予約する。
	 * ネイティブ環境ではこの時点で物理メモリは消費しない。
	 *
	 * @note std::vector(n)とは異なり、要素の構築は行わない。
	 *       要素を構築するにはresize(n)を続けて呼ぶこと。
	 *
	 * @param maxElementCount 格納可能な最大要素数
	 */
	explicit RootVector(size_type maxElementCount)
	{
		Init(maxElementCount);
	}

	/**
	 * @brief 最大要素数を指定し、指定個数の要素を値で初期化するコンストラクタ
	 *
	 * maxElementCount分の仮想アドレス空間を予約した後、
	 * count個の要素をvalueのコピーで構築する。
	 *
	 * @param maxElementCount 格納可能な最大要素数
	 * @param count 初期構築する要素数
	 * @param value 各要素の初期値
	 */
	RootVector(size_type maxElementCount, size_type count, const T& value)
	{
		assert(count <= maxElementCount && "初期要素数が最大要素数を超えています。");
		Init(maxElementCount);
		EnsureCommitted(count);
		for (size_type i = 0; i < count; ++i)
		{
			new (&m_basePtr[i]) T(value);
		}
		m_size = count;
	}

	/**
	 * @brief 初期化リストで初期化するコンストラクタ
	 *
	 * リストの要素数をmax_capacityとして予約し、全要素をコピー構築する。
	 * 構築後にpush_backで追加する余地はない。
	 * 追加の余地が必要な場合はInit()で大きめに予約してからassign()を使うこと。
	 *
	 * @param init 初期化リスト
	 */
	RootVector(std::initializer_list<T> init)
	{
		Init(init.size());
		EnsureCommitted(init.size());
		size_type i = 0;
		for (const auto& v : init)
		{
			new (&m_basePtr[i]) T(v);
			++i;
		}
		m_size = init.size();
	}

	/**
	 * @brief イテレータ範囲で初期化するコンストラクタ
	 *
	 * [first, last) の要素数をmax_capacityとして予約し、全要素をコピー構築する。
	 * 構築後にpush_backで追加する余地はない。
	 * 追加の余地が必要な場合はInit()で大きめに予約してからassign()を使うこと。
	 *
	 * @tparam InputIt 入力イテレータの型
	 * @param first 範囲の先頭
	 * @param last 範囲の終端
	 */
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	RootVector(InputIt first, InputIt last)
	{
		const size_type count = static_cast<size_type>(std::distance(first, last));
		Init(count);
		EnsureCommitted(count);
		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
		{
			new (&m_basePtr[i]) T(*it);
		}
		m_size = count;
	}

	/**
	 * @brief デストラクタ
	 *
	 * 全ての構築済み要素のデストラクタを呼び出した後、
	 * 予約した仮想アドレス空間を全て解放する。
	 */
	~RootVector()
	{
		if (m_basePtr)
		{
			DestroyRange(0, m_size);
			VirtualMemoryAllocator::Release(m_basePtr, m_reservedBytes);
		}
	}

	// コピー禁止（仮想アドレス空間の共有は危険）
	RootVector(const RootVector&) = delete;
	RootVector& operator=(const RootVector&) = delete;

	/**
	 * @brief ムーブコンストラクタ
	 *
	 * 移動元の仮想アドレス空間の所有権を引き継ぐ。
	 * 移動元は未初期化状態に戻る。
	 * 要素のコピーやアドレス変更は一切発生しない。
	 *
	 * @param other 移動元のRootVector
	 */
	RootVector(RootVector&& other) noexcept
		: m_basePtr(other.m_basePtr)
		, m_size(other.m_size)
		, m_committedBytes(other.m_committedBytes)
		, m_maxCount(other.m_maxCount)
		, m_reservedBytes(other.m_reservedBytes)
	{
		other.m_basePtr        = nullptr;
		other.m_size           = 0;
		other.m_committedBytes = 0;
		other.m_maxCount       = 0;
		other.m_reservedBytes  = 0;
	}

	/**
	 * @brief ムーブ代入演算子
	 *
	 * 自身が保持する仮想アドレス空間を解放してから、
	 * 移動元の所有権を引き継ぐ。
	 *
	 * @param other 移動元のRootVector
	 * @return 自身への参照
	 */
	RootVector& operator=(RootVector&& other) noexcept
	{
		if (this != &other)
		{
			if (m_basePtr)
			{
				DestroyRange(0, m_size);
				VirtualMemoryAllocator::Release(m_basePtr, m_reservedBytes);
			}

			m_basePtr        = other.m_basePtr;
			m_size           = other.m_size;
			m_committedBytes = other.m_committedBytes;
			m_maxCount       = other.m_maxCount;
			m_reservedBytes  = other.m_reservedBytes;

			other.m_basePtr        = nullptr;
			other.m_size           = 0;
			other.m_committedBytes = 0;
			other.m_maxCount       = 0;
			other.m_reservedBytes  = 0;
		}
		return *this;
	}

	/**
	 * @brief 初期化リスト代入演算子
	 *
	 * 既存の要素を全て破棄し、初期化リストの内容で置き換える。
	 * 初期化リストのサイズがmax_capacityを超えるとassertで停止する。
	 *
	 * @param init 代入する初期化リスト
	 * @return 自身への参照
	 */
	RootVector& operator=(std::initializer_list<T> init)
	{
		assign(init);
		return *this;
	}

	// ================================================================
	// 初期化
	// ================================================================

	/**
	 * @brief 仮想アドレス空間を予約して使用可能状態にする
	 *
	 * 最大要素数分の仮想アドレス空間をOSから予約する。
	 * 予約サイズはOS確保粒度の倍数に切り上げられる。
	 * ネイティブ環境ではこの時点で物理メモリはほぼ消費されない。
	 * フォールバック環境では最大要素数分の実メモリが即座に確保される。
	 *
	 * 既に初期化済みの場合はassertで停止する。
	 * 再初期化したい場合は先にRelease()を呼ぶこと。
	 *
	 * @param maxElementCount 格納可能な最大要素数
	 * @return 成功時true、メモリ予約が失敗した場合false
	 */
	bool Init(size_type maxElementCount)
	{
		assert(m_basePtr == nullptr && "既に初期化済みです。再初期化する場合はRelease()を先に呼んでください。");

		const size_t rawBytes = maxElementCount * sizeof(T);
		m_reservedBytes = AlignUp(rawBytes, g_AllocationGranularity);
		m_maxCount = maxElementCount;

		m_basePtr = static_cast<T*>(VirtualMemoryAllocator::Reserve(m_reservedBytes));

		if (!m_basePtr)
		{
			m_reservedBytes = 0;
			m_maxCount      = 0;
			return false;
		}

		m_size           = 0;
		m_committedBytes = 0;
		return true;
	}

	/**
	 * @brief 全要素を破棄し、仮想アドレス空間を解放して未初期化状態に戻す
	 *
	 * デストラクタと同じ処理だが、オブジェクトは再利用可能。
	 * 解放後にInit()を呼べば再度使用できる。
	 */
	void Release()
	{
		if (m_basePtr)
		{
			DestroyRange(0, m_size);
			VirtualMemoryAllocator::Release(m_basePtr, m_reservedBytes);
		}

		m_basePtr        = nullptr;
		m_size           = 0;
		m_committedBytes = 0;
		m_maxCount       = 0;
		m_reservedBytes  = 0;
	}

	// ================================================================
	// 要素アクセス
	// ================================================================

	/**
	 * @brief インデックスで要素にアクセス
	 * @param index 要素のインデックス
	 * @return 要素への参照
	 */
	reference operator[](size_type index)
	{
		assert(index < m_size && "インデックスが範囲外です。");
		return m_basePtr[index];
	}

	/**
	 * @brief インデックスで要素にアクセス（const版）
	 * @param index 要素のインデックス
	 * @return 要素へのconst参照
	 */
	const_reference operator[](size_type index) const
	{
		assert(index < m_size && "インデックスが範囲外です。");
		return m_basePtr[index];
	}

	/**
	 * @brief 境界チェック付きインデックスアクセス
	 *
	 * 範囲外アクセスの場合はstd::out_of_range例外を投げる。
	 *
	 * @param index 要素のインデックス
	 * @return 要素への参照
	 * @throws std::out_of_range indexがsize()以上の場合
	 */
	reference at(size_type index)
	{
		if (index >= m_size)
		{
			throw std::out_of_range("RootVector::at() - インデックスが範囲外です。");
		}
		return m_basePtr[index];
	}

	/**
	 * @brief 境界チェック付きインデックスアクセス（const版）
	 *
	 * @param index 要素のインデックス
	 * @return 要素へのconst参照
	 * @throws std::out_of_range indexがsize()以上の場合
	 */
	const_reference at(size_type index) const
	{
		if (index >= m_size)
		{
			throw std::out_of_range("RootVector::at() - インデックスが範囲外です。");
		}
		return m_basePtr[index];
	}

	/// 先頭要素への参照を取得
	reference front()             { assert(m_size > 0); return m_basePtr[0]; }

	/// 先頭要素へのconst参照を取得
	const_reference front() const { assert(m_size > 0); return m_basePtr[0]; }

	/// 末尾要素への参照を取得
	reference back()              { assert(m_size > 0); return m_basePtr[m_size - 1]; }

	/// 末尾要素へのconst参照を取得
	const_reference back() const  { assert(m_size > 0); return m_basePtr[m_size - 1]; }

	/// 先頭アドレスを取得（生涯変わらない）
	pointer data()             { return m_basePtr; }

	/// 先頭アドレスを取得（const版）
	const_pointer data() const { return m_basePtr; }

	// ================================================================
	// イテレータ
	// ================================================================

	iterator begin()              { return m_basePtr; }
	const_iterator begin() const  { return m_basePtr; }
	const_iterator cbegin() const { return m_basePtr; }

	iterator end()                { return m_basePtr + m_size; }
	const_iterator end() const    { return m_basePtr + m_size; }
	const_iterator cend() const   { return m_basePtr + m_size; }

	reverse_iterator rbegin()              { return reverse_iterator(end()); }
	const_reverse_iterator rbegin() const  { return const_reverse_iterator(end()); }
	const_reverse_iterator crbegin() const { return const_reverse_iterator(end()); }

	reverse_iterator rend()                { return reverse_iterator(begin()); }
	const_reverse_iterator rend() const    { return const_reverse_iterator(begin()); }
	const_reverse_iterator crend() const   { return const_reverse_iterator(begin()); }

	// ================================================================
	// 容量情報
	// ================================================================

	/// 現在の有効要素数
	size_type size() const     { return m_size; }

	/// コミット済みの要素数（物理メモリが割り当てられた分）
	size_type capacity() const { return m_committedBytes / sizeof(T); }

	/// 予約した最大要素数
	size_type max_capacity() const { return m_maxCount; }

	/// 要素が空かどうか
	bool empty() const { return m_size == 0; }

	/// 初期化済みかどうか（Init()またはコンストラクタで予約済み）
	bool is_initialized() const { return m_basePtr != nullptr; }

	/**
	 * @brief 指定した要素数分の物理メモリを事前にコミットする
	 *
	 * std::vectorのreserve()に相当する。
	 * 指定数がmax_capacityを超えるとassertで停止する。
	 * 既にコミット済みの範囲で足りる場合は何もしない。
	 *
	 * @param count コミットする要素数
	 */
	void reserve(size_type count)
	{
		assert(count <= m_maxCount && "max_capacityを超えるreserveはできません。");
		EnsureCommitted(count);
	}

	// ================================================================
	// 要素の追加
	// ================================================================

	/**
	 * @brief 末尾に要素をコピー追加する
	 *
	 * 必要に応じて物理メモリをコミットしてからplacement newで要素を構築する。
	 * 最大要素数に達している場合はassertで停止する。
	 *
	 * @param value コピーする要素
	 */
	void push_back(const T& value)
	{
		assert(m_size < m_maxCount && "最大要素数に達しています。");
		EnsureCommitted(m_size + 1);
		new (&m_basePtr[m_size]) T(value);
		++m_size;
	}

	/**
	 * @brief 末尾に要素をムーブ追加する
	 *
	 * 必要に応じて物理メモリをコミットしてからplacement newで要素をムーブ構築する。
	 * 最大要素数に達している場合はassertで停止する。
	 *
	 * @param value ムーブする要素
	 */
	void push_back(T&& value)
	{
		assert(m_size < m_maxCount && "最大要素数に達しています。");
		EnsureCommitted(m_size + 1);
		new (&m_basePtr[m_size]) T(std::move(value));
		++m_size;
	}

	/**
	 * @brief 末尾に要素を直接構築する
	 *
	 * 必要に応じて物理メモリをコミットしてから、
	 * 引数を完全転送してplacement newで要素を構築する。
	 *
	 * @tparam Args コンストラクタ引数の型
	 * @param args コンストラクタに転送する引数
	 * @return 構築された要素への参照
	 */
	template<typename... Args>
	T& emplace_back(Args&&... args)
	{
		assert(m_size < m_maxCount && "最大要素数に達しています。");
		EnsureCommitted(m_size + 1);
		T* ptr = new (&m_basePtr[m_size]) T(std::forward<Args>(args)...);
		++m_size;
		return *ptr;
	}

	// ================================================================
	// 挿入
	// ================================================================

	/**
	 * @brief 指定位置に要素をコピー挿入する
	 *
	 * posの位置に要素を挿入し、pos以降の要素を後方にシフトする。
	 *
	 * @note シフトにより、pos以降のアドレスに以前と異なるオブジェクトが
	 *       配置される。そのアドレスへのキャッシュポインタは、アドレスは
	 *       有効だが指す先のオブジェクトが変わっている点に注意。
	 *
	 * @param pos 挿入位置を示すイテレータ
	 * @param value 挿入する要素
	 * @return 挿入された要素を指すイテレータ
	 */
	iterator insert(const_iterator pos, const T& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");
		assert(m_size < m_maxCount && "最大要素数に達しています。");

		EnsureCommitted(m_size + 1);
		ShiftRight(index, 1);
		ReconstructAt(index, value);

		++m_size;
		return m_basePtr + index;
	}

	/**
	 * @brief 指定位置に要素をムーブ挿入する
	 *
	 * @note insert(pos, const T&)と同様の注意事項が適用される。
	 *
	 * @param pos 挿入位置を示すイテレータ
	 * @param value ムーブする要素
	 * @return 挿入された要素を指すイテレータ
	 */
	iterator insert(const_iterator pos, T&& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");
		assert(m_size < m_maxCount && "最大要素数に達しています。");

		EnsureCommitted(m_size + 1);
		ShiftRight(index, 1);
		ReconstructAt(index, std::move(value));

		++m_size;
		return m_basePtr + index;
	}

	/**
	 * @brief 指定位置に同じ値をcount個挿入する
	 *
	 * @note insert(pos, const T&)と同様の注意事項が適用される。
	 *
	 * @param pos 挿入位置を示すイテレータ
	 * @param count 挿入する要素数
	 * @param value 挿入する値
	 * @return 最初に挿入された要素を指すイテレータ
	 */
	iterator insert(const_iterator pos, size_type count, const T& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");
		assert(m_size + count <= m_maxCount && "最大要素数を超えます。");

		if (count == 0) return m_basePtr + index;

		EnsureCommitted(m_size + count);
		ShiftRight(index, count);

		for (size_type i = 0; i < count; ++i)
		{
			ReconstructAt(index + i, value);
		}

		m_size += count;
		return m_basePtr + index;
	}

	/**
	 * @brief 指定位置にイテレータ範囲の要素を挿入する
	 *
	 * @note insert(pos, const T&)と同様の注意事項が適用される。
	 *
	 * @tparam InputIt 入力イテレータの型
	 * @param pos 挿入位置を示すイテレータ
	 * @param first 範囲の先頭
	 * @param last 範囲の終端
	 * @return 最初に挿入された要素を指すイテレータ
	 */
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	iterator insert(const_iterator pos, InputIt first, InputIt last)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		const size_type count = static_cast<size_type>(std::distance(first, last));
		assert(index <= m_size && "挿入位置が範囲外です。");
		assert(m_size + count <= m_maxCount && "最大要素数を超えます。");

		if (count == 0) return m_basePtr + index;

		EnsureCommitted(m_size + count);
		ShiftRight(index, count);

		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
		{
			ReconstructAt(index + i, *it);
		}

		m_size += count;
		return m_basePtr + index;
	}

	/**
	 * @brief 指定位置に初期化リストの要素を挿入する
	 *
	 * @note insert(pos, const T&)と同様の注意事項が適用される。
	 *
	 * @param pos 挿入位置を示すイテレータ
	 * @param ilist 挿入する初期化リスト
	 * @return 最初に挿入された要素を指すイテレータ
	 */
	iterator insert(const_iterator pos, std::initializer_list<T> ilist)
	{
		return insert(pos, ilist.begin(), ilist.end());
	}

	/**
	 * @brief 指定位置に要素を直接構築する
	 *
	 * posの位置にコンストラクタ引数を完全転送して要素を構築する。
	 * pos以降の既存要素は後方にシフトされる。
	 *
	 * @note insert()と同様に、シフトによってpos以降のアドレスに
	 *       以前と異なるオブジェクトが配置される点に注意。
	 *
	 * @tparam Args コンストラクタ引数の型
	 * @param pos 構築位置を示すイテレータ
	 * @param args コンストラクタに転送する引数
	 * @return 構築された要素を指すイテレータ
	 */
	template<typename... Args>
	iterator emplace(const_iterator pos, Args&&... args)
	{
    	const size_type index = static_cast<size_type>(pos - cbegin());
    	assert(index <= m_size && "挿入位置が範囲外です。");
    	assert(m_size < m_maxCount && "最大要素数に達しています。");

    	EnsureCommitted(m_size + 1);
    	ShiftRight(index, 1);
    	EmplaceAt(index, std::forward<Args>(args)...);

    	++m_size;
    	return m_basePtr + index;
	}

	// ================================================================
	// 削除
	// ================================================================

	/**
	 * @brief 指定位置の要素を削除する
	 *
	 * pos以降の要素を前方にシフトし、末尾要素を破棄する。
	 *
	 * @note シフトにより、pos以降のアドレスに以前と異なるオブジェクトが
	 *       配置される。insert()と同様の注意事項が適用される。
	 *
	 * @param pos 削除する要素を示すイテレータ
	 * @return 削除された要素の次の要素を指すイテレータ
	 */
	iterator erase(const_iterator pos)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index < m_size && "削除位置が範囲外です。");

		ShiftLeft(index, 1);
		--m_size;
		m_basePtr[m_size].~T();

		return m_basePtr + index;
	}

	/**
	 * @brief 指定範囲の要素を削除する
	 *
	 * [first, last) の要素を削除し、後続の要素を前方にシフトする。
	 *
	 * @note insert()と同様の注意事項が適用される。
	 *
	 * @param first 削除範囲の先頭を示すイテレータ
	 * @param last 削除範囲の終端を示すイテレータ
	 * @return 削除された範囲の次の要素を指すイテレータ
	 */
	iterator erase(const_iterator first, const_iterator last)
	{
		const size_type beginIdx = static_cast<size_type>(first - cbegin());
		const size_type endIdx   = static_cast<size_type>(last - cbegin());
		assert(beginIdx <= endIdx && endIdx <= m_size && "削除範囲が不正です。");

		const size_type count = endIdx - beginIdx;
		if (count == 0) return m_basePtr + beginIdx;

		ShiftLeft(beginIdx, count);

		const size_type newSize = m_size - count;
		DestroyRange(newSize, m_size);
		m_size = newSize;

		return m_basePtr + beginIdx;
	}

	// ================================================================
	// 要素の削除（末尾）
	// ================================================================

	/**
	 * @brief 末尾の要素を破棄する
	 *
	 * 末尾要素のデストラクタを明示的に呼び出してサイズを減らす。
	 * 空の状態で呼ぶとassertで停止する。
	 */
	void pop_back()
	{
		assert(m_size > 0 && "空のコンテナに対してpop_back()が呼ばれました。");
		--m_size;
		m_basePtr[m_size].~T();
	}

	/**
	 * @brief 全要素を破棄してサイズを0にする
	 *
	 * 全ての構築済み要素のデストラクタを呼び出す。
	 * コミット済みの物理メモリはそのまま維持される。
	 * 完全に解放したい場合はshrink_to_fit()を続けて呼ぶこと。
	 */
	void clear()
	{
		DestroyRange(0, m_size);
		m_size = 0;
	}

	// ================================================================
	// サイズ変更
	// ================================================================

	/**
	 * @brief 要素数をデフォルト値で変更する
	 *
	 * 現在のサイズより大きい場合はデフォルト構築で要素を追加し、
	 * 小さい場合は末尾から要素を破棄する。
	 *
	 * @param newSize 新しい要素数
	 */
	void resize(size_type newSize)
	{
		assert(newSize <= m_maxCount && "最大要素数を超えるresizeはできません。");

		if (newSize > m_size)
		{
			EnsureCommitted(newSize);
			for (size_type i = m_size; i < newSize; ++i)
			{
				new (&m_basePtr[i]) T();
			}
		}
		else if (newSize < m_size)
		{
			DestroyRange(newSize, m_size);
		}

		m_size = newSize;
	}

	/**
	 * @brief 要素数を指定値で変更する
	 *
	 * 現在のサイズより大きい場合はvalueのコピーで要素を追加し、
	 * 小さい場合は末尾から要素を破棄する。
	 *
	 * @param newSize 新しい要素数
	 * @param value 新しい要素の初期値
	 */
	void resize(size_type newSize, const T& value)
	{
		assert(newSize <= m_maxCount && "最大要素数を超えるresizeはできません。");

		if (newSize > m_size)
		{
			EnsureCommitted(newSize);
			for (size_type i = m_size; i < newSize; ++i)
			{
				new (&m_basePtr[i]) T(value);
			}
		}
		else if (newSize < m_size)
		{
			DestroyRange(newSize, m_size);
		}

		m_size = newSize;
	}

	/**
	 * @brief 使用中の要素数に合わせてコミット済みメモリを縮小する
	 *
	 * 現在の要素数に必要なページ数を超えるコミット済みページをデコミットして
	 * 物理メモリをOSに返却する。仮想アドレス空間は維持されるため、
	 * 再度要素を追加すれば自動的に再コミットされる。
	 *
	 * フォールバック環境ではデコミットできないため何もしない。
	 */
	void shrink_to_fit()
	{
		const size_t neededBytes = CalcCommitBytes(m_size);

		if (neededBytes < m_committedBytes)
		{
			void* newBase = VirtualMemoryAllocator::Decommit(
				m_basePtr, m_committedBytes, neededBytes
			);
			assert(newBase != nullptr && "メモリのデコミットに失敗しました。");

			m_basePtr = static_cast<T*>(newBase);
			m_committedBytes = neededBytes;
		}
	}

	// ================================================================
	// 一括代入
	// ================================================================

	/**
	 * @brief 指定個数の同じ値で内容を置き換える
	 *
	 * 既存の全要素を破棄した後、count個のvalueのコピーで再構築する。
	 *
	 * @param count 代入する要素数
	 * @param value 各要素の値
	 */
	void assign(size_type count, const T& value)
	{
		assert(count <= m_maxCount && "最大要素数を超えるassignはできません。");
		clear();
		EnsureCommitted(count);
		for (size_type i = 0; i < count; ++i)
		{
			new (&m_basePtr[i]) T(value);
		}
		m_size = count;
	}

	/**
	 * @brief イテレータ範囲の内容で置き換える
	 *
	 * 既存の全要素を破棄した後、[first, last)の内容で再構築する。
	 *
	 * @tparam InputIt 入力イテレータの型
	 * @param first 範囲の先頭
	 * @param last 範囲の終端
	 */
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	void assign(InputIt first, InputIt last)
	{
		const size_type count = static_cast<size_type>(std::distance(first, last));
		assert(count <= m_maxCount && "最大要素数を超えるassignはできません。");
		clear();
		EnsureCommitted(count);
		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
		{
			new (&m_basePtr[i]) T(*it);
		}
		m_size = count;
	}

	/**
	 * @brief 初期化リストの内容で置き換える
	 *
	 * 既存の全要素を破棄した後、初期化リストの内容で再構築する。
	 *
	 * @param ilist 代入する初期化リスト
	 */
	void assign(std::initializer_list<T> ilist)
	{
		assign(ilist.begin(), ilist.end());
	}

	// ================================================================
	// 交換
	// ================================================================

	/**
	 * @brief 別のRootVectorと内容を交換する
	 *
	 * 全ての内部状態（メモリ、サイズ、容量）を交換する。
	 * 要素のコピーやムーブは一切発生しない。
	 *
	 * @note 交換後、外部でキャッシュされたポインタは交換相手のメモリを指す。
	 *       ポインタの指す先が意図通りか確認すること。
	 *
	 * @param other 交換先のRootVector
	 */
	void swap(RootVector& other) noexcept
	{
		if (this == &other) return;

		std::swap(m_basePtr,        other.m_basePtr);
		std::swap(m_size,           other.m_size);
		std::swap(m_committedBytes, other.m_committedBytes);
		std::swap(m_maxCount,       other.m_maxCount);
		std::swap(m_reservedBytes,  other.m_reservedBytes);
	}

	// ================================================================
	// 比較演算子
	// ================================================================

	/// 等価比較
	bool operator==(const RootVector& other) const
	{
		if (m_size != other.m_size) return false;
		return std::equal(begin(), end(), other.begin());
	}

	/// 非等価比較
	bool operator!=(const RootVector& other) const
	{
		return !(*this == other);
	}

	/// 辞書式小なり比較
	bool operator<(const RootVector& other) const
	{
		return std::lexicographical_compare(begin(), end(), other.begin(), other.end());
	}

	/// 辞書式以下比較
	bool operator<=(const RootVector& other) const
	{
		return !(other < *this);
	}

	/// 辞書式大なり比較
	bool operator>(const RootVector& other) const
	{
		return other < *this;
	}

	/// 辞書式以上比較
	bool operator>=(const RootVector& other) const
	{
		return !(*this < other);
	}

private:
	// ================================================================
	// 内部ユーティリティ
	// ================================================================

	/**
	 * @brief 指定した要素数が収まるように物理メモリをコミットする
	 *
	 * 既にコミット済みの範囲で足りる場合は何もしない。
	 * 不足する場合はページ粒度に切り上げたバイト数をまとめてコミットする。
	 * 全環境でベースアドレスは変わらない。
	 *
	 * @param requiredCount 必要な要素数
	 */
	void EnsureCommitted(size_type requiredCount)
	{
		const size_t requiredBytes = requiredCount * sizeof(T);
		if (requiredBytes <= m_committedBytes)
		{
			return;
		}

		const size_t newCommittedBytes = CalcCommitBytes(requiredCount);

		void* newBase = VirtualMemoryAllocator::Commit(
			m_basePtr, m_committedBytes, newCommittedBytes
		);
		assert(newBase != nullptr && "物理メモリのコミットに失敗しました。");
		#ifdef ROOT_VECTOR_STABLE_ADDRESS
		assert(newBase == m_basePtr && "ベースアドレスが変わりました。");
		#endif
		m_basePtr = static_cast<T*>(newBase);

		m_committedBytes = newCommittedBytes;
	}

	/**
	 * @brief 指定位置の要素を破棄して再構築する
	 *
	 * ShiftRight後のスロットは、ムーブ元として内容が不定の既存オブジェクトか、
	 * 末尾を超えた未構築領域のいずれかになる。
	 * 既存オブジェクトの場合はデストラクタで破棄してからplacement newで再構築し、
	 * 未構築領域の場合はplacement newのみで構築する。
	 * これにより、代入とplacement newの不統一を避ける。
	 *
	 * @tparam U 構築に使う値の型（const T&またはT&&）
	 * @param index 構築先のインデックス
	 * @param value 構築に使う値
	 */
	template<typename U>
	void ReconstructAt(size_type index, U&& value)
	{
		if (index < m_size)
		{
			m_basePtr[index].~T();
		}
		new (&m_basePtr[index]) T(std::forward<U>(value));
	}

	/**
	 * @brief 指定位置の要素を破棄して引数転送で再構築する
	 *
	 * ReconstructAtのemplace版。コンストラクタ引数を完全転送して構築する。
	 * ShiftRight後のムーブ残骸がある場合はデストラクタで破棄してから再構築する。
	 * 未構築領域の場合はplacement newのみで構築する。
	 *
	 * @tparam Args コンストラクタ引数の型
	 * @param index 構築先のインデックス
	 * @param args コンストラクタに転送する引数
	 */
	template<typename... Args>
	void EmplaceAt(size_type index, Args&&... args)
	{
	    if (index < m_size)
	    {
	        m_basePtr[index].~T();
	    }
	    new (&m_basePtr[index]) T(std::forward<Args>(args)...);
	}

	/**
	 * @brief 要素を後方にシフトする（挿入用）
	 *
	 * [pos, m_size) の要素を [pos + count, m_size + count) に移動する。
	 * 末尾を超える領域は未構築のためplacement new + ムーブで構築し、
	 * 既存領域はムーブ代入で移動する。
	 *
	 * @param pos シフト開始位置
	 * @param count シフトする距離
	 */
	void ShiftRight(size_type pos, size_type count)
	{
		if (count == 0 || pos == m_size) return;

		for (size_type i = m_size; i > pos; --i)
		{
			const size_type src = i - 1;
			const size_type dst = src + count;

			if (dst >= m_size)
			{
				new (&m_basePtr[dst]) T(std::move(m_basePtr[src]));
			}
			else
			{
				m_basePtr[dst] = std::move(m_basePtr[src]);
			}
		}
	}

	/**
	 * @brief 要素を前方にシフトする（削除用）
	 *
	 * [pos + count, m_size) の要素を [pos, m_size - count) にムーブ代入する。
	 * シフト後、末尾のcount個の要素はムーブ元の残骸になるが、
	 * 呼び出し側でDestroyRangeにより破棄される。
	 *
	 * @param pos シフト先の開始位置
	 * @param count 削除する要素数（シフト距離）
	 */
	void ShiftLeft(size_type pos, size_type count)
	{
		if (count == 0) return;

		for (size_type i = pos + count; i < m_size; ++i)
		{
			m_basePtr[i - count] = std::move(m_basePtr[i]);
		}
	}

	/**
	 * @brief 指定した要素数に必要なコミットバイト数を算出する
	 *
	 * 要素数に対応するバイト数をページ粒度に切り上げて返す。
	 * 戻り値は常にページアライメント済みのため、
	 * VirtualMemoryAllocator::Commit/Decommitに直接渡せる。
	 * 最大予約バイト数を超えないように制限する。
	 *
	 * @param elementCount 必要な要素数
	 * @return ページアライメント済みのバイト数
	 */
	size_t CalcCommitBytes(size_type elementCount) const
	{
    	const size_t neededBytes  = elementCount * sizeof(T);
    	const size_t alignedBytes = AlignUp(neededBytes, g_PageSize);
    	return std::min(alignedBytes, m_reservedBytes);
	}

	/**
	 * @brief 値を指定アライメントの倍数に切り上げる
	 *
	 * @param value 切り上げ対象の値
	 * @param alignment アライメント（2のべき乗であること）
	 * @return 切り上げた値
	 */
	static size_t AlignUp(size_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	/**
	 * @brief 指定範囲の要素のデストラクタを呼び出す
	 *
	 * [beginIndex, endIndex) の範囲の各要素に対してデストラクタを明示呼び出しする。
	 * トリビアルデストラクタの型では最適化により何もしない。
	 *
	 * @param beginIndex 破棄開始インデックス（含む）
	 * @param endIndex 破棄終了インデックス（含まない）
	 */
	void DestroyRange(size_type beginIndex, size_type endIndex)
	{
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (size_type i = beginIndex; i < endIndex; ++i)
			{
				m_basePtr[i].~T();
			}
		}
	}

	// ================================================================
	// メンバ変数
	// ================================================================

	/** 予約した仮想アドレス空間の先頭ポインタ（全環境で生涯変わらない） */
	T* m_basePtr = nullptr;

	/** 構築済み要素数 */
	size_type m_size = 0;

	/** コミット済みバイト数（常にページアライメント済み） */
	size_t m_committedBytes = 0;

	/** 予約した最大要素数 */
	size_type m_maxCount = 0;

	/** 予約した仮想アドレス空間の総バイト数 */
	size_t m_reservedBytes = 0;

};

/// ADL用swap関数
template<typename T>
void swap(RootVector<T>& lhs, RootVector<T>& rhs) noexcept
{
	lhs.swap(rhs);
}