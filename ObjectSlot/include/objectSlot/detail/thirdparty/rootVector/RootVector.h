#pragma once

#include "VirtualMemoryAllocator.h"

#include <cstddef>
#include <cstdint>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
#include <utility>
#include <iterator>
#include <initializer_list>
#include <stdexcept>
#include <algorithm>

/**
 * @class root_vector
 * @brief std::vectorの機能をベースに、全環境で安定した要素参照を提供するコンテナ
 *
 * 【責任】
 * - std::vectorと同等の動的配列機能を提供する
 * - 全環境で安定した要素参照を提供するroot_pointer型を提供する
 * - 要素の構築/破棄をplacement new/デストラクタ呼び出しで正しく行う
 *
 * 【ネイティブ環境での動作】
 * - 初回確保時に大きな仮想アドレス空間を予約する（物理メモリは消費しない）
 * - 以降のpush_back等では物理メモリのコミットのみで済み、アドレスは不変
 * - root_pointerは生ポインタ（T*）を直接保持する（8バイト）
 * - 仮想アドレス空間を超えた場合はエラーメッセージとともに強制終了する
 *
 * 【フォールバック環境での動作】
 * - mallocで確保し、std::vectorと同じ2倍成長で拡張する
 * - 初回確保時にポインタテーブル（T*の配列）をmallocで確保する
 * - root_pointerはテーブルエントリのアドレス（T**）を保持する（8バイト）
 * - データの引っ越し時にテーブルの中身を更新し、root_pointerの値は変わらない
 * - テーブル上限を超えた場合はエラーメッセージとともに強制終了する
 *
 * 【std::vectorとの相違点】
 * - 要素アクセスはoperator[]ではなくget()を使用する
 * - get_root_pointer()で全環境で安定したroot_pointerを取得できる
 *
 * @tparam T 格納する要素の型
 */
template<typename T>
class root_vector
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
	// root_pointer（全環境で安定した要素参照、8バイト）
	// ================================================================

	/**
	 * @class root_pointer
	 * @brief 全環境で8バイトの安定した要素アクセスを提供するスマートポインタ型
	 *
	 * 【責任】
	 * - ネイティブ環境では生ポインタ（T*）を直接保持し、1回のデリファレンスでアクセスする
	 * - フォールバック環境ではポインタテーブルエントリのアドレス（T**）を保持し、
	 *   2回のデリファレンスでアクセスする。データが引っ越してもテーブルの中身が
	 *   更新されるため、root_pointer自体の値は変わらない
	 *
	 * 【注意事項】
	 * - insert/eraseで要素がシフトされると、同じインデックスに
	 *   異なるオブジェクトが配置される
	 * - eraseで要素数が減ると、末尾側のroot_pointerは無効になる
	 * - root_vectorより先にroot_pointerが破棄されること
	 */
	class root_pointer
	{
	public:
		/// デフォルトコンストラクタ（無効な状態で生成）
		root_pointer() = default;

#if defined(ROOT_VECTOR_STABLE_ADDRESS)
		/// 生ポインタから構築する（ネイティブ環境）
		explicit root_pointer(T* ptr) : m_ptr(ptr) {}
#else
		/// テーブルエントリのアドレスから構築する（フォールバック環境）
		explicit root_pointer(T** handle) : m_handle(handle) {}
#endif

		/// 要素への参照を取得
		T& operator*() const
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			return *m_ptr;
#else
			return **m_handle;
#endif
		}

		/// アロー演算子
		T* operator->() const
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			return m_ptr;
#else
			return *m_handle;
#endif
		}

		/// 現在の生ポインタを取得
		T* get() const
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			return m_ptr;
#else
			return m_handle ? *m_handle : nullptr;
#endif
		}

		/// 有効なポインタを保持しているか
		explicit operator bool() const
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			return m_ptr != nullptr;
#else
			return m_handle != nullptr;
#endif
		}

		/// 無効化する
		void reset()
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			m_ptr = nullptr;
#else
			m_handle = nullptr;
#endif
		}

		/// 等価比較
		bool operator==(const root_pointer& other) const
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			return m_ptr == other.m_ptr;
#else
			return m_handle == other.m_handle;
#endif
		}

		/// 非等価比較
		bool operator!=(const root_pointer& other) const
		{
			return !(*this == other);
		}

	private:
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
		/** 要素への生ポインタ（アドレス不変が保証される） */
		T* m_ptr = nullptr;
