#include "include/objectSlot/ObjectSlot.h"
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>

// ======================================================
// テスト用の型定義
// ======================================================

/// ポリモーフィック参照テスト用のインターフェース
class IDrawable {
public:
    virtual ~IDrawable() = default;
    virtual void Draw() const = 0;
};

/// IDrawableの具体型A
class Mesh : public IDrawable {
public:
    std::string name;
    int vertexCount = 0;
    Mesh() = default;
    Mesh(const std::string& n) : name(n), vertexCount(0) {}
    Mesh(const std::string& n, int v) : name(n), vertexCount(v) {}
    void Draw() const override { std::cout << "  メッシュ描画: " << name << std::endl; }
};

/// IDrawableの具体型B
class Sprite : public IDrawable {
public:
    std::string name;
    Sprite() = default;
    Sprite(const std::string& n) : name(n) {}
    void Draw() const override { std::cout << "  スプライト描画: " << name << std::endl; }
};

/// 通知購読テスト用：通知を送る側
struct Device {
    std::string name;
};

/// 通知購読テスト用：通知を受け取る側
struct Buffer {
    std::string name;
    Subscription<Device> deviceSubscription;
    void Release() { std::cout << "  " << name << " を解放しました" << std::endl; }
};

/// EnableSlotFromThisテスト用：ObjectSlotSystem版
class SelfAwareObject : public EnableSlotFromThis<SelfAwareObject> {
public:
    std::string name;
    SelfAwareObject() = default;
    SelfAwareObject(const std::string& n) : name(n) {}

    SlotPtr<SelfAwareObject> GetSelf() {
        return SlotPtrFromThis();
    }

    WeakSlotPtr<SelfAwareObject> GetWeakSelf() const {
        return WeakSlotPtrFromThis();
    }
};

/// EnableSlotFromThisテスト用：SignalSlotSystem版
class SelfAwareSignalObject : public EnableSlotFromThis<SelfAwareSignalObject> {
public:
    std::string name;
    SelfAwareSignalObject() = default;
    SelfAwareSignalObject(const std::string& n) : name(n) {}

    SignalSlotPtr<SelfAwareSignalObject> GetSelf() {
        return SignalSlotPtrFromThis();
    }

    WeakSignalSlotPtr<SelfAwareSignalObject> GetWeakSelf() const {
        return WeakSignalSlotPtrFromThis();
    }
};

// ======================================================
// テスト用ヘルパー
// ======================================================

/// テストカテゴリのヘッダーを表示
static void PrintCategory(const std::string& category) {
    std::cout << "\n  ----------------------------------------" << std::endl;
    std::cout << "  " << category << std::endl;
    std::cout << "  ----------------------------------------" << std::endl;
}

/// テスト項目のヘッダーを表示
static void PrintTest(const std::string& title) {
    static int testNumber = 0;
    ++testNumber;
    std::cout << "\n[テスト " << testNumber << "] " << title << std::endl;
}

/// 結果を表示し、成功数と失敗数を記録
static int g_passed = 0;
static int g_failed = 0;
static void PrintResult(bool success) {
    if (success) {
        ++g_passed;
        std::cout << "  結果: 成功" << std::endl;
    }
    else {
        ++g_failed;
        std::cout << "  結果: *** 失敗 ***" << std::endl;
    }
}

// ======================================================
// テスト本体
// ======================================================

