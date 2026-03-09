# ObjectSlot

C++17向けのヘッダーオンリー・オブジェクトプールライブラリです。
参照カウント付きスマートポインタ、解放通知の購読パターン、基底型へのポリモーフィック参照を提供します。

## 特徴

- **メモリ連続配置** — 同じ型のオブジェクトを`std::vector`で連続配置し、キャッシュ効率を高めます
- **世代番号によるダングリング検出** — 削除済みスロットの再利用時に、古いハンドルが無効であることを自動検出します
- **参照カウント** — `shared_ptr`のように、全ての参照が破棄されるとオブジェクトが自動的に削除されます
- **解放通知の購読** — オブジェクトが解放される際に、登録されたコールバックを逆順に実行します
- **ポリモーフィック参照** — 異なる具体型のオブジェクトを、基底インターフェースで統一的に管理できます
- **ヘッダーオンリー** — `#include`するだけで使えます。ビルド設定やリンクは不要です

## 導入

`include/objectSlot/` ディレクトリをプロジェクトのインクルードパスに追加し、ヘッダーをインクルードしてください。

```cpp
#include <objectSlot/ObjectSlot.h>
```

## 要件

- C++17以上

## 概要

### プールの種類

用途に応じて3種類のプールを使い分けます。

| プール | 説明 | 返すポインタ型 |
|--------|------|---------------|
| `ObjectSlotSystem<T>` | 軽量版。通知機能なし | `SlotPtr<T>` |
| `SignalSlotSystem<T>` | 解放通知の購読に対応 | `SignalSlotPtr<T>` |
| `RefSlotSystem<T>` | 購読 + ポリモーフィック参照に対応 | `SignalSlotPtr<T>` |

各プールはシングルトンで、`GetInstance()`でインスタンスを取得します。

### ポインタの種類

| ポインタ | サイズ | 説明 |
|----------|--------|------|
| `SlotPtr<T>` | 16B | 軽量な参照カウント付きポインタ |
| `SignalSlotPtr<T>` | 16B | 購読機能付きの参照カウントポインタ |
| `WeakSlotPtr<T>` | 16B | 参照カウントに影響しない弱参照 |
| `SlotRef<T>` | 16B | 基底型へのポリモーフィック参照。Get()はゼロコスト |
| `Subscription<T>` | — | 購読の有効期間を管理。破棄で自動解除 |

## 使い方

### 基本（SlotPtr）

```cpp
auto& slot = ObjectSlotSystem<Mesh>::GetInstance();

// 作成
auto mesh = slot.Create(Mesh{ "Box" });

// アクセス
mesh->Draw();
Mesh& ref = *mesh;

// コピー（参照カウント増加）
SlotPtr<Mesh> copy = mesh;

// 解放
mesh.Reset();     // または mesh = nullptr;
```

### 弱参照（WeakSlotPtr）

```cpp
auto mesh = slot.Create(Mesh{ "Box" });
WeakSlotPtr<Mesh> weak = mesh.GetWeak();

if (!weak.IsExpired()) {
    SlotPtr<Mesh> locked = weak.Lock();
    locked->Draw();
}
```

### 解放通知（SignalSlotPtr + Subscription）

依存関係のあるリソースの解放順序を制御します。
通知は登録の逆順に実行されるため、後から作られた依存リソースが先に解放されます。

```cpp
auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
auto device = deviceSlot.Create(Device{ "GPU" });

// 購読を登録（Subscriptionを購読者側で保持する）
Subscription<Device> sub = device.Subscribe([]() {
    // deviceが解放される時に実行される
});

// Subscriptionが破棄されると購読は自動解除される
```

購読者側のオブジェクトにSubscriptionをメンバとして持たせることで、購読者の寿命と購読の寿命を一致させることができます。

```cpp
struct Buffer {
    Subscription<Device> deviceSubscription;
    void Release() { /* バッファ解放処理 */ }
};

auto buffer = bufferSlot.Create(Buffer{});
Buffer* pBuffer = buffer.Get();

buffer->deviceSubscription = device.Subscribe([pBuffer]() {
    pBuffer->Release();
});

// bufferが先に破棄された場合 → Subscription破棄 → 購読自動解除
// deviceが先に破棄された場合 → コールバック実行 → バッファ解放
```

### ポリモーフィック参照（SlotRef）

異なる具体型のオブジェクトを基底インターフェースで統一的に管理します。
`SlotRef`の`Get()`はキャッシュされた生ポインタを返すだけなので、アクセスコストはゼロです。