#else
		/** ポインタテーブルエントリのアドレス（テーブル自体は固定アドレス） */
		T** m_handle = nullptr;
#endif
	};

	// ================================================================
	// コンストラクタ / デストラクタ
	// ================================================================

	/**
	 * @brief デフォルトコンストラクタ（空の状態で生成）
	 *
	 * std::vector同様、メモリは確保されない。
	 * 最初のpush_back等で自動的にメモリが確保される。
	 */
	root_vector() = default;

	/**
	 * @brief 指定個数のデフォルト構築要素で初期化するコンストラクタ
	 *
	 * std::vector(n) と同じ動作。
	 *
	 * @param count 構築する要素数
	 */
	explicit root_vector(size_type count)
	{
		ensure_capacity(count);
		ensure_committed(count);
		for (size_type i = 0; i < count; ++i)
			new (&m_base_ptr[i]) T();
		m_size = count;
		on_elements_added(0, count);
	}

	/**
	 * @brief 指定個数の要素を値で初期化するコンストラクタ
	 *
	 * std::vector(n, value) と同じ動作。
	 *
	 * @param count 構築する要素数
	 * @param value 各要素の初期値
	 */
	root_vector(size_type count, const T& value)
	{
		ensure_capacity(count);
		ensure_committed(count);
		for (size_type i = 0; i < count; ++i)
			new (&m_base_ptr[i]) T(value);
		m_size = count;
		on_elements_added(0, count);
	}

	/**
	 * @brief 初期化リストで初期化するコンストラクタ
	 *
	 * @param init 初期化リスト
	 */
	root_vector(std::initializer_list<T> init)
	{
		ensure_capacity(init.size());
		ensure_committed(init.size());
		size_type i = 0;
		for (const auto& v : init)
		{
			new (&m_base_ptr[i]) T(v);
			++i;
		}
		m_size = init.size();
		on_elements_added(0, m_size);
	}

	/**
	 * @brief イテレータ範囲で初期化するコンストラクタ
	 *
	 * @tparam InputIt 入力イテレータの型
	 * @param first 範囲の先頭
	 * @param last 範囲の終端
	 */
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	root_vector(InputIt first, InputIt last)
	{
		const size_type count = static_cast<size_type>(std::distance(first, last));
		ensure_capacity(count);
		ensure_committed(count);
		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
			new (&m_base_ptr[i]) T(*it);
		m_size = count;
		on_elements_added(0, count);
	}

	/**
	 * @brief コピーコンストラクタ
	 *
	 * コピー元と同じサイズの新しい領域を確保し、全要素をコピー構築する。
	 * コピー先は独立した領域とポインタテーブルを持つ。
	 *
	 * @param other コピー元のroot_vector
	 */
	root_vector(const root_vector& other)
	{
		if (other.m_size > 0)
		{
			ensure_capacity(other.m_size);
			ensure_committed(other.m_size);
			for (size_type i = 0; i < other.m_size; ++i)
				new (&m_base_ptr[i]) T(other.m_base_ptr[i]);
			m_size = other.m_size;
			on_elements_added(0, m_size);
		}
	}

	/**
	 * @brief コピー代入演算子
	 *
	 * 自身の全要素を破棄した後、コピー元の要素で再構築する。
	 *
	 * @param other コピー元のroot_vector
	 * @return 自身への参照
	 */
	root_vector& operator=(const root_vector& other)
	{
		if (this != &other)
		{
			clear();
			if (other.m_size > 0)
			{
				ensure_capacity(other.m_size);
				ensure_committed(other.m_size);
				for (size_type i = 0; i < other.m_size; ++i)
					new (&m_base_ptr[i]) T(other.m_base_ptr[i]);
				m_size = other.m_size;
				on_elements_added(0, m_size);
			}
		}
		return *this;
	}

	/**
	 * @brief ムーブコンストラクタ
	 *
	 * 移動元のメモリとポインタテーブルの所有権を引き継ぐ。
	 * 移動元は空の状態に戻る。
	 *
	 * @param other 移動元のroot_vector
	 */
	root_vector(root_vector&& other) noexcept
		: m_base_ptr(other.m_base_ptr)
		, m_size(other.m_size)
		, m_committed_bytes(other.m_committed_bytes)
		, m_reserved_bytes(other.m_reserved_bytes)
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		, m_ptr_table(other.m_ptr_table)
		, m_table_capacity(other.m_table_capacity)
