# ObjectSlot

C++17向けのヘッダーオンリー メモリプールライブラリ。
オブジェクトをメモリ上に連続配置し、参照カウント方式で自動管理する。

個人のゲームエンジン開発用に作成したものです。
設計や仕様は今後変更される可能性があります。
また、網羅的なテストはまだ行っていません。
削除の通知機能版も追加するか検討中です。

## 特徴

- **メモリ連続配置** - キャッシュ効率の向上
- **参照カウント** - `shared_ptr`風の自動メモリ管理
- **弱参照** - `weak_ptr`風の参照
- **世代番号** - 削除済みハンドルの安全な検出
- **解放通知の購読** - リソース間の依存関係を安全に管理
- **ヘッダーオンリー** - 導入が簡単

## 必要要件

- C++17以上
- 標準ライブラリのみ使用（外部依存なし）

## 導入方法

`include` ディレクトリをプロジェクトのインクルードパスに追加し、以下の1行でインクルードする。
```cpp
#include <ObjectSlot/ObjectSlot.h>
```

## 2つのプール

用途に応じて2種類のプールを使い分ける。

| クラス | 返すポインタ | 用途 |
|--------|-------------|------|
| `ObjectSlotSystem<T>` | `SlotPtr<T>` | 通知不要な一般オブジェクト |
| `SignalSlotSystem<T>` | `System<T>` | 解放時に他のオブジェクトへ通知が必要なもの |

## 基本的な使い方

### SlotPtr（軽量版）
```cpp
#include <ObjectSlot/ObjectSlot.h>

struct Mesh {
    std::string name;
    void Draw() { /* 描画処理 */ }
};

int main() {
    auto& slot = ObjectSlotSystem<Mesh>::GetInstance();

    // 作成
    SlotPtr<Mesh> mesh = slot.Create(Mesh{ "Box" });
    mesh->Draw();

    // コピー（参照カウント増加）
    SlotPtr<Mesh> mesh2 = mesh;

    // 解放
    mesh = nullptr;
    mesh2 = nullptr;  // 参照カウント0 → 自動削除
}
```

### 弱参照
```cpp
SlotPtr<Mesh> mesh = slot.Create(Mesh{ "Box" });
WeakSlotPtr<Mesh> weak = mesh.GetWeak();

mesh = nullptr;

if (weak.IsExpired()) {
    std::cout << "既に破棄済み" << std::endl;
}

if (auto locked = weak.Lock()) {
    locked->Draw();
}
```

### 容量管理
```cpp
auto& slot = ObjectSlotSystem<Mesh>::GetInstance();

slot.Reserve(1000);           // 事前確保
slot.SetMaxCapacity(100);     // 最大容量を設定（0で無制限）

if (slot.CanCreate()) {
    auto mesh = slot.Create(Mesh{ "Box" });
}

slot.ShrinkToFit();           // 未使用メモリを解放
```

## 解放通知の購読（SignalSlotSystem）

他のオブジェクトが依存するリソースには `SignalSlotSystem` を使う。
`Subscribe()` で解放通知を購読し、返される `Subscription` を購読者側で保持する。
```cpp
struct Device {
    std::string name;
};

struct Buffer {
    std::string name;
    Subscription<Device> deviceSub;  // 購読オブジェクトを保持

    void Release() { /* GPU リソース解放 */ }
};

int main() {
    auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
    auto& bufferSlot = ObjectSlotSystem<Buffer>::GetInstance();

    auto device  = deviceSlot.Create(Device{ "GPU_0" });
    auto buffer1 = bufferSlot.Create(Buffer{ "VertexBuffer" });
    auto buffer2 = bufferSlot.Create(Buffer{ "IndexBuffer" });

    Buffer* p1 = buffer1.Get();
    Buffer* p2 = buffer2.Get();

    // deviceが解放される時に実行するコールバックを登録
    buffer1->deviceSub = device.Subscribe([p1]() { p1->Release(); });
    buffer2->deviceSub = device.Subscribe([p2]() { p2->Release(); });

    device.Reset();
    // → buffer1, buffer2のRelease()が自動的に呼ばれる
}
```

### 購読の自動解除

`Subscription` が破棄されると購読は自動的に解除される。
購読者が先に消えた場合、通知は送られない。
```cpp
{
    auto device = deviceSlot.Create(Device{ "GPU_0" });

    {
        auto buffer = bufferSlot.Create(Buffer{ "TempBuffer" });
        Buffer* p = buffer.Get();
        buffer->deviceSub = device.Subscribe([p]() { p->Release(); });
    }
    // ← bufferが破棄 → Subscription破棄 → 購読解除

    device.Reset();
    // ← 購読は解除済みなので何も起きない
}
```

## API

### ObjectSlotSystem / SignalSlotSystem

| メソッド | 説明 |
|--------|------|
| `GetInstance()` | シングルトンインスタンスを取得 |
| `Create(T&&)` | 要素を作成してポインタを返す |
| `Reserve(size_t)` | メモリを事前確保 |
| `SetMaxCapacity(size_t)` | 最大容量を設定（0で無制限） |
| `GetMaxCapacity()` | 最大容量を取得 |
| `CanCreate()` | 作成可能か判定 |
| `Count()` | 有効な要素数 |
| `Capacity()` | 確保済みスロット数 |
| `ShrinkToFit()` | 末尾の未使用メモリを解放 |
| `Clear()` | 全要素を削除 |
| `ForEach(Func)` | 全要素に対して処理を実行 |

### SlotPtr

| メソッド | 説明 |
|--------|------|
| `Get()` | 要素へのポインタを取得 |
| `IsValid()` | 有効か判定 |
| `UseCount()` | 参照カウントを取得 |
| `GetWeak()` | 弱参照を生成 |
| `Reset()` | 参照を解放 |
| `GetHandle()` | SlotHandleを取得 |

### System（SignalSlotSystem用）

SlotPtrの全機能に加えて以下を持つ。

| メソッド | 説明 |
|--------|------|
| `Subscribe(Func)` | 解放通知を購読し、Subscriptionを返す |

### WeakSlotPtr

| メソッド | 説明 |
|--------|------|
| `Lock()` | SlotPtrに昇格 |
| `IsExpired()` | 無効になったか判定 |
| `Reset()` | 参照をリセット |

### Subscription

| メソッド | 説明 |
|--------|------|
| `Unsubscribe()` | 購読を手動で解除 |
| `IsValid()` | 購読が有効か判定 |

## ライセンス

Unlicense
