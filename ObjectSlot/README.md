# ObjectSlot

C++17向けのヘッダーオンリー オブジェクトプールライブラリ。

## なぜ作ったか

`std::shared_ptr`はオブジェクトをヒープにバラバラに置くので、大量に扱うとキャッシュミスが増える。ObjectSlotは同じ型のオブジェクトをメモリ上に並べて置きつつ、`shared_ptr`と同じ感覚で使える。

ネイティブ環境（Windows / Linux / macOS）ではOS仮想メモリで要素のアドレスを固定するため、ポインタアクセスはゼロコスト。仮想メモリが使えない環境（Emscripten等）ではポインタテーブルで代替する。

## shared_ptr との違い

| | shared_ptr | ObjectSlot |
|---|---|---|
| メモリ配置 | バラバラ | 連続 |
| 参照カウント | atomic | 非atomic |
| 解放通知 | なし | あり |
| 一括処理 | 不可 | ForEach() |

## ポインタサイズ

全て16バイト。

| ポインタ | 役割 |
|---|---|
| `SlotPtr<T>` | 強参照（軽量） |
| `SignalSlotPtr<T>` | 強参照（解放通知あり） |
| `WeakSlotPtr<T>` | 弱参照 |
| `WeakSignalSlotPtr<T>` | 弱参照（解放通知あり） |
| `SlotRef<T>` | ポリモーフィック参照 |
| `Subscription<T>` | 購読管理 |
| `SubscriptionRef` | 購読管理（非テンプレート） |

## アクセスコスト

| ポインタ | ネイティブ | フォールバック |
|---|---|---|
| SlotPtr / SignalSlotPtr | ゼロコスト | ポインタ2回辿り |
| SlotRef | ゼロコスト | ゼロコスト |
| 弱参照 | Lock()経由 | Lock()経由 |

SlotPtr/SignalSlotPtrはフォールバック環境でポインタテーブル（`T**`）を経由するため2回辿りになる。

SlotRefは生ポインタ（`T*`）を直接持ち、再アロケーション時はプール側がポインタを書き換える。コピー・破棄のたびに登録・解除が走るが、SlotRefは長期保持が主な用途なので問題にならない。SlotPtr/SignalSlotPtrは頻繁にコピー・破棄されるため、登録コストの無い`root_pointer`方式を採用している。

## 使い方

```cpp
#include "objectSlot/ObjectSlot.h"

struct Mesh {
    std::string name;
};

int main() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();

    SlotPtr<Mesh> mesh = pool.Create(Mesh{ "Box" });
    SlotPtr<Mesh> mesh2 = mesh;   // 参照カウント増加

    mesh = nullptr;
    mesh2 = nullptr;  // 参照カウント0 → 自動削除
}
```

### 解放通知

```cpp
auto& pool = SignalSlotSystem<Device>::GetInstance();
auto device = pool.Create(Device{ "GPU" });

Subscription<Device> sub = device.Subscribe([]() {
    std::cout << "解放された" << std::endl;
});

device = nullptr;  // コールバック実行 → 削除
```

### ポリモーフィック参照

```cpp
auto mesh = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{});
auto sprite = RefSlotSystem<Sprite>::GetInstance().Create(Sprite{});

std::vector<SlotRef<IDrawable>> drawables;
drawables.push_back(SlotRef<IDrawable>(mesh));
drawables.push_back(SlotRef<IDrawable>(sprite));

for (auto& d : drawables) {
    d->Draw();
}
```

## 注意点

- **購読コールバックではSlotPtrを値キャプチャすること。** 参照キャプチャはダングリングの原因になる。
- **プールは型ごとにシングルトン。**
- **参照カウントは非atomic。** マルチスレッドでは外部同期が必要。

## 導入

`include/objectSlot` をインクルードパスに追加して `#include "objectSlot/ObjectSlot.h"` するだけ。外部依存なし。

## ライセンス

MIT または MIT-0 の選択制。

- **MIT** — 著作権表示が必要
- **MIT-0** — 著作権表示不要
