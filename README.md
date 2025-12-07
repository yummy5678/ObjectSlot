# ObjectSlot

C++17向けのヘッダーオンリー メモリプールライブラリ。
オブジェクトをメモリ上に連続配置し、参照カウント方式で自動管理する。

## 特徴

- **メモリ連続配置** - キャッシュ効率の向上
- **参照カウント** - `shared_ptr`風の自動メモリ管理
- **弱参照** - `weak_ptr`風の参照
- **世代番号** - 削除済みハンドルの安全な検出
- **破棄コールバック** - リソース解放の自動化
- **ヘッダーオンリー** - 導入が簡単

## 必要要件

- C++17以上
- 標準ライブラリのみ使用（外部依存なし）

## 使用例

### 基本的な使い方
```cpp
#include "ObjectSlot.h"

struct Mesh {
    std::string name;
    void Draw() { /* 描画処理 */ }
};

int main() {
    auto& slot = MemorySlot<Mesh>::GetInstance();

    // 作成
    SlotPtr<Mesh> mesh = slot.Create(Mesh{ "Box" });
    mesh->Draw();

    // コピー (参照カウント増加)
    SlotPtr<Mesh> mesh2 = mesh;

    // nullptrで解放
    mesh = nullptr;
    mesh2 = nullptr;  // ここで自動削除
}
```

### 破棄コールバック
```cpp
auto mesh = slot.Create(Mesh{ "Box" });

mesh.SetOnDestroy([]() {
    std::cout << "破棄された" << std::endl;
});
```

### 弱参照
```cpp
SlotPtr<Mesh> mesh = slot.Create(Mesh{ "Box" });
WeakSlotPtr<Mesh> weak = mesh.GetWeak();

mesh = nullptr;

if (weak.IsExpired()) {
    std::cout << "既に破棄済み" << std::endl;
}

// 安全にアクセス
if (auto locked = weak.Lock()) {
    (*locked)->Draw();
}
```

### 容量管理
```cpp
auto& slot = MemorySlot<Mesh>::GetInstance();

// 事前確保
slot.Reserve(1000);

// 最大容量を設定
pool.SetMaxCapacity(100);

// 作成可能か確認
if (slot.CanCreate()) {
    auto mesh = slot.Create(Mesh{ "Box" });
}

// 未使用メモリを解放
slot.ShrinkToFit();
```

## API

### MemorySlot

| メソッド | 説明 |
|---------|------|
| `GetInstance()` | シングルトンインスタンスを取得 |
| `Create(T&&)` | 要素を作成してSlotPtrを返す |
| `Reserve(size_t)` | メモリを事前確保 |
| `SetMaxCapacity(size_t)` | 最大容量を設定 (0 = 無制限) |
| `GetMaxCapacity()` | 最大容量を取得 |
| `CanCreate()` | 作成可能か判定 |
| `Count()` | 有効な要素数 |
| `Capacity()` | 確保済みスロット数 |
| `ShrinkToFit()` | 末尾の未使用メモリを解放 |
| `Clear()` | 全要素を削除 |
| `ForEach(Func)` | 全要素に対して処理を実行 |

### SlotPtr

| メソッド | 説明 |
|---------|------|
| `Get()` | 要素へのポインタを取得 |
| `IsValid()` | 有効か判定 |
| `UseCount()` | 参照カウントを取得 |
| `GetWeak()` | 弱参照を生成 |
| `Reset()` | 参照を解放 |
| `SetOnDestroy(Func)` | 破棄コールバックを設定 |
| `GetHandle()` | SlotHandleを取得 |

### WeakSlotPtr

| メソッド | 説明 |
|---------|------|
| `Lock()` | SlotPtrに昇格 (optional) |
| `IsExpired()` | 無効になったか判定 |
| `Reset()` | 参照をリセット |

## ライセンス
MIT または MIT-0 のデュアルライセンス。お好きな方を選択できます。

- **MIT** - 著作権表示が必要
- **MIT-0** - 著作権表示不要