#endif
	{
		other.m_base_ptr        = nullptr;
		other.m_size            = 0;
		other.m_committed_bytes = 0;
		other.m_reserved_bytes  = 0;
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		other.m_ptr_table       = nullptr;
		other.m_table_capacity  = 0;
#endif
	}

	/**
	 * @brief ムーブ代入演算子
	 *
	 * 自身のメモリとポインタテーブルを解放してから、移動元の所有権を引き継ぐ。
	 *
	 * @param other 移動元のroot_vector
	 * @return 自身への参照
	 */
	root_vector& operator=(root_vector&& other) noexcept
	{
		if (this != &other)
		{
			if (m_base_ptr)
			{
				destroy_range(0, m_size);
				virtual_memory_allocator::release(m_base_ptr, m_reserved_bytes);
			}
			free_ptr_table();

			m_base_ptr        = other.m_base_ptr;
			m_size            = other.m_size;
			m_committed_bytes = other.m_committed_bytes;
			m_reserved_bytes  = other.m_reserved_bytes;
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
			m_ptr_table       = other.m_ptr_table;
			m_table_capacity  = other.m_table_capacity;
#endif

			other.m_base_ptr        = nullptr;
			other.m_size            = 0;
			other.m_committed_bytes = 0;
			other.m_reserved_bytes  = 0;
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
			other.m_ptr_table       = nullptr;
			other.m_table_capacity  = 0;
#endif
		}
		return *this;
	}

	/**
	 * @brief デストラクタ
	 *
	 * 全ての構築済み要素のデストラクタを呼び出した後、
	 * データ領域とポインタテーブルを解放する。
	 */
	~root_vector()
	{
		if (m_base_ptr)
		{
			destroy_range(0, m_size);
			virtual_memory_allocator::release(m_base_ptr, m_reserved_bytes);
		}
		free_ptr_table();
	}

	/// 初期化リスト代入演算子
	root_vector& operator=(std::initializer_list<T> init)
	{
		assign(init);
		return *this;
	}

	// ================================================================
	// 要素アクセス
	// ================================================================

	/**
	 * @brief インデックスで要素への参照を取得する
	 *
	 * 一時的な読み書き用。返された参照のアドレスを長期保持しないこと。
	 * フォールバック環境ではデータの引っ越しで無効になる。
	 * 長期保持にはget_root_pointer()を使用すること。
	 *
	 * @param index 要素のインデックス
	 * @return 要素への参照
	 */
	reference get(size_type index)
	{
		assert(index < m_size && "インデックスが範囲外です。");
		return m_base_ptr[index];
	}

	/// 要素への参照を取得（const版）
	const_reference get(size_type index) const
	{
		assert(index < m_size && "インデックスが範囲外です。");
		return m_base_ptr[index];
	}

	/**
	 * @brief 指定インデックスの要素への安定ポインタを取得する
	 *
	 * 全環境で8バイトの安定したroot_pointerを返す。
	 * ネイティブ環境では生ポインタと同等のコスト（1回のデリファレンス）。
	 * フォールバック環境ではテーブル経由（2回のデリファレンス）だが、
	 * データが引っ越しても常に正しい要素にアクセスできる。
	 *
	 * @param index 要素のインデックス
	 * @return 要素への安定ポインタ
	 */
	root_pointer get_root_pointer(size_type index)
	{
		assert(index < m_size && "インデックスが範囲外です。");
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
		return root_pointer(&m_base_ptr[index]);
#else
		return root_pointer(&m_ptr_table[index]);
#endif
	}

	/**
	 * @brief 境界チェック付きインデックスアクセス
	 * @throws std::out_of_range indexがsize()以上の場合
	 */
	reference at(size_type index)
	{
		if (index >= m_size)
			throw std::out_of_range("root_vector::at() - インデックスが範囲外です。");
		return m_base_ptr[index];
	}

	/// 境界チェック付きインデックスアクセス（const版）
	const_reference at(size_type index) const
	{
		if (index >= m_size)
			throw std::out_of_range("root_vector::at() - インデックスが範囲外です。");
		return m_base_ptr[index];
	}

	reference front()             { assert(m_size > 0); return m_base_ptr[0]; }
	const_reference front() const { assert(m_size > 0); return m_base_ptr[0]; }

	reference back()              { assert(m_size > 0); return m_base_ptr[m_size - 1]; }
	const_reference back() const  { assert(m_size > 0); return m_base_ptr[m_size - 1]; }

	pointer data()             { return m_base_ptr; }
	const_pointer data() const { return m_base_ptr; }

	// ================================================================
	// イテレータ
	// ================================================================

	iterator begin()              { return m_base_ptr; }
	const_iterator begin() const  { return m_base_ptr; }
	const_iterator cbegin() const { return m_base_ptr; }

	iterator end()                { return m_base_ptr + m_size; }
	const_iterator end() const    { return m_base_ptr + m_size; }
	const_iterator cend() const   { return m_base_ptr + m_size; }

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
	size_type size() const { return m_size; }

	/// 再確保なしで格納可能な要素数
	size_type capacity() const { return m_reserved_bytes / sizeof(T); }

	/// 要素が空かどうか
	bool empty() const { return m_size == 0; }

	/// 指定した要素数分の容量を確保する
	void reserve(size_type count)
	{
		ensure_capacity(count);
		ensure_committed(count);
	}

	/**
	 * @brief 使用中の要素数に合わせてコミット済みメモリを縮小する
	 *
	 * ネイティブ環境では未使用のページをOSに返却する。
	 * フォールバック環境では何もしない。
	 */
	void shrink_to_fit()
	{
		const size_t needed_bytes = calc_commit_bytes(m_size, m_reserved_bytes);

		if (needed_bytes < m_committed_bytes)
		{
			void* result = virtual_memory_allocator::decommit(
				m_base_ptr, m_committed_bytes, needed_bytes
			);
			assert(result != nullptr && "メモリのデコミットに失敗しました。");
			m_committed_bytes = needed_bytes;
		}
	}

	// ================================================================
	// 要素の追加
	// ================================================================

	/// 末尾に要素をコピー追加する
	void push_back(const T& value)
	{
		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		new (&m_base_ptr[m_size]) T(value);
		on_element_added(m_size);
		++m_size;
	}

	/// 末尾に要素をムーブ追加する
	void push_back(T&& value)
	{
		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		new (&m_base_ptr[m_size]) T(std::move(value));
		on_element_added(m_size);
		++m_size;
	}

	/**
	 * @brief 末尾に要素を直接構築する
	 * @return 構築された要素への参照
	 */
	template<typename... Args>
	T& emplace_back(Args&&... args)
	{
		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		T* ptr = new (&m_base_ptr[m_size]) T(std::forward<Args>(args)...);
		on_element_added(m_size);
		++m_size;
		return *ptr;
	}

	// ================================================================
	// 挿入
	// ================================================================

	/// 指定位置に要素をコピー挿入する
	iterator insert(const_iterator pos, const T& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");

		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		shift_right(index, 1);
		reconstruct_at(index, value);
		on_element_added(m_size);

		++m_size;
		return m_base_ptr + index;
	}

	/// 指定位置に要素をムーブ挿入する
	iterator insert(const_iterator pos, T&& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");

		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		shift_right(index, 1);
		reconstruct_at(index, std::move(value));
		on_element_added(m_size);

		++m_size;
		return m_base_ptr + index;
	}

	/// 指定位置に同じ値をcount個挿入する
	iterator insert(const_iterator pos, size_type count, const T& value)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");
		if (count == 0) return m_base_ptr + index;

		ensure_capacity(m_size + count);
		ensure_committed(m_size + count);
		shift_right(index, count);
		for (size_type i = 0; i < count; ++i)
			reconstruct_at(index + i, value);
		on_elements_added(m_size, m_size + count);

		m_size += count;
		return m_base_ptr + index;
	}

	/// 指定位置にイテレータ範囲の要素を挿入する
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	iterator insert(const_iterator pos, InputIt first, InputIt last)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		const size_type count = static_cast<size_type>(std::distance(first, last));
		assert(index <= m_size && "挿入位置が範囲外です。");
		if (count == 0) return m_base_ptr + index;

		ensure_capacity(m_size + count);
		ensure_committed(m_size + count);
		shift_right(index, count);
		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
			reconstruct_at(index + i, *it);
		on_elements_added(m_size, m_size + count);

		m_size += count;
		return m_base_ptr + index;
	}

	/// 指定位置に初期化リストの要素を挿入する
	iterator insert(const_iterator pos, std::initializer_list<T> ilist)
	{
		return insert(pos, ilist.begin(), ilist.end());
	}

	/// 指定位置に要素を直接構築する
	template<typename... Args>
	iterator emplace(const_iterator pos, Args&&... args)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index <= m_size && "挿入位置が範囲外です。");

		ensure_capacity(m_size + 1);
		ensure_committed(m_size + 1);
		shift_right(index, 1);
		emplace_at(index, std::forward<Args>(args)...);
		on_element_added(m_size);

		++m_size;
		return m_base_ptr + index;
	}

	// ================================================================
	// 削除
	// ================================================================

	/// 指定位置の要素を削除する
	iterator erase(const_iterator pos)
	{
		const size_type index = static_cast<size_type>(pos - cbegin());
		assert(index < m_size && "削除位置が範囲外です。");

		shift_left(index, 1);
		--m_size;
		m_base_ptr[m_size].~T();

		return m_base_ptr + index;
	}

	/// 指定範囲の要素を削除する
	iterator erase(const_iterator first, const_iterator last)
	{
		const size_type begin_idx = static_cast<size_type>(first - cbegin());
		const size_type end_idx   = static_cast<size_type>(last - cbegin());
		assert(begin_idx <= end_idx && end_idx <= m_size && "削除範囲が不正です。");

		const size_type count = end_idx - begin_idx;
		if (count == 0) return m_base_ptr + begin_idx;

		shift_left(begin_idx, count);
		const size_type new_size = m_size - count;
		destroy_range(new_size, m_size);
		m_size = new_size;

		return m_base_ptr + begin_idx;
	}

	/// 末尾の要素を破棄する
	void pop_back()
	{
		assert(m_size > 0 && "空のコンテナに対してpop_back()が呼ばれました。");
		--m_size;
		m_base_ptr[m_size].~T();
	}

	/// 全要素を破棄してサイズを0にする
	void clear()
	{
		destroy_range(0, m_size);
		m_size = 0;
	}

	// ================================================================
	// サイズ変更
	// ================================================================

	/// 要素数をデフォルト値で変更する
	void resize(size_type new_size)
	{
		if (new_size > m_size)
		{
			ensure_capacity(new_size);
			ensure_committed(new_size);
			const size_type old_size = m_size;
			for (size_type i = old_size; i < new_size; ++i)
				new (&m_base_ptr[i]) T();
			m_size = new_size;
			on_elements_added(old_size, new_size);
		}
		else if (new_size < m_size)
		{
			destroy_range(new_size, m_size);
			m_size = new_size;
		}
	}

	/// 要素数を指定値で変更する
	void resize(size_type new_size, const T& value)
	{
		if (new_size > m_size)
		{
			ensure_capacity(new_size);
			ensure_committed(new_size);
			const size_type old_size = m_size;
			for (size_type i = old_size; i < new_size; ++i)
				new (&m_base_ptr[i]) T(value);
			m_size = new_size;
			on_elements_added(old_size, new_size);
		}
		else if (new_size < m_size)
		{
			destroy_range(new_size, m_size);
			m_size = new_size;
		}
	}

	// ================================================================
	// 一括代入
	// ================================================================

	/// 指定個数の同じ値で内容を置き換える
	void assign(size_type count, const T& value)
	{
		clear();
		ensure_capacity(count);
		ensure_committed(count);
		for (size_type i = 0; i < count; ++i)
			new (&m_base_ptr[i]) T(value);
		m_size = count;
		on_elements_added(0, count);
	}

	/// イテレータ範囲の内容で置き換える
	template<typename InputIt, std::enable_if_t<!std::is_integral_v<InputIt>, int> = 0>
	void assign(InputIt first, InputIt last)
	{
		const size_type count = static_cast<size_type>(std::distance(first, last));
		clear();
		ensure_capacity(count);
		ensure_committed(count);
		size_type i = 0;
		for (auto it = first; it != last; ++it, ++i)
			new (&m_base_ptr[i]) T(*it);
		m_size = count;
		on_elements_added(0, count);
	}

	/// 初期化リストの内容で置き換える
	void assign(std::initializer_list<T> ilist)
	{
		assign(ilist.begin(), ilist.end());
	}

	// ================================================================
	// 交換
	// ================================================================

	/// 別のroot_vectorと内容を交換する
	void swap(root_vector& other) noexcept
	{
		if (this == &other) return;

		std::swap(m_base_ptr,        other.m_base_ptr);
		std::swap(m_size,            other.m_size);
		std::swap(m_committed_bytes, other.m_committed_bytes);
		std::swap(m_reserved_bytes,  other.m_reserved_bytes);
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		std::swap(m_ptr_table,       other.m_ptr_table);
		std::swap(m_table_capacity,  other.m_table_capacity);
#endif
	}

	// ================================================================
	// 比較演算子
	// ================================================================

	bool operator==(const root_vector& other) const
	{
		if (m_size != other.m_size) return false;
		return std::equal(begin(), end(), other.begin());
	}

	bool operator!=(const root_vector& other) const { return !(*this == other); }

	bool operator<(const root_vector& other) const
	{
		return std::lexicographical_compare(begin(), end(), other.begin(), other.end());
	}

	bool operator<=(const root_vector& other) const { return !(other < *this); }
	bool operator>(const root_vector& other) const  { return other < *this; }
	bool operator>=(const root_vector& other) const { return !(*this < other); }

