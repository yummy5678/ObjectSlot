# ObjectSlot

C++17 向けのヘッダーオンリー オブジェクトプールライブラリ。

同じ型のオブジェクトを 1 か所に並べて置き、`std::shared_ptr` と同じ感覚のポインタ（強参照・弱参照・自動解放）で扱う。

## 目次

1. [何を解決するか](#何を解決するか)
2. [全体像](#全体像)
3. [最小の例](#最小の例)
4. [使い方](#使い方)
5. [仕組み](#仕組み)
6. [使う上での注意](#使う上での注意)
7. [実験的な実装：4 バイトポインタ版](#実験的な実装4-バイトポインタ版)
8. [導入・テスト・ライセンス](#導入)

## 何を解決するか

`std::shared_ptr` の所有権管理（参照カウント・自動解放・弱参照）と、`std::vector` の連続配置を合わせたようなポインタ型。要素は一度置いたら動かないので、ポインタはそのまま持ち続けられる。

## 全体像

### プールとポインタ

プールは型ごとに 3 種類あり、用途に応じて選ぶ。プールから `Create()` で要素を作ると、対応するポインタが返る。

| プール | 用途 | 返るポインタ | 弱参照 |
|---|---|---|---|
| `ObjectSlotSystem<T>` | 一番軽い。通知が要らない値（座標など） | `SlotPtr<T>` | `WeakSlotPtr<T>` |
| `SignalSlotSystem<T>` | 要素が解放される時に通知を受けたい | `SignalSlotPtr<T>` | `WeakSignalSlotPtr<T>` |

どのプールも型ごとのシングルトンで、`GetInstance()` で取得する。`RefSlotSystem<T>` は `SignalSlotSystem<T>` の別名（以前は `SlotRef` 専用のプールだったが、今はどのプールの要素からでも `SlotRef` を作れる）。

### ポインタの種類

| ポインタ | 所有権 | 役割 | サイズ |
|---|---|---|---|
| `SlotPtr<T>` / `SignalSlotPtr<T>` | 持つ | 要素にアクセスする。最後の 1 本が消えると要素が削除される | 16 B |
| `WeakSlotPtr<T>` / `WeakSignalSlotPtr<T>` | 持たない | 要素がまだ生きているかを確認し、`Lock()` で強参照に戻す | 16 B |
| `SlotRef<Base>` | 持つ | 型の違う要素を基底型で扱う。要素の一部（メンバ）を指すこともできる | 16 B |
| `Subscription<T>` / `SubscriptionRef` | – | 解放通知の購読。破棄すると自動で解除される | 16 B |

64 ビット環境の値。32 ビット環境ではそれぞれ 8〜12 バイト。

### プールとポインタの関係

プールは型ごとに 1 つあり、中に「要素本体の並び」「位置一覧（スロット番号 → 本体のアドレス）」「スロットごとの管理データ（参照カウント・世代番号・生死フラグ）」「生存一覧（`ForEach` の走査順）」を持つ。

ポインタが持つのは**プールのアドレスとスロット番号**で、要素本体の場所は位置一覧で引く。本体は `Compact()` で詰め直せるが、スロット番号は変わらないのでポインタはそのまま使える。コピーや破棄はスロット番号で管理データの参照カウントを増減し、弱参照の `Lock()` は世代番号が一致すれば `SlotPtr` を作る。

世代番号は要素を削除するたびに増える。同じスロットが別の要素に再利用されても、古い弱参照は世代番号が合わないので「無効」と判定される。

## 最小の例

```cpp
#include "objectSlot/ObjectSlot.h"

struct Position { float x = 0.0f, y = 0.0f, z = 0.0f; };

int main() {
    auto& pool = ObjectSlotSystem<Position>::GetInstance();

    SlotPtr<Position> player = pool.Create(Position{ 0.0f, 1.0f, 0.0f });
    player->x += 1.0f;

    SlotPtr<Position> cameraTarget = player;   // 同じ要素を共有（参照カウント 2）
    cameraTarget->y = 2.0f;                     // player 側からも同じ値が見える

    player = nullptr;                           // 参照カウント 1
    cameraTarget = nullptr;                     // 参照カウント 0 → 要素が削除される
}
```

`Position` を 1 万個作れば、1 万個が `ObjectSlotSystem<Position>` の中に隙間なく並ぶ。

## 使い方

### 弱参照

```cpp
WeakSlotPtr<Position> target = player.GetWeak();

if (SlotPtr<Position> locked = target.Lock()) {   // 生きていれば強参照が返る
    locked->x += 1.0f;
}
```

### 全要素をまとめて処理する

```cpp
ObjectSlotSystem<Position>::GetInstance().ForEach([](SlotHandle handle, Position& position) {
    position.y -= 9.8f * deltaTime;
});
```

生存している要素だけを順に訪れる。走査中に削除しても安全。`SortActiveIndexList()` を呼ぶと走査順がメモリ順に戻る。

### 解放時に通知を受ける

```cpp
auto& pool = SignalSlotSystem<Transform>::GetInstance();
SignalSlotPtr<Transform> parent = pool.Create(Transform{});

Subscription<Transform> sub = parent.Subscribe([]() {
    // parent が削除される直前に呼ばれる
});
```

通知は登録の逆順。`Subscription` を破棄すると購読は解除される。コールバックでポインタを使う時は値でキャプチャする。

### 型の違う要素を基底型で扱う

```cpp
auto mesh   = SignalSlotSystem<Mesh>::GetInstance().Create(Mesh{});     // Mesh   : IDrawable
auto sprite = SignalSlotSystem<Sprite>::GetInstance().Create(Sprite{}); // Sprite : IDrawable

std::vector<SlotRef<IDrawable>> drawables{ mesh, sprite };
for (auto& drawable : drawables) drawable->Draw();

SlotRef<Position> positionRef(mesh, &mesh->position);   // 要素の一部だけを指す（所有権は要素全体）
```

### 要素の内部から自分自身を指す

`EnableSlotFromThis<T>` を継承すると、要素の中で `SlotPtrFromThis()` / `WeakSlotPtrFromThis()`（`SignalSlotSystem` なら `SignalSlotPtrFromThis()` / `WeakSignalSlotPtrFromThis()`）が使える。`std::enable_shared_from_this` と同じ。

### 本体を詰め直す

削除を繰り返すと要素本体の並びに空きが混ざり、走査が飛び飛びになって物理メモリも返せない。シーン切り替えなど処理が止まってよい時に `Compact()` を呼ぶと、生存している本体を先頭から隙間なく並べ直し、末尾の物理メモリを返す。

```cpp
ObjectSlotSystem<Position>::GetInstance().Compact();
```

- `SlotPtr` / 弱参照 / `SlotRef` / 購読は全てスロット番号を基準にしているので、呼び出し後もそのまま使える
- 要素はムーブ構築で移される。要素のアドレスを生ポインタや参照で別に持っていた場合だけ無効になる
- 詰め直した後は `ForEach` の走査順が本体の並び順になる
- 自動では呼ばれない。呼ぶタイミングは利用側が決める

### プールを操作する

| 操作 | 内容 |
|---|---|
| `Count()` | 生存している要素数 |
| `Reserve(n)` | n 要素分の領域を先に確保する |
| `Clear()` | 全要素を削除する。残っている古いポインタは、この後触ってはいけない |
| `Compact()` | 生存している本体を先頭に詰め直し、末尾の物理メモリを返す |
| `ShrinkToFit()` | 末尾の空きスロット・空き本体を切り詰め、物理メモリを OS に返す。本体は動かさない |
| `SetMaxCapacity(n)` | 生存数の上限。超えると `Create()` が空のポインタを返す |
| `GetMemoryUsage()` | 物理メモリを消費している内訳 |

## 仕組み

### 要素が勝手に動かない理由

プールは最初に 256MB 分のアドレスを OS から**予約**しておき、要素が増えた分だけ**物理メモリを割り当てる**。アドレスは最初から決まっているので、要素が増えても引っ越さない。

- 予約だけでは物理メモリを消費しない。実際に使うメモリは要素数に比例する
- 削除した要素の場所は次の `Create()` で再利用する
- 要素を削除しても物理メモリはすぐには返さない。`ShrinkToFit()` で末尾の空き分を、`Compact()` で詰め直した後の空き分を返す

要素本体が動くのは `Compact()` を呼んだ時だけ。その時は位置一覧を書き換えるので、スロット番号を持つポインタは影響を受けない。

WebAssembly では仮想メモリが使えないので `malloc` で確保し、足りなくなると倍の領域に引っ越す。引っ越した時も位置一覧を作り直すので、ポインタは壊れない。要素数の上限はメモリ量だけで決まる。

### 参照カウントと世代番号

プールは要素ごとに次の管理データを持つ。

| データ | 役割 |
|---|---|
| 参照カウント | 強参照の本数。0 になると要素を削除する |
| 世代番号 | 削除のたびに増える。古い弱参照やハンドルを見分ける |
| 生死フラグ | 削除済みかどうか |
| 位置一覧 | スロット番号 → 本体のアドレス。`Compact()` で本体が動いた時に書き換わる |
| 生存一覧 | `ForEach` が走査するスロット番号の並び。削除時は末尾の要素を空いた位置に移して詰める |

要素 1 個あたり 28 バイト（`SignalSlotSystem` は購読リストが加わる）。

## 使う上での注意

**要素の型に求める条件**
- ムーブ構築が可能であること（`Create` で受け取った値を領域に構築する）
- デフォルト構築が可能であること（削除したスロットに空のオブジェクトを置くため）

**上限**
- 1 プールあたり 256MB。超えると強制終了する（WebAssembly ではこの上限はなく、メモリ量だけで決まる）

**やってはいけないこと**
- `Clear()` / `ShrinkToFit()` の後に、残っていた古いポインタを触る
- `Compact()` の後に、要素のアドレスを保持していた生ポインタや参照を使う（`SlotPtr` 等は問題ない）
- 購読コールバックでポインタを参照キャプチャする（ダングリングの原因になる。値でキャプチャする）
- 無効なポインタで `->` や `*` を使う（検証が必要なら `Get()` を使う）
- 複数スレッドから同期なしで触る（参照カウントは非 atomic）

**できないこと**
- 要素本体は連続しているが、削除済みの場所が混ざるので `T[]` として一括で外部に渡すことはできない

## 実験的な実装：4 バイトポインタ版

`include/objectSlot/experimental_detail/` に、ポインタを **4 バイト** にした実装が入っている。インターフェースは同じで、マクロで切り替える。

```cpp
// インクルード前に定義する（プロジェクトのプリプロセッサ定義でも可）
#define OBJECT_SLOT_USE_EXPERIMENTAL
#include "objectSlot/ObjectSlot.h"
```

定義しなければ通常の実装が使われる。両方とも同じクラス名を使うため、1 つの実行ファイルの中で混在はできない。OS の仮想メモリ機能が使える 64 ビット環境専用で、WebAssembly など動かない環境では定義していても通常の実装に切り替わる。

### 通常の実装との違い

**ポインタが小さい**

| | 通常 | 実験版 |
|---|---|---|
| `SlotPtr` / `SignalSlotPtr` | 16 B | **4 B** |
| `WeakSlotPtr` / `WeakSignalSlotPtr` | 16 B | **8 B** |
| `Subscription` | 16 B | 8 B |
| `SubscriptionRef` | 16 B | 12 B |
| `SlotRef` | 16 B | 16 B |

通常の実装は「プールへのポインタ ＋ スロット番号」を持つ。実験版は 32 ビットの**圧縮値**（どのプールの、どのスロットか）だけを持ち、アドレスは計算で求める。`SlotRef` だけは要素の型を知らないので、見出しへのポインタ（8 バイト）と圧縮値・バイト差（4 バイトずつ）を持ち、通常の実装と同じ 16 バイトになる。`SlotPtr` を 3 本持つ構造体なら 48 バイト → 12 バイト、100 万本の配列なら 15MB → 3MB になる。

**参照カウントの置き場所が違う**

通常の実装はプール側の配列に参照カウントを持つので、ポインタのコピー・破棄のたびにプールを引く。実験版はスロットごとの**見出し**（16 バイト）に参照カウントと世代番号を持ち、ポインタから直接届く。プールに触るのは参照カウントが 0 になって削除する時だけ。

**古いポインタが安全**

通常の実装では `Clear()` の後に古いポインタを触ると未定義動作になる。実験版は `Clear()` しても見出しが残るので、古いポインタをコピー・破棄しても何も起こらず、弱参照は「無効」と判定される。`ShrinkToFit()` で切り詰めた範囲を指す古い強参照だけは触ってはいけない。

**要素の型にデフォルトコンストラクタが不要**

通常の実装は削除したスロットに空のオブジェクトを置くのでデフォルト構築が必要だが、実験版は削除したスロットを未構築のまま残す。

**動く環境が限られる**

実験版は「型ごとに 4GB の仮想アドレスを予約する」ことを前提にしているので、OS の仮想メモリ機能が使える 64 ビット環境（Windows / Linux / macOS の x64・ARM64）でしか動かない。WebAssembly と 32 ビット環境では、マクロを定義していても自動的に通常の実装に切り替わる（コンパイル時にその旨のメッセージが出る）。外側の API は同じなので利用側のコードは変わらないが、ポインタの大きさなどは通常の実装のものになる。

**メモリ**

要素 1 個あたりの管理データは 28 バイト → 24 バイト（見出し 16 ＋ 生存一覧など 8）でほぼ同じ。ポインタが小さい分だけ合計は減り、ポインタを多く持つ設計ほど差が大きくなる。

仮想アドレスの使い方は大きく違う。通常の実装はプールごとに 256MB を予約するが、実験版は型ごとに 4GB を予約するので、プロセスの仮想サイズは「型の数 × 4GB」に見える。物理メモリの消費はどちらもコミットしたページ分だけで変わらない。

**性能**

| 操作 | 傾向 |
|---|---|
| ポインタのコピー・破棄、弱参照の `Lock()` | 実験版が速い（プールを引かない） |
| `SlotRef` の生成・破棄 | 実験版が速い（登録簿がない） |
| `->` でのアクセス、`ForEach` | 同等 |
| 要素の生成・削除 | 通常の実装がやや速い（実験版は見出しの初期化が加わる） |

### 仕組み

**ポインタを半分にする考え方**

64 ビットのアドレスを 2 つに分けて考える。

```
64 ビットのアドレス = [ 上位 32 ビット ][ 下位 32 ビット ]
```

型ごとに 4GB（＝ 2^32 バイト）の仮想アドレス空間を、**上位 32 ビットが同じになる位置**に予約する。すると、その型の要素はどれも上位 32 ビットが共通で、下位 32 ビットだけが違う。上位 32 ビットは型ごとに 1 つ覚えておけばよいので、ポインタは下位 32 ビットだけを持てば足りる。

```
要素のアドレス = 型ごとの基底（上位 32 ビット）＋ ポインタが持つ値（下位 32 ビット）
```

型はテンプレート引数で決まるので、`SlotPtr<Position>` は `Position` 用の基底を、`SlotPtr<Rotation>` は `Rotation` 用の基底を使う。

**圧縮値の内訳**

4GB の領域は 256MB × 16 の区画に分け、同じ型のプールごとに 1 区画を割り当てる。下位 32 ビットのうち上 4 ビットが区画番号（＝どのプールか）、残り 28 ビットがスロット番号になる。

```
ポインタの値（32 ビット） = [ 区画番号 4 ビット ][ スロット番号 28 ビット ]
```

区画の中は「位置表」「見出しの並び」「本体の並び」の 3 つに分かれる。位置表と見出しはスロット番号で引き、本体は位置表の値で引く。

**位置表と見出し**
位置表はスロット番号 → 本体の位置（区画番号 ＋ 区画内オフセット）で、区画の先頭にある。見出しは参照カウント・世代番号・自分の圧縮値・プール番号。どちらも「区画の先頭 ＋ 定数 ＋ スロット番号 × 大きさ」で届き、動かない。`Compact()` は本体を詰め直して位置表を書き換える。

位置表を区画の先頭に置いているのは、項目の位置が「区画の先頭 ＋ 番号 × 4」となって要素の型に依存しないようにするため。型を知らない `SlotRef` でも、見出しのアドレスから区画の先頭を求め、自分の圧縮値で位置表を引いて本体に届く。

まとめると、各操作で読む場所は次のとおり。

| 操作 | 読む場所 |
|---|---|
| `SlotPtr` の `->` | 位置表[番号] → 基底 ＋ 値 |
| `SlotPtr` のコピー・破棄 | 見出し[番号]（基底 ＋ 定数 ＋ 番号 × 16） |
| `SlotRef` の `->` | 区画の先頭 ＋ 番号 × 4 → 基底 ＋ 値 ＋ バイト差 |
| `SlotRef` のコピー・破棄 | 見出し（アドレスを直接持っている） |

**実メモリ**
予約した 4GB は物理メモリを消費しない。消費するのはコミットしたページ（Windows / Linux では 4KB）だけで、本体・位置表・見出しをそれぞれページ単位で増やす。

| 状態（要素 12 バイト） | 物理メモリ |
|---|---|
| プールに要素 1 個 | 約 12KB（本体・位置表・見出しが 1 ページずつ） |
| 要素 100 万個 | 約 47MB（本体 12MB ＋ 位置表 4MB ＋ 見出し 16MB ＋ 管理データ） |
| 全て解放後に `ShrinkToFit()` | 約 16MB（管理用 vector の残り） |

**WebAssembly**
WebAssembly（wasm32）は線形メモリが最大 4GB で、予約とコミットを分ける仕組みもないため、上の配置は作れない。この環境では通常の実装に切り替わる。

### 実験版の制限
- 1 プールの上限 256MB には位置表（4 バイト／要素）と見出し（16 バイト／要素）も含まれる。要素 12 バイトなら約 830 万個、64 バイトなら約 320 万個
- 同じ型のプールは最大 15 個（圧縮値のタグが 4 ビットのため。通常は `ObjectSlotSystem` / `SignalSlotSystem` の 2 つで足りる）
- OS の仮想メモリ機能が使える 64 ビット環境専用。WebAssembly と 32 ビット環境では通常の実装に切り替わる
- 型ごとに予約した 4GB はプロセス終了まで返さない。プールを破棄してもコミット済みのページは返さないので、返したい時は破棄前に `ShrinkToFit()` を呼ぶ（プールはシングルトンなので通常は問題にならない）
- 仮想アドレスの量を制限している環境（`ulimit -v` など）では 4GB の予約に失敗し、起動時に強制終了する。その場合はマクロを外して通常の実装を使う

### どちらの実装が動いているか調べる
`SlotTraits` に実装ごとの事実がまとまっている。利用側のコードでマクロを直接分岐に使う必要はない。

```cpp
SlotTraits::IMPLEMENTATION_NAME              // 表示名
SlotTraits::STRONG_POINTER_BYTES             // SlotPtr のサイズ
SlotTraits::SUPPORTS_STALE_POINTER_SAFETY    // Clear() 後の古いポインタが安全か
SlotTraits::REQUIRES_DEFAULT_CONSTRUCTOR     // T にデフォルトコンストラクタが必要か
```

## 導入

ヘッダーオンリーで外部依存なし。`include/` をインクルードパスに追加して `#include "objectSlot/ObjectSlot.h"` するだけ。

```
include/objectSlot/         ライブラリ本体（このフォルダだけコピーすれば使える）
  ObjectSlot.h              入口。これだけを #include する
  detail/                   通常の実装
  experimental_detail/      実験版の実装
  thirdparty/rootVector/    仮想メモリの予約・コミットを扱う部品
tests/main.cpp              機能テストとベンチマーク
tools/ObjectSlot.natvis     Visual Studio のデバッガ表示定義
build/vs/                   Visual Studio のソリューション
CMakeLists.txt              CMake 用
```

**CMake から使う**

```cmake
add_subdirectory(ObjectSlot)   # または FetchContent
target_link_libraries(myapp PRIVATE ObjectSlot::ObjectSlot)
```

`ObjectSlot::ObjectSlot` は `INTERFACE` ライブラリで、インクルードパスと C++17 の要求だけを伝える。

**Visual Studio のデバッガ表示**

`tools/ObjectSlot.natvis` をプロジェクトに「既存の項目の追加」で加えるか、`%USERPROFILE%\Documents\Visual Studio 2022\Visualizers\` に置く。通常・実験版の両方の表示定義が入っており、デバッガは動いている方を自動で使う。

## テストとベンチマーク

`tests/main.cpp` が機能テストとベンチマークを兼ねている。先頭の `#define OBJECT_SLOT_USE_EXPERIMENTAL` のコメントを外す（またはプロジェクトのプリプロセッサ定義に追加する）と実験版で、そのままだと通常の実装でビルドされる。

- Visual Studio：`build/vs/objectSlot.sln` を開く
- CMake：`cmake -S . -B build/cmake -DOBJECT_SLOT_USE_EXPERIMENTAL=ON` のあと `cmake --build build/cmake`

```
objectSlot_test               # テスト + ベンチマーク
objectSlot_test --no-bench    # テストのみ
objectSlot_test --bench-only  # ベンチマークのみ
objectSlot_test --quick       # 計測の規模を縮小
```

検証済みの環境：MSVC（Windows x64）、g++ / clang（Linux x64、ASan / UBSan）、Emscripten 4.0（wasm32、Node.js。通常の実装のみ）。

## ライセンス

MIT または MIT-0 の選択制。

- **MIT** — 著作権表示が必要
- **MIT-0** — 著作権表示不要
