※ちょっと使用を見直し中。現在内容が不安定。

# ObjectSlot

C++17向けのヘッダーオンリー メモリプールライブラリ。

オブジェクトをメモリ上に連続配置し、参照カウント方式で自動管理する。

共有ポインタの機能を参考にしつつ、メモリの連続配置を目標に作成した。

## 必要要件

- C++17以上
- 外部依存なし
- Windows / Linux / macOS / Emscripten 対応

## 導入方法

`include/objectSlot` フォルダをインクルードパスに追加する。

```cpp
#include "objectSlot/ObjectSlot.h"
```

## 設計思想

std::shared_ptrはオブジェクトをヒープ上にバラバラに配置するため、大量のオブジェクトを扱う場合にキャッシュ効率が悪い。ObjectSlotは同じ型のオブジェクトをメモリ上に連続配置し、ForEachによる一括走査を可能にする。

参照カウント方式はshared_ptrと同じだが、atomic操作を行わないためシングルスレッド環境に限定される。

## プール種類

| プール | 通知 | SlotRef | 用途 |
|--------|------|---------|------|
| `ObjectSlotSystem<T>` | — | — | 軽量な要素管理 |
| `SignalSlotSystem<T>` | ○ | — | 解放通知が必要な場合 |
| `RefSlotSystem<T>` | ○ | ○ | 基底型で統一管理する場合 |

全てシングルトンで、`GetInstance()`で取得する。

## スマートポインタ

| ポインタ | 参照カウント | 通知 | 用途 |
|----------|-------------|------|------|
| `SlotPtr<T>` | ○ | — | 基本の所有権管理 |
| `SignalSlotPtr<T>` | ○ | ○ | 解放通知付き |
| `WeakSlotPtr<T>` | — | — | 非所有の弱参照。Lock()で強参照に変換 |
| `WeakSignalSlotPtr<T>` | — | ○ | 通知付き弱参照。Lock()で強参照に変換 |
| `SlotRef<T>` | ○ | ○ | 基底型でのポリモーフィック参照 |

全て16バイト。

## 使用例

### 基本

```cpp
auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
pool.Reserve(1000);

SlotPtr<Mesh> mesh = pool.Create(Mesh{ "Box" });
mesh->Draw();

SlotPtr<Mesh> mesh2 = mesh;   // 参照カウント増加
mesh = nullptr;
mesh2 = nullptr;               // ここで自動削除
```

### 弱参照

```cpp
SlotPtr<Mesh> mesh = pool.Create(Mesh{ "Box" });
WeakSlotPtr<Mesh> weak = mesh.GetWeak();

mesh = nullptr;

if (auto locked = weak.Lock()) {
    locked->Draw();
} else {
    // 既に破棄済み
}
```

### 解放通知

```cpp
auto device = devicePool.Create(Device{ "GPU" });

Subscription<Device> sub = device.Subscribe([]() {
    std::cout << "デバイスが解放される" << std::endl;
});

device.Reset();  // → コールバック実行 → 要素削除
```

### ポリモーフィック参照

```cpp
auto mesh = meshPool.Create(Mesh{ "Box" });
auto sprite = spritePool.Create(Sprite{ "Player" });

std::vector<SlotRef<IDrawable>> drawables;
drawables.push_back(SlotRef<IDrawable>(mesh));
drawables.push_back(SlotRef<IDrawable>(sprite));

for (auto& d : drawables) {
    d->Draw();
}
```

### エイリアシング

```cpp
auto mesh = meshPool.Create(Mesh{ "Target", 256 });

// meshの所有権を共有しつつ、メンバ変数を直接参照
SlotRef<std::string> nameRef(mesh, &mesh->name);
```

### EnableSlotFromThis

```cpp
class Node : public EnableSlotFromThis<Node> {
public:
    SlotPtr<Node> GetSelf() { return SlotPtrFromThis(); }
};

auto node = pool.Create(Node{});
auto self = node->GetSelf();
```

## 注意点

**Reserve()の使用を推奨** — プール作成直後にReserve()で容量を確保すること。未指定の場合、要素追加時にデフォルト値で自動初期化される。

**参照キャプチャの禁止** — 購読コールバックでSlotPtrをキャプチャする場合、参照キャプチャ（`&`）は使わないこと。値キャプチャを使用する。

```cpp
// 危険: 参照キャプチャ
device.Subscribe([&buffer]() { buffer->Release(); });

// 安全: 値キャプチャ
device.Subscribe([buffer]() { buffer->Release(); });
```

**弱参照のLock()** — WeakSlotPtr / WeakSignalSlotPtrは要素の生存を保証しない。アクセス前にLock()で強参照に変換すること。

**insert / eraseの副作用** — RootVector上でのinsert/eraseは要素をシフトするため、シフト先のアドレスには以前と異なるオブジェクトが配置される。プール内部でinsert/eraseは使用しない（フリーリスト方式で管理している）が、RootVectorを直接使う場合は注意。

**シングルスレッド前提** — 参照カウントはatomic操作を行わない。マルチスレッド環境で使用する場合は外部で排他制御が必要。

## ライセンス

MIT / MIT-0 デュアルライセンス。