private:
	// ================================================================
	// ポインタテーブル管理（フォールバック環境専用）
	// ================================================================

	/**
	 * @brief ポインタテーブルを解放する
	 *
	 * フォールバック環境でのみ動作する。
	 * ネイティブ環境では何もしない。
	 */
	void free_ptr_table()
	{
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		if (m_ptr_table)
		{
			std::free(m_ptr_table);
			m_ptr_table      = nullptr;
			m_table_capacity = 0;
		}
#endif
	}

	/**
	 * @brief 単一要素のテーブルエントリを設定する
	 *
	 * push_back/emplace_back後に呼ぶ。
	 * ネイティブ環境では何もしない。
	 *
	 * @param index 設定する要素のインデックス
	 */
	void on_element_added([[maybe_unused]] size_type index)
	{
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		m_ptr_table[index] = &m_base_ptr[index];
#endif
	}

	/**
	 * @brief 範囲のテーブルエントリを設定する
	 *
	 * コンストラクタ、resize、insert、assign等で複数要素を追加した後に呼ぶ。
	 * ネイティブ環境では何もしない。
	 *
	 * @param begin_idx 開始インデックス（含む）
	 * @param end_idx 終了インデックス（含まない）
	 */
	void on_elements_added([[maybe_unused]] size_type begin_idx, [[maybe_unused]] size_type end_idx)
	{
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		if (m_ptr_table)
		{
			for (size_type i = begin_idx; i < end_idx; ++i)
				m_ptr_table[i] = &m_base_ptr[i];
		}
#endif
	}

	/**
	 * @brief データの引っ越し後に全テーブルエントリを更新する
	 *
	 * grow()でm_base_ptrが変わった後に呼ぶ。
	 * ネイティブ環境では何もしない。
	 */
	void on_data_relocated()
	{
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		if (m_ptr_table)
		{
			for (size_type i = 0; i < m_size; ++i)
				m_ptr_table[i] = &m_base_ptr[i];
		}
#endif
	}

	// ================================================================
	// 容量管理
	// ================================================================

	/**
	 * @brief 指定した要素数が収まるように容量を確保する
	 *
	 * 現在のcapacityで足りる場合は何もしない。
	 * 足りない場合はgrow()で新しい領域を確保する。
	 *
	 * ネイティブ環境では初回確保で大きな仮想アドレス空間を予約するため、
	 * 2回目以降のgrowは仮想アドレス空間の枯渇を意味する。
	 *
	 * フォールバック環境ではポインタテーブルの上限を超えた場合に強制終了する。
	 *
	 * @param required_count 必要な要素数
	 */
	void ensure_capacity(size_type required_count)
	{
		if (required_count <= capacity())
		{
			return;
		}

#if defined(ROOT_VECTOR_STABLE_ADDRESS)
		if (m_base_ptr != nullptr)
		{
			std::fprintf(stderr,
				"[root_vector] 致命的エラー: 仮想アドレス空間の上限に達しました。\n"
				"  現在の容量: %zu 要素\n"
				"  要求された容量: %zu 要素\n"
				"  予約済みバイト数: %zu\n",
				capacity(), required_count, m_reserved_bytes);
			std::abort();
		}
#else
		if (m_ptr_table != nullptr && required_count > m_table_capacity)
		{
			std::fprintf(stderr,
				"[root_vector] 致命的エラー: ポインタテーブルの上限に達しました。\n"
				"  テーブル容量: %zu 要素\n"
				"  要求された容量: %zu 要素\n",
				m_table_capacity, required_count);
			std::abort();
		}
#endif

		grow(required_count);
	}

	/**
	 * @brief 指定した要素数分の物理メモリをコミットする
	 *
	 * 既にコミット済みの範囲で足りる場合は何もしない。
	 * 不足する場合はページ粒度に切り上げてコミットする。
	 *
	 * @param required_count 必要な要素数
	 */
	void ensure_committed(size_type required_count)
	{
		const size_t required_bytes = required_count * sizeof(T);
		if (required_bytes <= m_committed_bytes)
		{
			return;
		}

		const size_t new_committed_bytes = calc_commit_bytes(required_count, m_reserved_bytes);

		void* result = virtual_memory_allocator::commit(
			m_base_ptr, m_committed_bytes, new_committed_bytes
		);
		assert(result != nullptr && "物理メモリのコミットに失敗しました。");

		m_committed_bytes = new_committed_bytes;
	}

	/**
	 * @brief 容量を拡張する（新領域確保→ムーブ→旧破棄→旧解放）
	 *
	 * 新しい領域を予約し、既存要素をムーブで引っ越す。
	 * フォールバック環境では初回呼び出し時にポインタテーブルも確保し、
	 * データの引っ越し後に全テーブルエントリを更新する。
	 *
	 * @param min_count 最低限必要な要素数
	 */
	void grow(size_type min_count)
	{
#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
		// フォールバック環境: 初回呼び出し時にポインタテーブルを確保する
		if (!m_ptr_table)
		{
			static constexpr size_type DEFAULT_TABLE_CAPACITY = 1024 * 1024;
			m_table_capacity = std::max(DEFAULT_TABLE_CAPACITY, min_count);
			m_ptr_table = static_cast<T**>(std::malloc(m_table_capacity * sizeof(T*)));
			assert(m_ptr_table != nullptr && "ポインタテーブルのメモリ確保に失敗しました。");
		}
#endif

		const size_type new_capacity = calc_grow_capacity(min_count);
		const size_t new_reserved_bytes = align_up(new_capacity * sizeof(T), g_allocation_granularity);

		T* new_ptr = static_cast<T*>(virtual_memory_allocator::reserve(new_reserved_bytes));
		assert(new_ptr != nullptr && "メモリの予約に失敗しました。");

		// 既存要素分の物理メモリをコミット
		size_t new_committed_bytes = 0;
		if (m_size > 0)
		{
			new_committed_bytes = calc_commit_bytes(m_size, new_reserved_bytes);
			void* commit_result = virtual_memory_allocator::commit(new_ptr, 0, new_committed_bytes);
			assert(commit_result != nullptr && "物理メモリのコミットに失敗しました。");
		}

		// 既存要素をムーブ構築
		for (size_type i = 0; i < m_size; ++i)
		{
			new (&new_ptr[i]) T(std::move(m_base_ptr[i]));
		}

		// 旧領域の解放（要素破棄→メモリ解放の順）
		T* old_ptr = m_base_ptr;
		const size_t old_reserved_bytes = m_reserved_bytes;
		if (old_ptr)
		{
			destroy_range(0, m_size);
			virtual_memory_allocator::release(old_ptr, old_reserved_bytes);
		}

		m_base_ptr        = new_ptr;
		m_reserved_bytes  = new_reserved_bytes;
		m_committed_bytes = new_committed_bytes;

		// フォールバック環境: テーブルの中身を新アドレスに更新
		on_data_relocated();
	}

	/**
	 * @brief 拡張後の容量を算出する
	 *
	 * ネイティブ環境では初回確保時に大きな仮想アドレス空間を予約する。
	 * フォールバック環境ではstd::vectorと同じ2倍成長戦略を使用する。
	 *
	 * @param min_count 最低限必要な要素数
	 * @return 新しい容量（要素数）
	 */
	size_type calc_grow_capacity(size_type min_count) const
	{
		const size_type current_cap = capacity();

		if (current_cap == 0)
		{
#if defined(ROOT_VECTOR_STABLE_ADDRESS)
			static constexpr size_t DEFAULT_RESERVE_BYTES = 256ULL * 1024 * 1024;
			const size_type default_count = static_cast<size_type>(DEFAULT_RESERVE_BYTES / sizeof(T));
			return std::max(min_count, default_count);
#else
			return std::max(min_count, static_cast<size_type>(1));
#endif
		}

		return std::max(current_cap * 2, min_count);
	}

	// ================================================================
	// 要素操作ヘルパー
	// ================================================================

	/// 指定位置の要素を破棄して再構築する
	template<typename U>
	void reconstruct_at(size_type index, U&& value)
	{
		if (index < m_size) m_base_ptr[index].~T();
		new (&m_base_ptr[index]) T(std::forward<U>(value));
	}

	/// 指定位置の要素を破棄して引数転送で再構築する
	template<typename... Args>
	void emplace_at(size_type index, Args&&... args)
	{
		if (index < m_size) m_base_ptr[index].~T();
		new (&m_base_ptr[index]) T(std::forward<Args>(args)...);
	}

	/**
	 * @brief 要素を後方にシフトする（挿入用）
	 *
	 * [pos, m_size) の要素を [pos + count, m_size + count) に移動する。
	 */
	void shift_right(size_type pos, size_type count)
	{
		if (count == 0 || pos == m_size) return;

		for (size_type i = m_size; i > pos; --i)
		{
			const size_type src = i - 1;
			const size_type dst = src + count;

			if (dst >= m_size)
				new (&m_base_ptr[dst]) T(std::move(m_base_ptr[src]));
			else
				m_base_ptr[dst] = std::move(m_base_ptr[src]);
		}
	}

	/**
	 * @brief 要素を前方にシフトする（削除用）
	 *
	 * [pos + count, m_size) の要素を [pos, m_size - count) にムーブ代入する。
	 */
	void shift_left(size_type pos, size_type count)
	{
		if (count == 0) return;

		for (size_type i = pos + count; i < m_size; ++i)
			m_base_ptr[i - count] = std::move(m_base_ptr[i]);
	}

	// ================================================================
	// ユーティリティ
	// ================================================================

	/**
	 * @brief 指定した要素数に必要なコミットバイト数を算出する
	 *
	 * ページ粒度に切り上げたバイト数を返す。
	 * 予約バイト数を超えないように制限する。
	 */
	static size_t calc_commit_bytes(size_type element_count, size_t reserved_bytes)
	{
		const size_t needed_bytes  = element_count * sizeof(T);
		const size_t aligned_bytes = align_up(needed_bytes, g_page_size);
		return std::min(aligned_bytes, reserved_bytes);
	}

	/// 値を指定アライメントの倍数に切り上げる
	static size_t align_up(size_t value, size_t alignment)
	{
		return (value + alignment - 1) & ~(alignment - 1);
	}

	/**
	 * @brief 指定範囲の要素のデストラクタを呼び出す
	 *
	 * トリビアルデストラクタの型では何もしない。
	 */
	void destroy_range(size_type begin_index, size_type end_index)
	{
		if constexpr (!std::is_trivially_destructible_v<T>)
		{
			for (size_type i = begin_index; i < end_index; ++i)
				m_base_ptr[i].~T();
		}
	}

	// ================================================================
	// メンバ変数
	// ================================================================

	/** メモリ領域の先頭ポインタ */
	T* m_base_ptr = nullptr;

	/** 構築済み要素数 */
	size_type m_size = 0;

	/** コミット済みバイト数（常にページアライメント済み） */
	size_t m_committed_bytes = 0;

	/** 予約済みバイト数（= capacity() * sizeof(T) 以上） */
	size_t m_reserved_bytes = 0;

#if !defined(ROOT_VECTOR_STABLE_ADDRESS)
	/** ポインタテーブル（各エントリがデータ要素のアドレスを保持する） */
	T** m_ptr_table = nullptr;

	/** ポインタテーブルの容量 */
	size_type m_table_capacity = 0;
#endif
};

/// ADL用swap関数
template<typename T>
void swap(root_vector<T>& lhs, root_vector<T>& rhs) noexcept
{
	lhs.swap(rhs);
}