```cpp
class IDrawable {
public:
    virtual ~IDrawable() = default;
    virtual void Draw() const = 0;
};

class Mesh : public IDrawable { /* ... */ };
class Sprite : public IDrawable { /* ... */ };

auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
auto& spriteSlot = RefSlotSystem<Sprite>::GetInstance();

auto mesh = meshSlot.Create(Mesh{ "Box" });
auto sprite = spriteSlot.Create(Sprite{ "Player" });

// 異なる型を基底型のvectorで管理
std::vector<SlotRef<IDrawable>> drawables;
drawables.push_back(SlotRef<IDrawable>(mesh));
drawables.push_back(SlotRef<IDrawable>(sprite));

for (auto& d : drawables) {
    d->Draw();  // ゼロコストアクセス
}
```

`SlotRef`は参照カウントに参加するため、元の`SignalSlotPtr`が破棄されても`SlotRef`が生きている限りオブジェクトは維持されます。

プールの再アロケーション時には、登録された全ての`SlotRef`のポインタが自動的に更新されます。

### プール操作

```cpp
auto& slot = ObjectSlotSystem<Mesh>::GetInstance();

slot.Reserve(100);          // メモリを事前確保
slot.SetMaxCapacity(256);   // 最大容量を設定（0で無制限）

slot.Count();               // 有効な要素数
slot.Capacity();            // 確保済みスロット数

slot.ForEach([](SlotHandle handle, Mesh& mesh) {
    mesh.Draw();            // 全要素に処理を実行
});

slot.ShrinkToFit();         // 末尾の未使用スロットを解放
slot.Clear();               // 全要素を削除
```

## 継承構造

```
SlotControlBase                  （非テンプレート基底。参照カウント管理）
  └ ObjectSlotSystemBase<T>      （型依存データストレージ）
      └ ObjectSlotSystem<T>      （軽量版シングルトンプール）
      └ SignalSlotSystemBase<T>  （購読リスト管理）
          └ SignalSlotSystem<T>  （通知付きシングルトンプール）
          └ RefSlotSystemBase<T> （SlotRefポインタ更新管理）
              └ RefSlotSystem<T> （全機能対応シングルトンプール）
```

## 設計上の注意

### SlotRefの制約

`SlotRef`によるポリモーフィック参照は単一継承のみをサポートしています。多重継承の場合、`static_cast`によるポインタオフセット調整が正しく行われない可能性があります。

### SlotRefの使用にはRefSlotSystemが必要

`SlotRef`はプール再アロケーション時にポインタを自動更新する仕組みに依存しています。`ObjectSlotSystem`や`SignalSlotSystem`で作成した要素を`SlotRef`に変換した場合、ポインタ更新の登録先がないため、再アロケーション後にダングリングポインタになる可能性があります。

`SlotRef`を使用する型は必ず`RefSlotSystem`で管理してください。

### ラムダキャプチャの注意

購読コールバックでSlotPtrをキャプチャする場合、参照キャプチャ（`&`）ではなく、生ポインタを値キャプチャしてください。

```cpp
// 危険: SlotPtrの参照キャプチャ
auto buffer = slot.Create(Buffer{});
device.Subscribe([&buffer]() { buffer->Release(); });  // ダングリングの危険

// 安全: 生ポインタの値キャプチャ
Buffer* pBuffer = buffer.Get();
device.Subscribe([pBuffer]() { pBuffer->Release(); }); // OK
```

## ファイル構成

```
include/objectSlot/
  ObjectSlot.h                  ← まとめヘッダー（これだけインクルードすれば全機能使える）
  detail/
    SlotHandle.h                ← 世代番号付きハンドル
    SlotControlBase.h           ← 非テンプレートの参照カウント基底
    ObjectSlotSystemBase.h      ← 型依存データストレージ基底
    ObjectSlotSystem.h          ← 軽量版シングルトンプール
    SlotPtr.h                   ← 軽量スマートポインタ
    WeakSlotPtr.h               ← 弱参照ポインタ
    SignalSlotSystemBase.h      ← 購読通知の基底
    SignalSlotSystem.h          ← 通知付きシングルトンプール
    SignalSlotPtr.h             ← 通知機能付きスマートポインタ
    Subscription.h              ← 購読の有効期間管理
    RefSlotSystemBase.h         ← SlotRefポインタ更新の基底
    RefSlotSystem.h             ← 全機能対応シングルトンプール
    SlotRef.h                   ← ポリモーフィック参照ポインタ
```

## ライセンス

このプロジェクトはMITライセンスのもとで公開されています。
著作権表示なしでの利用を希望する場合はMIT-0ライセンスも選択可能です。
