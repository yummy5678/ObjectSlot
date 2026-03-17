# ObjectSlot

C++17向けのヘッダーオンリー オブジェクトプールライブラリ。

## 設計思想

`std::shared_ptr`はオブジェクトをヒープ上にバラバラに確保するため、大量のオブジェクトを一括処理する場面ではキャッシュミスが増える。ObjectSlotは同じ型のオブジェクトをメモリ上に連続配置しつつ、`shared_ptr`と同等の参照カウント管理を提供する。

内部ストレージにOS仮想メモリ（Windows: `VirtualAlloc` / Linux・macOS: `mmap`）を活用した`root_vector`を使用し、要素のアドレスが生涯変わらないことを保証する。これにより全てのスマートポインタのGet()がゼロコスト（生ポインタ返却）で動作する。仮想メモリが使えない環境（Emscripten等）ではポインタテーブルによるフォールバックで同等の安定性を確保する。

## std::shared_ptr との違い

| | `std::shared_ptr` | ObjectSlot |
|---|---|---|
| メモリ配置 | ヒープ上にバラバラ | 連続メモリに配置 |
| 参照カウント | atomic（スレッドセーフ） | 非atomic（シングルスレッド想定） |
| 解放通知 | なし | Subscribe()でコールバック実行 |
| ポリモーフィック | `shared_ptr<Base>` | `SlotRef<Base>` |
| 一括イテレーション | 不可 | `ForEach()`で可能 |

## ポインタのサイズとアクセスコスト

全てのポインタ型は16バイトで統一されている。

| ポインタ | サイズ | Get()コスト（ネイティブ） | Get()コスト（フォールバック） |
|---|:---:|---|---|
| `SlotPtr<T>` | 16B | ゼロコスト | ポインタ2回辿り |
| `SignalSlotPtr<T>` | 16B | ゼロコスト | ポインタ2回辿り |
| `WeakSlotPtr<T>` | 16B | Lock()経由 | Lock()経由 |
| `WeakSignalSlotPtr<T>` | 16B | Lock()経由 | Lock()経由 |
| `SlotRef<T>` | 16B | ゼロコスト | ゼロコスト |
| `Subscription<T>` | 16B | — | — |
| `SubscriptionRef` | 16B | — | — |

ネイティブ環境（Windows / Linux / macOS）ではOS仮想メモリにより要素のアドレスが固定されるため、内部の`root_pointer`は生ポインタ（`T*`）を直接保持する。Get()は`return m_ptr`のゼロコスト。

フォールバック環境（Emscripten等）では`malloc`の再確保でアドレスが変わる可能性があるため、`root_pointer`はポインタテーブルのエントリアドレス（`T**`）を保持する。Get()は`return *m_handle`でポインタを2回辿る。データの引っ越し時にテーブルの中身が更新されるため、`root_pointer`自体の値は変わらない。

## 基本的な使い方

```cpp
#include "objectSlot/ObjectSlot.h"

struct Mesh {
    std::string name;
};

int main() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();

    // 作成（参照カウント = 1）
    SlotPtr<Mesh> mesh = pool.Create(Mesh{ "Box" });

    // コピー（参照カウント = 2）
    SlotPtr<Mesh> mesh2 = mesh;

    // 参照カウントが0になった時点で自動削除
    mesh = nullptr;
    mesh2 = nullptr;
}
```

### 解放通知

```cpp
auto& pool = SignalSlotSystem<Device>::GetInstance();
auto device = pool.Create(Device{ "GPU" });

Subscription<Device> sub = device.Subscribe([]() {
    std::cout << "デバイスが解放された" << std::endl;
});

device = nullptr;  // → コールバック実行 → 自動削除
```

複数の購読は登録の逆順に実行される。

### ポリモーフィック参照

```cpp
auto mesh = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "Box" });
auto sprite = RefSlotSystem<Sprite>::GetInstance().Create(Sprite{ "Player" });

std::vector<SlotRef<IDrawable>> drawables;
drawables.push_back(SlotRef<IDrawable>(mesh));
drawables.push_back(SlotRef<IDrawable>(sprite));

for (auto& d : drawables) {
    d->Draw();
}
```

## 使用上の注意

**購読コールバックでSlotPtrを参照キャプチャしないこと。** スコープを抜けた後にコールバックが実行されるとダングリング参照になる。値キャプチャを使うこと。

```cpp
// 危険
device.Subscribe([&buffer]() { buffer->Release(); });

// 安全
device.Subscribe([buffer]() { buffer->Release(); });
```

**プールはシングルトンで型ごとに1つ。** `ObjectSlotSystem<Mesh>::GetInstance()`は常に同じインスタンスを返す。

**参照カウントは非atomic。** マルチスレッドで同じスマートポインタを操作する場合は外部で同期が必要。

## 導入

`include/objectSlot` をインクルードパスに追加し、`#include "objectSlot/ObjectSlot.h"` するだけ。外部依存なし。

## ライセンス

MIT または MIT-0 のデュアルライセンス。お好きな方を選択できる。

- **MIT** — 著作権表示が必要
- **MIT-0** — 著作権表示不要
