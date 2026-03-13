#include "include/objectSlot/ObjectSlot.h"
#include <iostream>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_set>
#include <unordered_map>
#include <chrono>
#include <memory>
#include <numeric>

// ======================================================
// テスト用の型定義
// ======================================================

/// ポリモーフィック参照テスト用のインターフェース
class IDrawable {
public:
    virtual ~IDrawable() = default;
    virtual void Draw() const = 0;
    virtual const std::string& GetName() const = 0;
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
    const std::string& GetName() const override { return name; }
};

/// IDrawableの具体型B
class Sprite : public IDrawable {
public:
    std::string name;
    Sprite() = default;
    Sprite(const std::string& n) : name(n) {}
    void Draw() const override { std::cout << "  スプライト描画: " << name << std::endl; }
    const std::string& GetName() const override { return name; }
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

/// ベンチマーク用の軽量構造体（文字列を持たない）
struct BenchData {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int id = 0;
};

/// ベンチマーク用のインターフェース
class IBenchObject {
public:
    virtual ~IBenchObject() = default;
    virtual float GetValue() const = 0;
};

/// IBenchObjectの具体型
class BenchObject : public IBenchObject {
public:
    float value = 0.0f;
    BenchObject() = default;
    BenchObject(float v) : value(v) {}
    float GetValue() const override { return value; }
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
// ベンチマーク用ヘルパー
// ======================================================

/// 計測結果を表すナノ秒単位の型
using Nanoseconds = long long;

/// 指定回数のループを計測して平均ナノ秒を返す
template<typename Func>
Nanoseconds MeasureAverage(int iterations, Func&& func) {
    auto start = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < iterations; ++i) {
        func(i);
    }
    auto end = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return elapsed / iterations;
}

/// ベンチマーク結果を表示（2つの方式を比較）
static void PrintBenchmark(const std::string& label,
    Nanoseconds slotNs, Nanoseconds sharedNs)
{
    double ratio = (sharedNs > 0) ? static_cast<double>(slotNs) / sharedNs : 0.0;
    std::cout << "  " << label << ":" << std::endl;
    std::cout << "    ObjectSlot : " << slotNs << " ns" << std::endl;
    std::cout << "    shared_ptr : " << sharedNs << " ns" << std::endl;
    std::cout << "    比率       : " << std::fixed;
    std::cout.precision(2);
    std::cout << ratio << "x" << std::endl;
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

    PrintTest("SlotPtr - 順序比較とstd::set");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "SetA" });
        auto ptrB = slot.Create(Mesh{ "SetB" });
        auto ptrC = slot.Create(Mesh{ "SetC" });

        std::set<SlotPtr<Mesh>> meshSet;
        meshSet.insert(ptrA);
        meshSet.insert(ptrB);
        meshSet.insert(ptrC);
        meshSet.insert(ptrA);

        std::cout << "  set内の要素数: " << meshSet.size() << std::endl;
        PrintResult(meshSet.size() == 3);
    }

    PrintTest("SlotPtr - std::unordered_set");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "HashA" });
        auto ptrB = slot.Create(Mesh{ "HashB" });

        std::unordered_set<SlotPtr<Mesh>> meshSet;
        meshSet.insert(ptrA);
        meshSet.insert(ptrB);
        meshSet.insert(ptrA);

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

        ptr.Reset();
        bool afterExpired = (weak == nullptr);

        PrintResult(isValid && boolConv && notNull && afterExpired);
    }

    PrintTest("WeakSlotPtr - UseCount と Swap");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptrA = slot.Create(Mesh{ "WeakA" });
        auto ptrB = slot.Create(Mesh{ "WeakB" });

        WeakSlotPtr<Mesh> weakA = ptrA.GetWeak();
        WeakSlotPtr<Mesh> weakB = ptrB.GetWeak();

        bool countOk = (weakA.UseCount() == 1);

        weakA.Swap(weakB);
        auto lockedA = weakA.Lock();
        auto lockedB = weakB.Lock();

        PrintResult(countOk && lockedA->name == "WeakB" && lockedB->name == "WeakA");
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

        b.Reset();
        std::cout << "  B削除後Count: " << slot.Count() << std::endl;
        PrintResult(slot.Count() == 2);
    }

    PrintTest("ObjectSlotSystem - Reserve, ShrinkToFit, MaxCapacity");
    {
        auto& slot = ObjectSlotSystem<Sprite>::GetInstance();
        slot.Clear();
        slot.SetMaxCapacity(2);

        auto a = slot.Create(Sprite{ "1st" });
        auto b = slot.Create(Sprite{ "2nd" });
        auto c = slot.Create(Sprite{ "3rd" });

        bool maxCapOk = (slot.Count() == 2 && !c.IsValid());
        slot.SetMaxCapacity(0);

        PrintResult(maxCapOk);
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
            });

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
        device.Reset();
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
        }
        device.Reset();
        PrintResult(notified);
    }

    PrintTest("SignalSlotPtr - Swap と std::map");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "GPU_A" });
        auto devB = deviceSlot.Create(Device{ "GPU_B" });

        devA.Swap(devB);
        bool swapOk = (devA->name == "GPU_B" && devB->name == "GPU_A");

        std::map<SignalSlotPtr<Device>, std::string> deviceMap;
        deviceMap[devA] = "値A";
        deviceMap[devB] = "値B";
        bool mapOk = (deviceMap.size() == 2);

        PrintResult(swapOk && mapOk);
    }

    PrintTest("Subscription - UpdateCallback");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        int version = 0;
        auto sub = device.Subscribe([&version]() { version = 1; });
        sub.UpdateCallback([&version]() { version = 2; });

        device.Reset();
        PrintResult(version == 2);
    }

    // ==================================================
    PrintCategory("WeakSignalSlotPtr");
    // ==================================================

    PrintTest("WeakSignalSlotPtr - 基本操作");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        WeakSignalSlotPtr<Device> weak(device);

        bool valid = weak.IsValid() && !weak.IsExpired();
        bool countOk = (weak.UseCount() == 1);

        PrintResult(valid && countOk);
    }

    PrintTest("WeakSignalSlotPtr - Lock と期限切れ");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        WeakSignalSlotPtr<Device> weak;

        {
            auto device = deviceSlot.Create(Device{ "Temp" });
            weak = device;
            auto locked = weak.Lock();
            bool lockOk = (locked.IsValid() && locked->name == "Temp");
            if (!lockOk) { PrintResult(false); }
        }

        PrintResult(weak.IsExpired() && !weak.Lock().IsValid());
    }

    PrintTest("WeakSignalSlotPtr - Subscribe（弱参照から購読）");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto device = deviceSlot.Create(Device{ "GPU" });

        WeakSignalSlotPtr<Device> weak(device);

        bool notified = false;
        auto sub = weak.Subscribe([&notified]() { notified = true; });

        device.Reset();
        PrintResult(notified);
    }

    PrintTest("WeakSignalSlotPtr - GetWeak, Swap, コンテナ");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto devA = deviceSlot.Create(Device{ "Alpha" });
        auto devB = deviceSlot.Create(Device{ "Beta" });

        auto weakA = devA.GetWeak();
        auto weakB = devB.GetWeak();

        weakA.Swap(weakB);
        bool swapOk = (weakA.Lock()->name == "Beta" && weakB.Lock()->name == "Alpha");

        std::set<WeakSignalSlotPtr<Device>> weakSet;
        weakSet.insert(WeakSignalSlotPtr<Device>(devA));
        weakSet.insert(WeakSignalSlotPtr<Device>(devB));
        weakSet.insert(WeakSignalSlotPtr<Device>(devA));
        bool setOk = (weakSet.size() == 2);

        PrintResult(swapOk && setOk);
    }

    // ==================================================
    PrintCategory("SlotRef ポリモーフィック参照");
    // ==================================================

    PrintTest("SlotRef - 変換と統一管理");
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

    PrintTest("SlotRef - コピー・ムーブ・参照カウント");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "CopyMove" });

        SlotRef<IDrawable> ref1 = mesh;
        SlotRef<IDrawable> ref2 = ref1;
        SlotRef<IDrawable> ref3 = std::move(ref1);

        bool moveOk = !ref1.IsValid();
        bool copyOk = ref2.IsValid();
        bool moveDestOk = ref3.IsValid();
        bool countOk = (mesh.UseCount() == 3);

        PrintResult(moveOk && copyOk && moveDestOk && countOk);
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
        ref.Reset();
        PrintResult(alive && !ref.IsValid());
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

    PrintTest("SlotRef - Swap");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& spriteSlot = RefSlotSystem<Sprite>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "SwapMesh" });
        auto sprite = spriteSlot.Create(Sprite{ "SwapSprite" });

        SlotRef<IDrawable> refA = mesh;
        SlotRef<IDrawable> refB = sprite;

        refA.Swap(refB);

        auto* spritePtr = dynamic_cast<Sprite*>(refA.Get());
        auto* meshPtr = dynamic_cast<Mesh*>(refB.Get());
        PrintResult(spritePtr != nullptr && meshPtr != nullptr);
    }

    // ==================================================
    PrintCategory("SlotRef エイリアシング");
    // ==================================================

    PrintTest("SlotRef - エイリアシングの作成と所有権共有");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "AliasMesh", 42 });

        SlotRef<std::string> nameRef(mesh, &mesh->name);

        bool valueOk = (*nameRef == "AliasMesh");
        bool countOk = (mesh.UseCount() == 2);

        PrintResult(valueOk && countOk);
    }

    PrintTest("SlotRef - エイリアシングのコピーとムーブ");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "CopyAlias", 99 });

        SlotRef<int> vertRef(mesh, &mesh->vertexCount);
        SlotRef<int> vertRef2 = vertRef;
        SlotRef<int> vertRef3 = std::move(vertRef);

        bool moveOk = !vertRef.IsValid();
        bool copyOk = (*vertRef2 == 99);
        bool moveDestOk = (*vertRef3 == 99);
        bool countOk = (mesh.UseCount() == 3);

        PrintResult(moveOk && copyOk && moveDestOk && countOk);
    }

    PrintTest("SlotRef - エイリアシングの生存維持");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        SlotRef<std::string> nameRef;

        {
            auto mesh = meshSlot.Create(Mesh{ "OwnerTest" });
            nameRef = SlotRef<std::string>(mesh, &mesh->name);
        }

        bool alive = nameRef.IsValid();
        if (alive) std::cout << "  スコープ外の値: " << *nameRef << std::endl;
        nameRef.Reset();
        PrintResult(alive && !nameRef.IsValid());
    }

    // ==================================================
    PrintCategory("SlotRef Subscribe（SubscriptionRef）");
    // ==================================================

    PrintTest("SlotRef - Subscribe 基本動作");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "SubMesh" });

        SlotRef<IDrawable> ref = mesh;

        bool notified = false;
        SubscriptionRef sub = ref.Subscribe([&notified]() {
            notified = true;
            std::cout << "  SlotRef購読コールバック実行" << std::endl;
            });

        std::cout << "  SubscriptionRef有効: " << sub.IsValid() << std::endl;

        ref.Reset();
        mesh.Reset();
        PrintResult(notified);
    }

    PrintTest("SlotRef - Subscribe 手動解除");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "UnsubMesh" });

        SlotRef<IDrawable> ref = mesh;

        bool notified = false;
        SubscriptionRef sub = ref.Subscribe([&notified]() { notified = true; });

        sub.Unsubscribe();

        ref.Reset();
        mesh.Reset();
        PrintResult(!notified);
    }

    PrintTest("SlotRef - Subscribe UpdateCallback");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "UpdateMesh" });

        SlotRef<IDrawable> ref = mesh;

        int version = 0;
        SubscriptionRef sub = ref.Subscribe([&version]() { version = 1; });
        sub.UpdateCallback([&version]() { version = 2; });

        ref.Reset();
        mesh.Reset();
        PrintResult(version == 2);
    }

    PrintTest("SlotRef - Subscribe SubscriptionRef破棄時に購読が自動解除");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "AutoUnsub" });

        bool notified = false;
        {
            SlotRef<IDrawable> ref = mesh;
            SubscriptionRef sub = ref.Subscribe([&notified]() { notified = true; });
            // subがスコープを抜けて購読解除
        }

        mesh.Reset();
        PrintResult(!notified);
    }

    PrintTest("SlotRef - Subscribe 複数SlotRefからの購読");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "MultiSub" });

        SlotRef<IDrawable> ref1 = mesh;
        SlotRef<IDrawable> ref2 = mesh;

        std::vector<int> order;
        SubscriptionRef sub1 = ref1.Subscribe([&order]() { order.push_back(1); });
        SubscriptionRef sub2 = ref2.Subscribe([&order]() { order.push_back(2); });

        ref1.Reset();
        ref2.Reset();
        mesh.Reset();

        bool correctOrder = (order.size() == 2 && order[0] == 2 && order[1] == 1);
        PrintResult(correctOrder);
    }

    PrintTest("SlotRef - Subscribe ObjectSlotSystemでは空のSubscriptionRefが返る");
    {
        auto& slot = ObjectSlotSystem<Mesh>::GetInstance();
        auto ptr = slot.Create(Mesh{ "NoSignal" });

        SlotRef<IDrawable> ref = ptr;
        SubscriptionRef sub = ref.Subscribe([]() {});

        std::cout << "  ObjectSlotSystemでの購読: IsValid = " << sub.IsValid() << std::endl;
        PrintResult(!sub.IsValid());
    }

    // ==================================================
    PrintCategory("EnableSlotFromThis");
    // ==================================================

    PrintTest("EnableSlotFromThis - ObjectSlotSystem版");
    {
        auto& slot = ObjectSlotSystem<SelfAwareObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareObject{ "SelfObj" });

        auto self = ptr->GetSelf();
        bool selfOk = (self.IsValid() && self->name == "SelfObj");
        bool sameHandle = (self.GetHandle() == ptr.GetHandle());

        auto weakSelf = ptr->GetWeakSelf();
        bool weakOk = weakSelf.IsValid();
        bool countOk = (ptr.UseCount() == 2);

        PrintResult(selfOk && sameHandle && weakOk && countOk);
    }

    PrintTest("EnableSlotFromThis - SignalSlotSystem版");
    {
        auto& slot = SignalSlotSystem<SelfAwareSignalObject>::GetInstance();
        auto ptr = slot.Create(SelfAwareSignalObject{ "SelfSignal" });

        auto self = ptr->GetSelf();
        bool selfOk = (self.IsValid() && self->name == "SelfSignal");

        auto weakSelf = ptr->GetWeakSelf();
        bool weakOk = weakSelf.IsValid();
        bool countOk = (ptr.UseCount() == 2);

        PrintResult(selfOk && weakOk && countOk);
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
            drawableRef.Reset();
            meshReleased = true;
            });

        device.Reset();
        PrintResult(meshReleased && !drawableRef.IsValid());
    }

    PrintTest("複合 - WeakSignalSlotPtr + Subscribe による自動解放");
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        meshSlot.Clear();

        auto device = deviceSlot.Create(Device{ "AutoGPU" });
        auto mesh = meshSlot.Create(Mesh{ "AutoMesh" });

        WeakSignalSlotPtr<Device> weakDevice(device);
        SlotRef<IDrawable> drawableRef = mesh;

        bool autoReleased = false;
        auto sub = weakDevice.Subscribe([&autoReleased, &drawableRef]() {
            drawableRef.Reset();
            autoReleased = true;
            });

        device.Reset();
        PrintResult(autoReleased && !drawableRef.IsValid());
    }

    PrintTest("複合 - エイリアシング + 通知の連携");
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "AliasNotify", 256 });
        auto device = deviceSlot.Create(Device{ "GPU" });

        SlotRef<std::string> nameRef(mesh, &mesh->name);

        auto sub = device.Subscribe([&nameRef]() {
            nameRef.Reset();
            });

        device.Reset();
        PrintResult(!nameRef.IsValid());
    }

    // ==================================================
    PrintCategory("shared_ptr との速度比較");
    // ==================================================

    constexpr int BENCH_CREATE_COUNT = 100000;
    constexpr int BENCH_COPY_COUNT = 1000000;
    constexpr int BENCH_ACCESS_COUNT = 1000000;
    constexpr int BENCH_POLY_COUNT = 100000;

    std::cout << "\n  計測条件:" << std::endl;
    std::cout << "    作成・破棄 : " << BENCH_CREATE_COUNT << " 回" << std::endl;
    std::cout << "    コピー     : " << BENCH_COPY_COUNT << " 回" << std::endl;
    std::cout << "    アクセス   : " << BENCH_ACCESS_COUNT << " 回" << std::endl;
    std::cout << "    ポリモーフ : " << BENCH_POLY_COUNT << " 回" << std::endl;

    // --- 作成と破棄 ---
    std::cout << std::endl;
    {
        auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
        pool.Clear();
        pool.Reserve(BENCH_CREATE_COUNT);

        Nanoseconds slotNs = MeasureAverage(BENCH_CREATE_COUNT, [&](int i) {
            auto ptr = pool.Create(BenchData{ 1.0f, 2.0f, 3.0f, i });
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_CREATE_COUNT, [&](int i) {
            auto ptr = std::make_shared<BenchData>(BenchData{ 1.0f, 2.0f, 3.0f, i });
            });

        PrintBenchmark("作成 + 破棄（SlotPtr vs shared_ptr）", slotNs, sharedNs);
    }

    // --- コピー（参照カウント増減）---
    {
        auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
        pool.Clear();
        auto original = pool.Create(BenchData{ 1.0f, 2.0f, 3.0f, 0 });

        auto sharedOriginal = std::make_shared<BenchData>(BenchData{ 1.0f, 2.0f, 3.0f, 0 });

        Nanoseconds slotNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            SlotPtr<BenchData> copy = original;
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            std::shared_ptr<BenchData> copy = sharedOriginal;
            });

        PrintBenchmark("コピー + 破棄（参照カウント増減）", slotNs, sharedNs);
    }

    // --- 要素アクセス（Get / operator->）---
    {
        auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
        pool.Clear();
        auto slotPtr = pool.Create(BenchData{ 1.0f, 2.0f, 3.0f, 0 });

        auto sharedPtr = std::make_shared<BenchData>(BenchData{ 1.0f, 2.0f, 3.0f, 0 });

        volatile float sink = 0.0f;

        Nanoseconds slotNs = MeasureAverage(BENCH_ACCESS_COUNT, [&](int) {
            sink = slotPtr->x + slotPtr->y + slotPtr->z;
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_ACCESS_COUNT, [&](int) {
            sink = sharedPtr->x + sharedPtr->y + sharedPtr->z;
            });

        PrintBenchmark("要素アクセス（operator->）", slotNs, sharedNs);
    }

    // --- SignalSlotPtr 作成と破棄 ---
    {
        auto& pool = SignalSlotSystem<BenchData>::GetInstance();
        pool.Clear();
        pool.Reserve(BENCH_CREATE_COUNT);

        Nanoseconds slotNs = MeasureAverage(BENCH_CREATE_COUNT, [&](int i) {
            auto ptr = pool.Create(BenchData{ 1.0f, 2.0f, 3.0f, i });
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_CREATE_COUNT, [&](int i) {
            auto ptr = std::make_shared<BenchData>(BenchData{ 1.0f, 2.0f, 3.0f, i });
            });

        PrintBenchmark("作成 + 破棄（SignalSlotPtr vs shared_ptr）", slotNs, sharedNs);
    }

    // --- ポリモーフィックアクセス（SlotRef vs shared_ptr<Base>）---
    {
        auto& pool = RefSlotSystem<BenchObject>::GetInstance();
        pool.Clear();
        pool.Reserve(BENCH_POLY_COUNT);

        std::vector<SlotRef<IBenchObject>> slotRefs;
        std::vector<SignalSlotPtr<BenchObject>> slotOwners;
        slotRefs.reserve(BENCH_POLY_COUNT);
        slotOwners.reserve(BENCH_POLY_COUNT);

        for (int i = 0; i < BENCH_POLY_COUNT; ++i) {
            auto obj = pool.Create(BenchObject{ static_cast<float>(i) });
            slotRefs.push_back(SlotRef<IBenchObject>(obj));
            slotOwners.push_back(std::move(obj));
        }

        std::vector<std::shared_ptr<IBenchObject>> sharedPtrs;
        sharedPtrs.reserve(BENCH_POLY_COUNT);
        for (int i = 0; i < BENCH_POLY_COUNT; ++i) {
            sharedPtrs.push_back(std::make_shared<BenchObject>(static_cast<float>(i)));
        }

        volatile float sink = 0.0f;

        auto slotStart = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < BENCH_POLY_COUNT; ++i) {
            sink = slotRefs[i]->GetValue();
        }
        auto slotEnd = std::chrono::high_resolution_clock::now();
        Nanoseconds slotNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            slotEnd - slotStart).count() / BENCH_POLY_COUNT;

        auto sharedStart = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < BENCH_POLY_COUNT; ++i) {
            sink = sharedPtrs[i]->GetValue();
        }
        auto sharedEnd = std::chrono::high_resolution_clock::now();
        Nanoseconds sharedNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            sharedEnd - sharedStart).count() / BENCH_POLY_COUNT;

        PrintBenchmark("ポリモーフィックアクセス（SlotRef vs shared_ptr<Base>）", slotNs, sharedNs);
    }

    // --- 弱参照 Lock（WeakSignalSlotPtr vs weak_ptr）---
    {
        auto& pool = SignalSlotSystem<BenchData>::GetInstance();
        pool.Clear();
        auto signalPtr = pool.Create(BenchData{ 1.0f, 2.0f, 3.0f, 0 });
        WeakSignalSlotPtr<BenchData> weakSignal(signalPtr);

        auto sharedPtr = std::make_shared<BenchData>(BenchData{ 1.0f, 2.0f, 3.0f, 0 });
        std::weak_ptr<BenchData> weakShared = sharedPtr;

        volatile float sink = 0.0f;

        Nanoseconds slotNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            if (auto locked = weakSignal.Lock()) {
                sink = locked->x;
            }
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            if (auto locked = weakShared.lock()) {
                sink = locked->x;
            }
            });

        PrintBenchmark("弱参照Lock + アクセス + 破棄", slotNs, sharedNs);
    }

    // --- SlotRef 作成と破棄 ---
    {
        auto& pool = RefSlotSystem<BenchObject>::GetInstance();
        pool.Clear();
        pool.Reserve(BENCH_CREATE_COUNT);

        auto owner = pool.Create(BenchObject{ 42.0f });
        auto sharedOwner = std::make_shared<BenchObject>(42.0f);

        Nanoseconds slotNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            SlotRef<IBenchObject> ref(owner);
            });

        Nanoseconds sharedNs = MeasureAverage(BENCH_COPY_COUNT, [&](int) {
            std::shared_ptr<IBenchObject> ref = sharedOwner;
            });

        PrintBenchmark("SlotRef vs shared_ptr<Base> 作成 + 破棄", slotNs, sharedNs);
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