int main()
{
    std::cout << "========================================" << std::endl;
    std::cout << " ObjectSlot ライブラリ 全機能テスト" << std::endl;
    std::cout << "========================================" << std::endl;

    // ==================================================
    PrintCategory("SlotPtr 基本操作");
    // ==================================================

    PrintTest("SlotPtr - 作成と要素アクセス");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "TestMesh" });

        bool valid = ptr.IsValid();
        bool nameMatch = (ptr->name == "TestMesh");
        std::cout << "  IsValid: " << valid << ", name: " << ptr->name << std::endl;
        PrintResult(valid && nameMatch);
    }

    PrintTest("SlotPtr - コピーと参照カウント");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr1 = slot.Create(Mesh{ "CopyTest" });

        std::cout << "  作成直後: UseCount = " << ptr1.UseCount() << std::endl;

        SlotPtr<Mesh> ptr2 = ptr1;
        std::cout << "  コピー後: UseCount = " << ptr1.UseCount() << std::endl;

        ptr1.Reset();
        std::cout << "  ptr1解放後: ptr2.UseCount = " << ptr2.UseCount() << std::endl;

        PrintResult(ptr2.IsValid() && ptr2.UseCount() == 1);
    }

    PrintTest("SlotPtr - ムーブ");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr1 = slot.Create(Mesh{ "MoveTest" });

        SlotPtr<Mesh> ptr2 = std::move(ptr1);
        std::cout << "  ムーブ後: ptr1.IsValid = " << ptr1.IsValid()
            << ", ptr2.IsValid = " << ptr2.IsValid() << std::endl;

        PrintResult(!ptr1.IsValid() && ptr2.IsValid() && ptr2.UseCount() == 1);
    }

    PrintTest("SlotPtr - nullptr代入と比較");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "NullTest" });

        bool beforeNull = (ptr != nullptr);
        ptr = nullptr;
        bool afterNull = (ptr == nullptr);

        std::cout << "  代入前: (ptr != nullptr) = " << beforeNull << std::endl;
        std::cout << "  代入後: (ptr == nullptr) = " << afterNull << std::endl;
        PrintResult(beforeNull && afterNull);
    }

    PrintTest("SlotPtr - 間接参照演算子");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "DerefTest" });

        Mesh& ref = *ptr;
        ref.name = "Changed";
        std::cout << "  変更後: " << ptr->name << std::endl;
        PrintResult(ptr->name == "Changed");
    }

    PrintTest("SlotPtr - Swap");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "Alpha" });
        auto ptrB = slot.Create(Mesh{ "Beta" });

        ptrA.Swap(ptrB);
        std::cout << "  Swap後: ptrA->name = " << ptrA->name
            << ", ptrB->name = " << ptrB->name << std::endl;
        PrintResult(ptrA->name == "Beta" && ptrB->name == "Alpha");
    }

    PrintTest("SlotPtr - 順序比較演算子");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "First" });
        auto ptrB = slot.Create(Mesh{ "Second" });

        // ハンドルのインデックス順で比較される
        bool lessWorks = (ptrA < ptrB) || (ptrB < ptrA);
        bool leqWorks = (ptrA <= ptrA);
        bool geqWorks = (ptrA >= ptrA);
        std::cout << "  ptrA < ptrB or ptrB < ptrA: " << lessWorks << std::endl;
        std::cout << "  ptrA <= ptrA: " << leqWorks << std::endl;
        PrintResult(lessWorks && leqWorks && geqWorks);
    }

    PrintTest("SlotPtr - std::setに格納");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "SetA" });
        auto ptrB = slot.Create(Mesh{ "SetB" });
        auto ptrC = slot.Create(Mesh{ "SetC" });

        std::set<SlotPtr<Mesh>> meshSet;
        meshSet.insert(ptrA);
        meshSet.insert(ptrB);
        meshSet.insert(ptrC);
        meshSet.insert(ptrA); // 重複

        std::cout << "  set内の要素数: " << meshSet.size() << std::endl;
        PrintResult(meshSet.size() == 3);
    }

    PrintTest("SlotPtr - std::unordered_setに格納");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "HashA" });
        auto ptrB = slot.Create(Mesh{ "HashB" });

        std::unordered_set<SlotPtr<Mesh>> meshSet;
        meshSet.insert(ptrA);
        meshSet.insert(ptrB);
        meshSet.insert(ptrA); // 重複

        std::cout << "  unordered_set内の要素数: " << meshSet.size() << std::endl;
        PrintResult(meshSet.size() == 2);
    }

    // ==================================================
    PrintCategory("WeakSlotPtr");
    // ==================================================

    PrintTest("WeakSlotPtr - 弱参照とLock");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "WeakTest" });

        WeakSlotPtr<Mesh> weak = ptr.GetWeak();
        std::cout << "  IsExpired (参照あり): " << weak.IsExpired() << std::endl;

        SlotPtr<Mesh> locked = weak.Lock();
        std::cout << "  Lock成功: " << locked.IsValid()
            << ", UseCount: " << ptr.UseCount() << std::endl;

        locked.Reset();
        ptr.Reset();
        std::cout << "  IsExpired (全解放後): " << weak.IsExpired() << std::endl;

        SlotPtr<Mesh> failedLock = weak.Lock();
        PrintResult(weak.IsExpired() && !failedLock.IsValid());
    }

    PrintTest("WeakSlotPtr - IsValid / operator bool / nullptr比較");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "WeakValid" });

        WeakSlotPtr<Mesh> weak = ptr.GetWeak();

        bool isValid = weak.IsValid();
        bool boolConv = static_cast<bool>(weak);
        bool notNull = (weak != nullptr);
        bool nullIsNull = (nullptr != weak);

        std::cout << "  IsValid: " << isValid << ", bool: " << boolConv
            << ", != nullptr: " << notNull << std::endl;

        ptr.Reset();
        bool afterExpired = (weak == nullptr);
        std::cout << "  解放後 == nullptr: " << afterExpired << std::endl;

        PrintResult(isValid && boolConv && notNull && nullIsNull && afterExpired);
    }

    PrintTest("WeakSlotPtr - UseCount");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr1 = slot.Create(Mesh{ "WeakCount" });
        SlotPtr<Mesh> ptr2 = ptr1;

        WeakSlotPtr<Mesh> weak = ptr1.GetWeak();
        std::cout << "  UseCount (2参照): " << weak.UseCount() << std::endl;

        ptr2.Reset();
        std::cout << "  UseCount (1参照): " << weak.UseCount() << std::endl;

        PrintResult(weak.UseCount() == 1);
    }

    PrintTest("WeakSlotPtr - Swap");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "WeakA" });
        auto ptrB = slot.Create(Mesh{ "WeakB" });

        WeakSlotPtr<Mesh> weakA = ptrA.GetWeak();
        WeakSlotPtr<Mesh> weakB = ptrB.GetWeak();

        weakA.Swap(weakB);

        auto lockedA = weakA.Lock();
        auto lockedB = weakB.Lock();
        std::cout << "  Swap後: weakA -> " << lockedA->name
            << ", weakB -> " << lockedB->name << std::endl;
        PrintResult(lockedA->name == "WeakB" && lockedB->name == "WeakA");
    }

    // ==================================================
    PrintCategory("ObjectSlotSystem プール操作");
    // ==================================================

    PrintTest("ObjectSlotSystem - Count, Capacity, ForEach");
    {
        auto& slot = ObjectSlotSystem<Sprite>::GetInstance();
        slot.Clear();

        auto a = slot.Create(Sprite{ "A" });
        auto b = slot.Create(Sprite{ "B" });
        auto c = slot.Create(Sprite{ "C" });

        std::cout << "  Count: " << slot.Count() << ", Capacity: " << slot.Capacity() << std::endl;

        std::cout << "  ForEach: ";
        slot.ForEach([](SlotHandle h, const Sprite& s) {
            std::cout << s.name << " ";
            });
        std::cout << std::endl;

        b.Reset();
        std::cout << "  B削除後Count: " << slot.Count() << std::endl;
        PrintResult(slot.Count() == 2);
    }

    PrintTest("ObjectSlotSystem - Reserve と ShrinkToFit");
    {
        auto& slot = ObjectSlotSystem<Sprite>::GetInstance();
        slot.Clear();

        slot.Reserve(100);
        std::cout << "  Reserve(100)後のCapacity: " << slot.Capacity() << std::endl;

        auto a = slot.Create(Sprite{ "Only" });
        std::cout << "  要素1つのCount: " << slot.Count() << std::endl;

        slot.ShrinkToFit();
        std::cout << "  ShrinkToFit後のCapacity: " << slot.Capacity() << std::endl;
        PrintResult(slot.Count() == 1);
    }

    PrintTest("ObjectSlotSystem - MaxCapacity");
    {
        auto& slot = ObjectSlotSystem<Sprite>::GetInstance();
        slot.Clear();
        slot.SetMaxCapacity(2);

        auto a = slot.Create(Sprite{ "1st" });
        auto b = slot.Create(Sprite{ "2nd" });
        auto c = slot.Create(Sprite{ "3rd" });

        std::cout << "  MaxCapacity: " << slot.GetMaxCapacity() << std::endl;
        std::cout << "  Count: " << slot.Count() << std::endl;
        std::cout << "  3つ目は有効: " << c.IsValid() << std::endl;
        PrintResult(slot.Count() == 2 && !c.IsValid());

        slot.SetMaxCapacity(0);
    }

    // ==================================================
    PrintCategory("SignalSlotPtr 購読通知");
    // ==================================================

    PrintTest("SignalSlotPtr - 基本的な購読と通知");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        bool notified = false;
        auto sub = device.Subscribe([&notified]() {
            notified = true;
            std::cout << "  購読コールバック実行" << std::endl;
            });

        std::cout << "  Subscription有効: " << sub.IsValid() << std::endl;
        device.Reset();
        PrintResult(notified);
    }

    PrintTest("SignalSlotPtr - 複数購読の逆順通知");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        std::vector<int> order;
        std::vector<Subscription<Device>> subs;

        for (int i = 1; i <= 3; ++i) {
            subs.push_back(device.Subscribe([&order, i]() {
                order.push_back(i);
                std::cout << "  購読者 " << i << " 通知受信" << std::endl;
                }));
        }

        device.Reset();
        bool correctOrder = (order.size() == 3 && order[0] == 3 && order[1] == 2 && order[2] == 1);
        PrintResult(correctOrder);
    }

    PrintTest("SignalSlotPtr - 購読の手動解除");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        bool notified = false;
        auto sub = device.Subscribe([&notified]() { notified = true; });

        sub.Unsubscribe();
        std::cout << "  Unsubscribe後: sub.IsValid = " << sub.IsValid() << std::endl;

        device.Reset();
        std::cout << "  通知されたか: " << notified << std::endl;
        PrintResult(!notified);
    }

    PrintTest("SignalSlotPtr - 購読者の先行破棄による自動解除");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto& bufferSlot = ObjectSlotSystem<Buffer>::GetInstance();

        auto device = deviceSlot.Create(Device{ "GPU" });
        bool notified = false;

        {
            auto buffer = bufferSlot.Create(Buffer{ "TempBuffer" });
            buffer->deviceSubscription = device.Subscribe([&notified]() {
                notified = true;
                });
        }
        // Bufferが破棄 → Subscription破棄 → 購読自動解除

        device.Reset();
        std::cout << "  購読者先行破棄後に通知されたか: " << notified << std::endl;
        PrintResult(!notified);
    }

    PrintTest("SignalSlotPtr - Subscriptionのムーブ");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        bool notified = false;
        Subscription<Device> sub2;
        {
            auto sub1 = device.Subscribe([&notified]() { notified = true; });
            sub2 = std::move(sub1);
            std::cout << "  ムーブ後: sub1.IsValid = " << sub1.IsValid()
                << ", sub2.IsValid = " << sub2.IsValid() << std::endl;
        }
        // sub1はスコープを抜けたが、所有権はsub2に移動済み

        device.Reset();
        PrintResult(notified);
    }

    PrintTest("SignalSlotPtr - Swap");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "GPU_A" });
        auto devB = deviceSlot.Create(Device{ "GPU_B" });

        devA.Swap(devB);
        std::cout << "  Swap後: devA->name = " << devA->name
            << ", devB->name = " << devB->name << std::endl;
        PrintResult(devA->name == "GPU_B" && devB->name == "GPU_A");
    }

    PrintTest("SignalSlotPtr - 順序比較演算子とstd::map");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "First" });
        auto devB = deviceSlot.Create(Device{ "Second" });

        std::map<SignalSlotPtr<Device>, std::string> deviceMap;
        deviceMap[devA] = "値A";
        deviceMap[devB] = "値B";

        std::cout << "  map内の要素数: " << deviceMap.size() << std::endl;
        PrintResult(deviceMap.size() == 2 && deviceMap[devA] == "値A");
    }

    // ==================================================
    PrintCategory("Subscription::UpdateCallback");
    // ==================================================

    PrintTest("Subscription - コールバックの差し替え");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        int callbackVersion = 0;
        auto sub = device.Subscribe([&callbackVersion]() {
            callbackVersion = 1;
            });

        // コールバックを差し替え
        sub.UpdateCallback([&callbackVersion]() {
            callbackVersion = 2;
            });

        device.Reset();
        std::cout << "  実行されたコールバック: バージョン " << callbackVersion << std::endl;
        PrintResult(callbackVersion == 2);
    }

    // ==================================================
    PrintCategory("WeakSignalSlotPtr");
    // ==================================================

    PrintTest("WeakSignalSlotPtr - SignalSlotPtrからの変換と基本操作");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        WeakSignalSlotPtr<Device> weak(device);

        bool isValid = weak.IsValid();
        bool notExpired = !weak.IsExpired();
        bool boolConv = static_cast<bool>(weak);
        bool notNull = (weak != nullptr);
        uint32_t useCount = weak.UseCount();

        std::cout << "  IsValid: " << isValid << ", UseCount: " << useCount << std::endl;
        PrintResult(isValid && notExpired && boolConv && notNull && useCount == 1);
    }

    PrintTest("WeakSignalSlotPtr - Lock");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        WeakSignalSlotPtr<Device> weak(device);
        {
            auto locked = weak.Lock();
            std::cout << "  Lock成功: " << locked.IsValid()
                << ", UseCount: " << locked.UseCount() << std::endl;
            PrintResult(locked.IsValid() && locked->name == "GPU" && locked.UseCount() == 2);
        }
        // lockedが破棄 → 参照カウント戻る
    }

    PrintTest("WeakSignalSlotPtr - 期限切れ後のLock");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        WeakSignalSlotPtr<Device> weak;

        {
            auto device = deviceSlot.Create(Device{ "Temp" });
            weak = device;
        }
        // deviceが破棄 → 参照カウント0 → 要素削除

        bool expired = weak.IsExpired();
        auto locked = weak.Lock();
        bool lockFailed = !locked.IsValid();

        std::cout << "  IsExpired: " << expired << ", Lock失敗: " << lockFailed << std::endl;
        PrintResult(expired && lockFailed);
    }

    PrintTest("WeakSignalSlotPtr - Subscribe（弱参照から購読）");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        WeakSignalSlotPtr<Device> weak(device);

        bool notified = false;
        auto sub = weak.Subscribe([&notified]() {
            notified = true;
            std::cout << "  弱参照からの購読コールバック実行" << std::endl;
            });

        std::cout << "  Subscription有効: " << sub.IsValid() << std::endl;
        std::cout << "  弱参照のUseCount: " << weak.UseCount() << std::endl;

        device.Reset();
        // 弱参照は参照カウントに影響しないため、deviceのReset()で参照カウント0 → 通知発火
        PrintResult(notified);
    }

    PrintTest("WeakSignalSlotPtr - GetWeak経由での生成");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        auto weak = device.GetWeak();

        std::cout << "  GetWeak後: IsValid = " << weak.IsValid()
            << ", UseCount = " << weak.UseCount() << std::endl;

        auto locked = weak.Lock();
        std::cout << "  Lock後: name = " << locked->name << std::endl;

        PrintResult(weak.IsValid() && locked->name == "GPU");
    }

    PrintTest("WeakSignalSlotPtr - Swap");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "Alpha" });
        auto devB = deviceSlot.Create(Device{ "Beta" });

        WeakSignalSlotPtr<Device> weakA(devA);
        WeakSignalSlotPtr<Device> weakB(devB);

        weakA.Swap(weakB);

        auto lockedA = weakA.Lock();
        auto lockedB = weakB.Lock();
        std::cout << "  Swap後: weakA -> " << lockedA->name
            << ", weakB -> " << lockedB->name << std::endl;
        PrintResult(lockedA->name == "Beta" && lockedB->name == "Alpha");
    }

    PrintTest("WeakSignalSlotPtr - 順序比較とstd::set");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "A" });
        auto devB = deviceSlot.Create(Device{ "B" });

        std::set<WeakSignalSlotPtr<Device>> weakSet;
        weakSet.insert(WeakSignalSlotPtr<Device>(devA));
        weakSet.insert(WeakSignalSlotPtr<Device>(devB));
        weakSet.insert(WeakSignalSlotPtr<Device>(devA)); // 重複

        std::cout << "  set内の要素数: " << weakSet.size() << std::endl;
        PrintResult(weakSet.size() == 2);
    }

    PrintTest("WeakSignalSlotPtr - std::unordered_set");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "HashA" });
        auto devB = deviceSlot.Create(Device{ "HashB" });

        std::unordered_set<WeakSignalSlotPtr<Device>> weakSet;
        weakSet.insert(WeakSignalSlotPtr<Device>(devA));
        weakSet.insert(WeakSignalSlotPtr<Device>(devB));
        weakSet.insert(WeakSignalSlotPtr<Device>(devA)); // 重複

        std::cout << "  unordered_set内の要素数: " << weakSet.size() << std::endl;
        PrintResult(weakSet.size() == 2);
    }

    // ==================================================
    PrintCategory("SlotRef ポリモーフィック参照");
    // ==================================================

    PrintTest("SlotRef - SignalSlotPtrからの変換");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "RefMesh" });

        SlotRef<IDrawable> ref = mesh;
        ref->Draw();

        std::cout << "  UseCount: " << mesh.UseCount() << std::endl;
        PrintResult(ref.IsValid() && mesh.UseCount() == 2);
    }

    PrintTest("SlotRef - 異なる具体型を基底型で統一管理");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& spriteSlot = RefSlotSystem<Sprite>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "Box" });
        auto sprite = spriteSlot.Create(Sprite{ "Player" });

        std::vector<SlotRef<IDrawable>> drawables;
        drawables.push_back(SlotRef<IDrawable>(mesh));
        drawables.push_back(SlotRef<IDrawable>(sprite));

        for (auto& d : drawables) {
            d->Draw();
        }

        PrintResult(drawables.size() == 2 && mesh.UseCount() == 2 && sprite.UseCount() == 2);
    }

    PrintTest("SlotRef - コピーと参照カウント");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "CopyRef" });

        SlotRef<IDrawable> ref1 = mesh;
        SlotRef<IDrawable> ref2 = ref1;
        SlotRef<IDrawable> ref3;
        ref3 = ref2;

        std::cout << "  UseCount: " << mesh.UseCount() << std::endl;
        PrintResult(mesh.UseCount() == 4);
    }

    PrintTest("SlotRef - ムーブ");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "MoveRef" });

        SlotRef<IDrawable> ref1 = mesh;
        SlotRef<IDrawable> ref2 = std::move(ref1);

        std::cout << "  ref1.IsValid: " << ref1.IsValid()
            << ", ref2.IsValid: " << ref2.IsValid() << std::endl;
        PrintResult(!ref1.IsValid() && ref2.IsValid() && mesh.UseCount() == 2);
    }

    PrintTest("SlotRef - SlotRefだけで生存維持");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        SlotRef<IDrawable> ref;

        {
            auto mesh = meshSlot.Create(Mesh{ "Survive" });
            ref = mesh;
        }

        bool alive = ref.IsValid();
        if (alive) ref->Draw();

        ref.Reset();
        bool released = !ref.IsValid();

        PrintResult(alive && released);
    }

    PrintTest("SlotRef - nullptr代入と比較");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "NullRef" });

        SlotRef<IDrawable> ref = mesh;
        bool beforeNull = (ref != nullptr);
        ref = nullptr;
        bool afterNull = (ref == nullptr);

        PrintResult(beforeNull && afterNull);
    }

    PrintTest("SlotRef - 再アロケーション後のポインタ更新");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        meshSlot.Clear();

        auto mesh0 = meshSlot.Create(Mesh{ "Original" });
        SlotRef<IDrawable> ref = mesh0;

        std::vector<SignalSlotPtr<Mesh>> meshes;
        for (int i = 0; i < 100; ++i) {
            meshes.push_back(meshSlot.Create(Mesh{ "Filler_" + std::to_string(i) }));
        }

        bool stillValid = ref.IsValid();
        if (stillValid) ref->Draw();

        PrintResult(stillValid);
    }

    PrintTest("SlotRef - 要素削除時のポインタ無効化");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "WillDelete" });

        SlotRef<IDrawable> ref = mesh;
        std::cout << "  削除前: ref.IsValid = " << ref.IsValid() << std::endl;

        mesh.Reset();
        std::cout << "  SignalSlotPtr解放後: ref.IsValid = " << ref.IsValid() << std::endl;

        ref.Reset();
        std::cout << "  ref解放後: ref.IsValid = " << ref.IsValid() << std::endl;
        PrintResult(!ref.IsValid());
    }

    PrintTest("SlotRef - Swap");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& spriteSlot = RefSlotSystem<Sprite>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "SwapMesh" });
        auto sprite = spriteSlot.Create(Sprite{ "SwapSprite" });

        SlotRef<IDrawable> refA = mesh;
        SlotRef<IDrawable> refB = sprite;

        refA.Swap(refB);

        std::cout << "  Swap後:" << std::endl;
        std::cout << "  refA: "; refA->Draw();
        std::cout << "  refB: "; refB->Draw();

        // refAはSpriteを、refBはMeshを指しているはず
        auto* spritePtr = dynamic_cast<Sprite*>(refA.Get());
        auto* meshPtr = dynamic_cast<Mesh*>(refB.Get());
        PrintResult(spritePtr != nullptr && meshPtr != nullptr);
    }

    // ==================================================
    PrintCategory("SlotRef エイリアシング");
    // ==================================================

    PrintTest("SlotRef - SignalSlotPtrからのエイリアシング");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "AliasMesh", 42 });

        // メンバ変数nameを直接参照するエイリアシングSlotRef
        SlotRef<std::string> nameRef(mesh, &mesh->name);

        std::cout << "  エイリアス経由の値: " << *nameRef << std::endl;
        std::cout << "  mesh.UseCount: " << mesh.UseCount() << std::endl;

        PrintResult(*nameRef == "AliasMesh" && mesh.UseCount() == 2);
    }

    PrintTest("SlotRef - エイリアシングのコピー");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "CopyAlias", 99 });

        SlotRef<int> vertRef(mesh, &mesh->vertexCount);
        SlotRef<int> vertRef2 = vertRef; // コピー

        std::cout << "  コピー元: " << *vertRef << ", コピー先: " << *vertRef2 << std::endl;
        std::cout << "  mesh.UseCount: " << mesh.UseCount() << std::endl;

        PrintResult(*vertRef == 99 && *vertRef2 == 99 && mesh.UseCount() == 3);
    }

    PrintTest("SlotRef - エイリアシングのムーブ");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "MoveAlias", 77 });

        SlotRef<int> vertRef(mesh, &mesh->vertexCount);
        SlotRef<int> vertRef2 = std::move(vertRef);

        std::cout << "  ムーブ元.IsValid: " << vertRef.IsValid()
            << ", ムーブ先: " << *vertRef2 << std::endl;
        std::cout << "  mesh.UseCount: " << mesh.UseCount() << std::endl;

        PrintResult(!vertRef.IsValid() && *vertRef2 == 77 && mesh.UseCount() == 2);
    }

    PrintTest("SlotRef - エイリアシングの所有権共有");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        SlotRef<std::string> nameRef;

        {
            auto mesh = meshSlot.Create(Mesh{ "OwnerTest", 10 });
            nameRef = SlotRef<std::string>(mesh, &mesh->name);
            std::cout << "  スコープ内: " << *nameRef << std::endl;
        }
        // meshは破棄されたが、nameRefが参照カウントを保持

        bool alive = nameRef.IsValid();
        std::cout << "  スコープ外: IsValid = " << alive << std::endl;

        if (alive) std::cout << "  値: " << *nameRef << std::endl;
        nameRef.Reset();

        PrintResult(alive);
    }

    PrintTest("SlotRef - エイリアシングの解放時にオーナーも解放");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "ReleaseTest" });

        SlotRef<std::string> nameRef(mesh, &mesh->name);
        std::cout << "  作成後 UseCount: " << mesh.UseCount() << std::endl;

        mesh.Reset();
        std::cout << "  mesh解放後: nameRef.IsValid = " << nameRef.IsValid() << std::endl;

        nameRef.Reset();
        std::cout << "  nameRef解放後: nameRef.IsValid = " << nameRef.IsValid() << std::endl;
        PrintResult(!nameRef.IsValid());
    }

    // ==================================================
    PrintCategory("EnableSlotFromThis");
    // ==================================================

    PrintTest("EnableSlotFromThis - ObjectSlotSystem版 SlotPtrFromThis");
    {
        auto& slot = ObjectSlotSystem<SelfAwareObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareObject{ "SelfObj" });

        auto self = ptr->GetSelf();
        std::cout << "  GetSelf成功: " << self.IsValid()
            << ", name: " << self->name
            << ", UseCount: " << self.UseCount() << std::endl;

        bool sameObject = (self.GetHandle() == ptr.GetHandle());
        std::cout << "  同じオブジェクト: " << sameObject << std::endl;

        PrintResult(self.IsValid() && self->name == "SelfObj" && sameObject);
    }

    PrintTest("EnableSlotFromThis - ObjectSlotSystem版 WeakSlotPtrFromThis");
    {
        auto& slot = ObjectSlotSystem<SelfAwareObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareObject{ "WeakSelfObj" });

        auto weakSelf = ptr->GetWeakSelf();
        std::cout << "  WeakSelf有効: " << weakSelf.IsValid()
            << ", UseCount: " << weakSelf.UseCount() << std::endl;

        // 弱参照なので参照カウントは増えない
        bool countUnchanged = (ptr.UseCount() == 1);
        std::cout << "  参照カウント不変: " << countUnchanged << std::endl;

        auto locked = weakSelf.Lock();
        std::cout << "  Lock後: " << locked->name << std::endl;

        PrintResult(weakSelf.IsValid() && countUnchanged && locked->name == "WeakSelfObj");
    }

    PrintTest("EnableSlotFromThis - SignalSlotSystem版 SignalSlotPtrFromThis");
    {
        auto& slot = SignalSlotSystem<SelfAwareSignalObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareSignalObject{ "SelfSignalObj" });

        auto self = ptr->GetSelf();
        std::cout << "  GetSelf成功: " << self.IsValid()
            << ", name: " << self->name
            << ", UseCount: " << self.UseCount() << std::endl;

        bool sameObject = (self.GetHandle() == ptr.GetHandle());
        PrintResult(self.IsValid() && self->name == "SelfSignalObj" && sameObject);
    }

    PrintTest("EnableSlotFromThis - SignalSlotSystem版 WeakSignalSlotPtrFromThis");
    {
        auto& slot = SignalSlotSystem<SelfAwareSignalObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareSignalObject{ "WeakSelfSignal" });

        auto weakSelf = ptr->GetWeakSelf();
        std::cout << "  WeakSelf有効: " << weakSelf.IsValid()
            << ", UseCount: " << weakSelf.UseCount() << std::endl;

        bool countUnchanged = (ptr.UseCount() == 1);
        std::cout << "  参照カウント不変: " << countUnchanged << std::endl;

        PrintResult(weakSelf.IsValid() && countUnchanged);
    }

    PrintTest("EnableSlotFromThis - コピー/ムーブでスロット情報が転送されない");
    {
        auto& slot = ObjectSlotSystem<SelfAwareObject>::GetInstance();
        auto ptr1 = slot.Create(SelfAwareObject{ "Original" });

        // ptrを使ってプール内のオブジェクトのnameを変更
        auto self1 = ptr1->GetSelf();
        std::cout << "  ptr1のGetSelf成功: " << self1.IsValid() << std::endl;

        // 新しいオブジェクトを作成（コピーではなく別のプール要素）
        auto ptr2 = slot.Create(SelfAwareObject{ "Another" });
        auto self2 = ptr2->GetSelf();
        std::cout << "  ptr2のGetSelf成功: " << self2.IsValid() << std::endl;

        // 別々のハンドルであること
        bool differentHandles = (self1.GetHandle() != self2.GetHandle());
        std::cout << "  異なるハンドル: " << differentHandles << std::endl;

        PrintResult(self1.IsValid() && self2.IsValid() && differentHandles);
    }

    // ==================================================
    PrintCategory("複合テスト");
    // ==================================================

    PrintTest("複合 - 通知 + SlotRef の連携");
    {
        auto& deviceSlot = RefSlotSystem<Device>::GetInstance();
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        meshSlot.Clear();

        auto device = deviceSlot.Create(Device{ "MainGPU" });
        auto mesh = meshSlot.Create(Mesh{ "GpuMesh" });

        SlotRef<IDrawable> drawableRef = mesh;

        bool meshReleased = false;
        auto sub = device.Subscribe([&meshReleased, &drawableRef]() {
            std::cout << "  デバイス解放通知: ";
            drawableRef->Draw();
            drawableRef.Reset();
            meshReleased = true;
            });

        std::cout << "  デバイス解放前: mesh.UseCount = " << mesh.UseCount() << std::endl;
        device.Reset();
        std::cout << "  デバイス解放後: drawableRef.IsValid = " << drawableRef.IsValid() << std::endl;

        PrintResult(meshReleased && !drawableRef.IsValid());
    }

    PrintTest("複合 - WeakSignalSlotPtr + Subscribe による自動解放");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        meshSlot.Clear();

        auto device = deviceSlot.Create(Device{ "AutoGPU" });
        auto mesh = meshSlot.Create(Mesh{ "AutoMesh" });

        // デバイスを弱参照で保持（参照カウントに影響しない）
        WeakSignalSlotPtr<Device> weakDevice(device);
        SlotRef<IDrawable> drawableRef = mesh;

        bool autoReleased = false;
        auto sub = weakDevice.Subscribe([&autoReleased, &drawableRef]() {
            std::cout << "  弱参照経由の解放通知: メッシュを自動解放" << std::endl;
            drawableRef.Reset();
            autoReleased = true;
            });

        std::cout << "  device.UseCount (弱参照は含まない): " << device.UseCount() << std::endl;

        device.Reset();
        // 弱参照は参照カウントに影響しないため、Resetで即座に参照カウント0 → 通知発火

        std::cout << "  自動解放された: " << autoReleased << std::endl;
        std::cout << "  drawableRef.IsValid: " << drawableRef.IsValid() << std::endl;

        PrintResult(autoReleased && !drawableRef.IsValid());
    }

    PrintTest("複合 - エイリアシング + 通知の連携");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "AliasNotify", 256 });
        auto device = deviceSlot.Create(Device{ "GPU" });

        // メンバ変数をエイリアシングで参照
        SlotRef<std::string> nameRef(mesh, &mesh->name);

        // デバイス解放時にエイリアシング参照も解放
        auto sub = device.Subscribe([&nameRef]() {
            std::cout << "  通知受信: nameRef = " << *nameRef << std::endl;
            nameRef.Reset();
            });

        device.Reset();

        std::cout << "  nameRef解放済み: " << !nameRef.IsValid() << std::endl;
        PrintResult(!nameRef.IsValid());
    }

    // ==================================================
    // 結果サマリー
    // ==================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << " 全テスト完了" << std::endl;
    std::cout << "  成功: " << g_passed << std::endl;
    std::cout << "  失敗: " << g_failed << std::endl;
    std::cout << "========================================" << std::endl;

    return g_failed > 0 ? 1 : 0;
}