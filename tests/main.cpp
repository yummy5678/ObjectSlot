/**
 * @file main.cpp
 * @brief ObjectSlot ライブラリの機能テストと性能計測
 *
 * 構成:
 *   1. テスト基盤       … 検証マクロと結果の集計
 *   2. テスト用の型
 *   3. 機能テスト       … カテゴリごとに関数で分割
 *   4. 計測基盤         … 繰り返し計測と中央値、キャッシュ追い出し
 *   5. 性能計測         … shared_ptr比較、歯抜け走査、ポインタ配列走査
 *   6. main             … 引数で実行内容を切り替え
 *
 * 引数:
 *   --no-bench   機能テストのみ実行
 *   --bench-only 性能計測のみ実行
 *   --quick      計測の規模を縮小する（動作確認用）
 *
 * 終了コード: 失敗した検証の数（全て成功なら0）
 */

// ------------------------------------------------------
// 実装の切り替え
// ------------------------------------------------------
// OBJECT_SLOT_USE_EXPERIMENTAL を定義すると experimental_detail/ の新実装、
// 定義しなければ detail/ の従来実装でテストとベンチマークを行う。
// プロジェクトのプリプロセッサ定義で切り替えることを想定しているが、
// ここで直接 #define しても構わない。
// このファイルの中ではマクロを直接見ず、実装ごとの違いは SlotTraits から取る。
// #define OBJECT_SLOT_USE_EXPERIMENTAL
#include "objectSlot/ObjectSlot.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <set>
#include <map>
#include <unordered_set>
#include <chrono>
#include <memory>
#include <numeric>
#include <random>
#include <algorithm>
#include <functional>
#include <cstring>
#include <sstream>
#include <cstdlib>
#include <new>


// ======================================================
// ヒープ確保量の追跡
// ======================================================
// shared_ptr側のメモリ使用量を実測するため、グローバルの operator new を
// 差し替えて要求バイト数を数える。プログラム全体の確保が対象になるので、
// 計測したい区間の前後で差分を取って使う。

/// 確保時に先頭へ置く見出しのサイズ（要求サイズを保存し、16バイト境界も保つ）
constexpr size_t HEAP_HEADER_BYTES = 16;

static size_t g_heapRequestedBytes = 0;   ///< 現在生存している確保の要求バイト数の合計
static size_t g_heapAllocationCount = 0;  ///< 現在生存している確保の回数

/// 要求バイト数を見出しに記録してから確保する
static void* TrackedAllocate(size_t bytes) {
    void* block = std::malloc(bytes + HEAP_HEADER_BYTES);
    if (block == nullptr) throw std::bad_alloc();
    *static_cast<size_t*>(block) = bytes;
    g_heapRequestedBytes += bytes;
    ++g_heapAllocationCount;
    return static_cast<char*>(block) + HEAP_HEADER_BYTES;
}

/// 見出しから要求バイト数を読み戻して解放する
static void TrackedFree(void* pointer) noexcept {
    if (pointer == nullptr) return;
    void* block = static_cast<char*>(pointer) - HEAP_HEADER_BYTES;
    g_heapRequestedBytes -= *static_cast<size_t*>(block);
    --g_heapAllocationCount;
    std::free(block);
}

void* operator new(size_t bytes) { return TrackedAllocate(bytes); }
void* operator new[](size_t bytes) { return TrackedAllocate(bytes); }
void operator delete(void* pointer) noexcept { TrackedFree(pointer); }
void operator delete[](void* pointer) noexcept { TrackedFree(pointer); }
void operator delete(void* pointer, size_t) noexcept { TrackedFree(pointer); }
void operator delete[](void* pointer, size_t) noexcept { TrackedFree(pointer); }

/// ヒープ確保量の差分を取るための記録点
struct HeapMark {
    size_t bytes = g_heapRequestedBytes;
    size_t count = g_heapAllocationCount;
    size_t BytesSince() const { return g_heapRequestedBytes - bytes; }
    size_t CountSince() const { return g_heapAllocationCount - count; }
};

// ======================================================
// 1. テスト基盤
// ======================================================

/// 検証結果を集計する
struct TestReport {
    int passedChecks = 0;                  ///< 成功した検証の数
    int failedChecks = 0;                  ///< 失敗した検証の数
    int passedTests = 0;                   ///< 全検証が成功したテストの数
    int failedTests = 0;                   ///< 1つ以上失敗したテストの数
    int failedInCurrentTest = 0;           ///< 現在のテスト内の失敗数
    std::vector<std::string> failureLog;   ///< 失敗した検証の記録
};

static TestReport g_report;

/// 検証を行い、失敗時は行番号付きで記録する
static void Check(bool condition, const char* description, int line) {
    if (condition) {
        ++g_report.passedChecks;
        return;
    }
    ++g_report.failedChecks;
    ++g_report.failedInCurrentTest;
    std::string message = std::string("    [NG] ") + description + "  (main.cpp:" + std::to_string(line) + ")";
    std::cout << message << std::endl;
    g_report.failureLog.push_back(message);
}

/// 条件が真であることを検証する
#define CHECK(condition, description) Check((condition), (description), __LINE__)

/// テスト関数を1つ実行し、結果を表示する
static void RunTest(const char* name, void (*testFunction)()) {
    static int testNumber = 0;
    ++testNumber;
    g_report.failedInCurrentTest = 0;

    std::cout << "[" << std::setw(2) << testNumber << "] " << name << std::endl;
    testFunction();

    if (g_report.failedInCurrentTest == 0) {
        ++g_report.passedTests;
    }
    else {
        ++g_report.failedTests;
        std::cout << "    *** 失敗 ***" << std::endl;
    }
}

/// カテゴリの見出しを表示する
static void PrintCategory(const char* category) {
    std::cout << "\n--- " << category << " ---" << std::endl;
}

// ======================================================
// 2. テスト用の型
// ======================================================

/// ポリモーフィック参照テスト用のインターフェース
class IDrawable {
public:
    virtual ~IDrawable() = default;
    virtual const std::string& GetName() const = 0;
};

/// IDrawableの具体型A
class Mesh : public IDrawable {
public:
    std::string name;
    int vertexCount = 0;
    Mesh() = default;
    explicit Mesh(const std::string& meshName) : name(meshName) {}
    Mesh(const std::string& meshName, int vertices) : name(meshName), vertexCount(vertices) {}
    const std::string& GetName() const override { return name; }
};

/// IDrawableの具体型B
class Sprite : public IDrawable {
public:
    std::string name;
    Sprite() = default;
    explicit Sprite(const std::string& spriteName) : name(spriteName) {}
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
};

/// EnableSlotFromThis テスト用：ObjectSlotSystem版
class SelfAwareObject : public EnableSlotFromThis<SelfAwareObject> {
public:
    std::string name;
    SelfAwareObject() = default;
    explicit SelfAwareObject(const std::string& objectName) : name(objectName) {}
    SlotPtr<SelfAwareObject> GetSelf() { return SlotPtrFromThis(); }
    WeakSlotPtr<SelfAwareObject> GetWeakSelf() const { return WeakSlotPtrFromThis(); }
};

/// EnableSlotFromThis テスト用：SignalSlotSystem版
class SelfAwareSignalObject : public EnableSlotFromThis<SelfAwareSignalObject> {
public:
    std::string name;
    SelfAwareSignalObject() = default;
    explicit SelfAwareSignalObject(const std::string& objectName) : name(objectName) {}
    SignalSlotPtr<SelfAwareSignalObject> GetSelf() { return SignalSlotPtrFromThis(); }
    WeakSignalSlotPtr<SelfAwareSignalObject> GetWeakSelf() const { return WeakSignalSlotPtrFromThis(); }
};

/**
 * @brief 生存数を数える型（デストラクタ呼び出しの検証用）
 *
 * 値付きで構築されたものだけを「本物」として数える。
 * デフォルト構築（プールが削除済みスロットに置く空のオブジェクト）は数えず、
 * ムーブでは本物であることが移動元から移動先へ移る。
 */
struct Tracked {
    int value = 0;
    bool isReal = false;      ///< 値付きで構築された本物かどうか
    static int aliveCount;    ///< 生存している本物の数

    Tracked() = default;
    explicit Tracked(int initialValue) : value(initialValue), isReal(true) { ++aliveCount; }
    Tracked(Tracked&& other) noexcept : value(other.value), isReal(other.isReal) { other.isReal = false; }
    Tracked& operator=(Tracked&& other) noexcept {
        if (this != &other) {
            if (isReal) --aliveCount;
            value = other.value; isReal = other.isReal; other.isReal = false;
        }
        return *this;
    }
    Tracked(const Tracked&) = delete;
    Tracked& operator=(const Tracked&) = delete;
    ~Tracked() { if (isReal) --aliveCount; }
};
int Tracked::aliveCount = 0;

/// 性能計測用の軽量構造体（16バイト）
struct BenchData {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    int id = 0;
};

/// 性能計測用のインターフェース
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
    explicit BenchObject(float initialValue) : value(initialValue) {}
    float GetValue() const override { return value; }
};

/// 歯抜け走査の計測用（1キャッシュライン分のサイズ）
struct SparseData {
    uint32_t value = 0;
    char padding[60] = {};
};

// ======================================================
// 3. 機能テスト
// ======================================================

// ------------------------------------------------------
// SlotPtr
// ------------------------------------------------------

static void Test_SlotPtr_CreateAndAccess() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto ptr = pool.Create(Mesh{ "TestMesh" });
    CHECK(ptr.IsValid(), "生成直後は有効");
    CHECK(ptr->name == "TestMesh", "アロー演算子で要素にアクセスできる");
    CHECK((*ptr).name == "TestMesh", "間接参照演算子で要素にアクセスできる");
    CHECK(ptr.Get() != nullptr && ptr.Get()->name == "TestMesh", "Get()が要素を返す");
}

static void Test_SlotPtr_CopyAndRefCount() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto ptr1 = pool.Create(Mesh{ "RefCount" });
    CHECK(ptr1.UseCount() == 1, "生成直後の参照カウントは1");
    {
        auto ptr2 = ptr1;
        CHECK(ptr1.UseCount() == 2 && ptr2.UseCount() == 2, "コピーで参照カウントが2になる");
        CHECK(ptr1 == ptr2, "コピー同士は等価");
    }
    CHECK(ptr1.UseCount() == 1, "コピーの破棄で参照カウントが1に戻る");
}

static void Test_SlotPtr_Move() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto ptr1 = pool.Create(Mesh{ "Move" });
    auto ptr2 = std::move(ptr1);
    CHECK(!ptr1.IsValid(), "ムーブ元は無効になる");
    CHECK(ptr2.IsValid() && ptr2->name == "Move", "ムーブ先が要素を引き継ぐ");
    CHECK(ptr2.UseCount() == 1, "ムーブでは参照カウントが増えない");
}

static void Test_SlotPtr_NullptrAndComparison() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto ptr = pool.Create(Mesh{ "Null" });
    CHECK(ptr != nullptr && !(ptr == nullptr), "有効なポインタはnullptrと等しくない");
    ptr = nullptr;
    CHECK(ptr == nullptr && !ptr, "nullptr代入で無効になる");
    CHECK(ptr.Get() == nullptr, "無効なポインタのGet()はnullptr");
    SlotPtr<Mesh> defaultConstructed;
    CHECK(!defaultConstructed && defaultConstructed.Get() == nullptr, "デフォルト構築は無効");
}

static void Test_SlotPtr_Swap() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto ptrA = pool.Create(Mesh{ "Alpha" });
    auto ptrB = pool.Create(Mesh{ "Beta" });
    ptrA.Swap(ptrB);
    CHECK(ptrA->name == "Beta" && ptrB->name == "Alpha", "Swapで中身が入れ替わる");
    swap(ptrA, ptrB);
    CHECK(ptrA->name == "Alpha" && ptrB->name == "Beta", "ADL経由のswapでも入れ替わる");
}

static void Test_SlotPtr_OrderingAndContainers() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto a = pool.Create(Mesh{ "A" });
    auto b = pool.Create(Mesh{ "B" });
    auto c = pool.Create(Mesh{ "C" });
    std::set<SlotPtr<Mesh>> ordered{ c, a, b, a };
    CHECK(ordered.size() == 3, "std::setで重複が除かれる");
    std::unordered_set<SlotPtr<Mesh>> hashed{ a, b, a, b };
    CHECK(hashed.size() == 2, "std::unordered_setで重複が除かれる");
    CHECK(hashed.count(a) == 1, "コピーが同じキーとして検索できる");
}

static void Test_SlotPtr_Size() {
    CHECK(sizeof(SlotPtr<Mesh>) == SlotTraits::STRONG_POINTER_BYTES, "SlotPtrのサイズが実装の公称値と一致");
    CHECK(sizeof(SlotPtr<Mesh>) <= 16, "SlotPtrは16バイト以下");
}

// ------------------------------------------------------
// WeakSlotPtr
// ------------------------------------------------------

static void Test_WeakSlotPtr_LockAndExpire() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto strong = pool.Create(Mesh{ "Weak" });
    auto weak = strong.GetWeak();
    CHECK(weak.IsValid() && !weak.IsExpired(), "強参照が生きている間は有効");
    {
        auto locked = weak.Lock();
        CHECK(locked && locked->name == "Weak", "Lock()で強参照に昇格できる");
        CHECK(strong.UseCount() == 2, "Lock()で参照カウントが増える");
    }
    CHECK(strong.UseCount() == 1, "昇格した参照の破棄でカウントが戻る");
    strong = nullptr;
    CHECK(weak.IsExpired() && !weak.Lock(), "全ての強参照が消えると期限切れ");
}

static void Test_WeakSlotPtr_GenerationGuard() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    pool.Clear();   // フリーリストを空にして、次の解放スロットが確実に再利用されるようにする
    auto first = pool.Create(Mesh{ "First" });
    auto weak = first.GetWeak();
    const uint32_t index = first.GetHandle().index;
    first = nullptr;

    // 同じスロットが再利用されても、古い弱参照は復活しない
    auto second = pool.Create(Mesh{ "Second" });
    CHECK(second.GetHandle().index == index, "解放されたスロットが再利用される");
    CHECK(weak.IsExpired(), "スロット再利用後も古い弱参照は無効のまま（世代番号で区別）");
}

static void Test_WeakSlotPtr_UseCountAndSwap() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto a = pool.Create(Mesh{ "A" });
    auto b = pool.Create(Mesh{ "B" });
    auto weakA = a.GetWeak();
    auto weakB = b.GetWeak();
    CHECK(weakA.UseCount() == 1, "弱参照からUseCount()が読める");
    weakA.Swap(weakB);
    CHECK(weakA.Lock()->name == "B" && weakB.Lock()->name == "A", "Swapで参照先が入れ替わる");
    CHECK(WeakSlotPtr<Mesh>().IsExpired(), "デフォルト構築は期限切れ");
}

static void Test_WeakSlotPtr_Size() {
    CHECK(sizeof(WeakSlotPtr<Mesh>) == SlotTraits::WEAK_POINTER_BYTES, "WeakSlotPtrのサイズが実装の公称値と一致");
    CHECK(sizeof(WeakSlotPtr<Mesh>) <= 16, "WeakSlotPtrは16バイト以下");
}

// ------------------------------------------------------
// ObjectSlotSystem
// ------------------------------------------------------

static void Test_Pool_CountAndForEach() {
    auto& pool = ObjectSlotSystem<Sprite>::GetInstance();
    pool.Clear();
    auto a = pool.Create(Sprite{ "A" });
    auto b = pool.Create(Sprite{ "B" });
    auto c = pool.Create(Sprite{ "C" });
    CHECK(pool.Count() == 3, "Count()が生存数を返す");

    int visited = 0;
    pool.ForEach([&](SlotHandle, Sprite&) { ++visited; });
    CHECK(visited == 3, "ForEachが全要素を訪問する");

    b = nullptr;
    visited = 0;
    pool.ForEach([&](SlotHandle, Sprite& sprite) { ++visited; CHECK(sprite.name != "B", "削除済みの要素は訪問されない"); });
    CHECK(visited == 2 && pool.Count() == 2, "削除後は残りだけ訪問する");
}

static void Test_Pool_ForEachDeleteDuringIteration() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    std::vector<SlotPtr<Tracked>> ptrs;
    for (int i = 0; i < 10; ++i) ptrs.push_back(pool.Create(Tracked(i)));

    int visited = 0;
    pool.ForEach([&](SlotHandle, Tracked& item) {
        ++visited;
        // 走査中に偶数を削除する（削除は走査完了まで遅延されるはず）
        if (item.value % 2 == 0) {
            for (auto& p : ptrs) if (p && p->value == item.value) p.Reset();
        }
    });
    CHECK(visited == 10, "走査中に削除しても開始時の全要素を訪問する");
    CHECK(pool.Count() == 5, "走査完了後に削除が反映される");
    CHECK(Tracked::aliveCount == 5, "削除された要素のデストラクタが呼ばれている");
    ptrs.clear();
    CHECK(pool.Count() == 0 && Tracked::aliveCount == 0, "全て解放されている");
}

static void Test_Pool_ForEachCreateDuringIteration() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    std::vector<SlotPtr<Tracked>> ptrs;
    for (int i = 0; i < 5; ++i) ptrs.push_back(pool.Create(Tracked(i)));

    int visited = 0;
    pool.ForEach([&](SlotHandle, Tracked&) {
        ++visited;
        // 走査中に生成した要素は今回の走査では訪問されないはず
        if (visited == 1) ptrs.push_back(pool.Create(Tracked(100)));
    });
    CHECK(visited == 5, "走査中に生成した要素は今回は訪問されない（生存一覧の走査範囲を固定するため）");
    CHECK(pool.Count() == 6, "生成自体は反映される");
    ptrs.clear();
}

static void Test_Pool_SortActiveIndexList() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    std::vector<SlotPtr<Tracked>> ptrs;
    for (int i = 0; i < 100; ++i) ptrs.push_back(pool.Create(Tracked(i)));
    CHECK(pool.IsActiveIndexListSorted(), "順番に生成した直後は昇順");

    // 途中を削除すると末尾との交換で順序が崩れる
    for (int i = 0; i < 100; i += 3) ptrs[i].Reset();
    CHECK(!pool.IsActiveIndexListSorted(), "途中の削除で昇順フラグが下りる");

    pool.SortActiveIndexList();
    const auto& list = pool.GetActiveIndexList();
    CHECK(pool.IsActiveIndexListSorted() && std::is_sorted(list.begin(), list.end()), "整列後は実際に昇順");
    CHECK(list.size() == pool.Count(), "整列後も一覧の要素数が生存数と一致");

    // 整列後も全要素を正しく訪問できる
    int visited = 0;
    pool.ForEach([&](SlotHandle, Tracked&) { ++visited; });
    CHECK(static_cast<size_t>(visited) == pool.Count(), "整列後のForEachが全要素を訪問する");
    ptrs.clear();
}

static void Test_Pool_ReserveShrinkMaxCapacity() {
    auto& pool = ObjectSlotSystem<Sprite>::GetInstance();
    pool.Clear();
    pool.Reserve(64);
    pool.SetMaxCapacity(2);
    auto a = pool.Create(Sprite{ "1st" });
    auto b = pool.Create(Sprite{ "2nd" });
    auto c = pool.Create(Sprite{ "3rd" });
    CHECK(pool.Count() == 2 && !c.IsValid(), "最大容量を超えると無効なポインタが返る");
    pool.SetMaxCapacity(0);

    b = nullptr;
    pool.ShrinkToFit();
    CHECK(pool.Capacity() == 1, "末尾の空きスロットがShrinkToFitで切り詰められる");
    auto d = pool.Create(Sprite{ "4th" });
    CHECK(d.IsValid() && pool.Count() == 2, "縮小後も生成できる");
}

static void Test_Pool_Clear() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    std::vector<SlotPtr<Tracked>> ptrs;
    for (int i = 0; i < 10; ++i) ptrs.push_back(pool.Create(Tracked(i)));
    ptrs.clear();
    pool.Clear();
    CHECK(pool.Count() == 0 && pool.Capacity() == 0, "Clear()で空になる");
    CHECK(Tracked::aliveCount == 0, "Clear()後に生存オブジェクトが残らない");
}

// ------------------------------------------------------
// SignalSlotPtr
// ------------------------------------------------------

static void Test_Signal_BasicNotify() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "GPU" });
    bool notified = false;
    auto sub = device.Subscribe([&] { notified = true; });
    CHECK(sub.IsValid(), "購読オブジェクトが有効");
    device = nullptr;
    CHECK(notified, "解放時にコールバックが呼ばれる");
}

static void Test_Signal_ReverseOrder() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "GPU" });
    std::vector<int> order;
    auto s1 = device.Subscribe([&] { order.push_back(1); });
    auto s2 = device.Subscribe([&] { order.push_back(2); });
    auto s3 = device.Subscribe([&] { order.push_back(3); });
    device = nullptr;
    CHECK(order.size() == 3 && order[0] == 3 && order[1] == 2 && order[2] == 1, "登録の逆順に通知される");
}

static void Test_Signal_ManualUnsubscribe() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "GPU" });
    bool notified = false;
    auto sub = device.Subscribe([&] { notified = true; });
    sub.Unsubscribe();
    CHECK(!sub.IsValid(), "解除後は購読オブジェクトが無効");
    device = nullptr;
    CHECK(!notified, "解除した購読は呼ばれない");
}

static void Test_Signal_SubscriptionMove() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "GPU" });
    bool notified = false;
    Subscription<Device> moved;
    {
        auto sub = device.Subscribe([&] { notified = true; });
        moved = std::move(sub);
        CHECK(!sub.IsValid() && moved.IsValid(), "ムーブで購読が移る");
    }
    device = nullptr;
    CHECK(notified, "ムーブ先が生きていれば通知される");
}

static void Test_Signal_SwapAndMap() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto a = pool.Create(Device{ "A" });
    auto b = pool.Create(Device{ "B" });
    a.Swap(b);
    CHECK(a->name == "B" && b->name == "A", "Swapで入れ替わる");
    std::map<SignalSlotPtr<Device>, int> byPtr{ { a, 1 }, { b, 2 } };
    CHECK(byPtr.size() == 2 && byPtr[a] == 1, "std::mapのキーに使える");
}

static void Test_Signal_UpdateCallback() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "GPU" });
    int result = 0;
    auto sub = device.Subscribe([&] { result = 1; });
    sub.UpdateCallback([&] { result = 2; });
    device = nullptr;
    CHECK(result == 2, "差し替えたコールバックが呼ばれる");
}

static void Test_Signal_DeferredRemovalDuringNotify() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto a = pool.Create(Device{ "A" });
    auto b = pool.Create(Device{ "B" });
    bool bNotified = false;
    auto subB = b.Subscribe([&] { bNotified = true; });
    // Aの解放通知の中でBを解放する（削除が遅延され、両方とも正しく解放されるはず）
    auto subA = a.Subscribe([&] { b.Reset(); });
    const size_t before = pool.Count();
    a.Reset();
    CHECK(bNotified && !b, "通知中の削除が遅延されても両方解放される");
    CHECK(pool.Count() == before - 2, "プールの生存数が2減る");
}

static void Test_Signal_Size() {
    CHECK(sizeof(SignalSlotPtr<Device>) == SlotTraits::STRONG_POINTER_BYTES, "SignalSlotPtrのサイズが実装の公称値と一致");
    CHECK(sizeof(WeakSignalSlotPtr<Device>) == SlotTraits::WEAK_POINTER_BYTES, "WeakSignalSlotPtrのサイズが実装の公称値と一致");
    CHECK(sizeof(Subscription<Device>) == SlotTraits::SUBSCRIPTION_BYTES, "Subscriptionのサイズが実装の公称値と一致");
    CHECK(sizeof(SubscriptionRef) == SlotTraits::SUBSCRIPTION_REF_BYTES, "SubscriptionRefのサイズが実装の公称値と一致");
    CHECK(sizeof(SignalSlotPtr<Device>) == sizeof(SlotPtr<Device>), "通知付きでも強参照のサイズは同じ");
}

// ------------------------------------------------------
// WeakSignalSlotPtr
// ------------------------------------------------------

static void Test_WeakSignal_LockAndExpire() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto strong = pool.Create(Device{ "GPU" });
    WeakSignalSlotPtr<Device> weak = strong;
    CHECK(weak.IsValid() && weak.GetHandle() == strong.GetHandle(), "強参照から弱参照を作れる");
    {
        auto locked = weak.Lock();
        CHECK(locked && locked->name == "GPU" && strong.UseCount() == 2, "Lock()で昇格できる");
    }
    strong = nullptr;
    CHECK(weak.IsExpired() && !weak.Lock(), "解放後は期限切れ");
    WeakSignalSlotPtr<Device> fromInvalid = strong;
    CHECK(fromInvalid.IsExpired(), "無効な強参照からの弱参照は期限切れ");
}

static void Test_WeakSignal_Subscribe() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto strong = pool.Create(Device{ "GPU" });
    WeakSignalSlotPtr<Device> weak = strong;
    bool notified = false;
    auto sub = weak.Subscribe([&] { notified = true; });
    CHECK(sub.IsValid(), "弱参照から購読できる");
    strong = nullptr;
    CHECK(notified, "弱参照経由の購読も通知される");
}

static void Test_WeakSignal_GetWeakSwapContainer() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto a = pool.Create(Device{ "A" });
    auto b = pool.Create(Device{ "B" });
    auto weakA = a.GetWeak();
    auto weakB = b.GetWeak();
    weakA.Swap(weakB);
    CHECK(weakA.Lock()->name == "B", "Swapで参照先が入れ替わる");
    std::set<WeakSignalSlotPtr<Device>> ordered{ weakA, weakB, weakA };
    CHECK(ordered.size() == 2, "std::setのキーに使える");
}

// ------------------------------------------------------
// SlotRef
// ------------------------------------------------------

static void Test_SlotRef_Conversion() {
    auto mesh = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "Box" });
    auto sprite = RefSlotSystem<Sprite>::GetInstance().Create(Sprite{ "Player" });
    std::vector<SlotRef<IDrawable>> drawables;
    drawables.push_back(SlotRef<IDrawable>(mesh));
    drawables.push_back(SlotRef<IDrawable>(sprite));
    CHECK(drawables[0]->GetName() == "Box" && drawables[1]->GetName() == "Player", "異なる具体型を基底型で扱える");
    CHECK(mesh.UseCount() == 2 && sprite.UseCount() == 2, "SlotRefが参照カウントを保持する");
}

static void Test_SlotRef_CopyMoveRefCount() {
    auto mesh = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "Box" });
    SlotRef<IDrawable> ref1 = mesh;
    {
        SlotRef<IDrawable> ref2 = ref1;
        CHECK(mesh.UseCount() == 3, "SlotRefのコピーで参照カウントが増える");
        SlotRef<IDrawable> ref3 = std::move(ref2);
        CHECK(mesh.UseCount() == 3 && !ref2.IsValid() && ref3.IsValid(), "ムーブでは増えず、ムーブ元が無効になる");
    }
    CHECK(mesh.UseCount() == 2, "スコープを抜けると戻る");
}

static void Test_SlotRef_KeepsAlive() {
    SlotRef<IDrawable> ref;
    {
        auto mesh = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "Survivor" });
        ref = mesh;
    }
    CHECK(ref.IsValid() && ref->GetName() == "Survivor", "元のポインタが消えてもSlotRefだけで生存する");
}

static void Test_SlotRef_ValidAfterManyCreates() {
    auto& pool = RefSlotSystem<Mesh>::GetInstance();
    auto first = pool.Create(Mesh{ "Original" });
    SlotRef<IDrawable> ref = first;
    std::vector<SignalSlotPtr<Mesh>> fillers;
    for (int i = 0; i < 100; ++i) fillers.push_back(pool.Create(Mesh{ "Filler" }));
    CHECK(ref.IsValid() && ref->GetName() == "Original", "大量生成後もSlotRefが有効");
}

static void Test_SlotRef_Swap() {
    auto a = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "A" });
    auto b = RefSlotSystem<Mesh>::GetInstance().Create(Mesh{ "B" });
    SlotRef<IDrawable> refA = a;
    SlotRef<IDrawable> refB = b;
    refA.Swap(refB);
    CHECK(refA->GetName() == "B" && refB->GetName() == "A", "Swapで入れ替わる");
}

static void Test_SlotRef_Aliasing() {
    auto& pool = RefSlotSystem<Mesh>::GetInstance();
    auto mesh = pool.Create(Mesh{ "AliasMesh", 42 });
    SlotRef<std::string> nameRef(mesh, &mesh->name);
    SlotRef<int> countRef(mesh, &mesh->vertexCount);
    CHECK(*nameRef == "AliasMesh" && *countRef == 42, "エイリアシングでメンバを直接参照できる");
    CHECK(mesh.UseCount() == 3, "エイリアシング参照も所有権を共有する");

    SlotRef<std::string> copied = nameRef;
    SlotRef<std::string> moved = std::move(copied);
    CHECK(mesh.UseCount() == 4 && !copied.IsValid() && *moved == "AliasMesh", "エイリアシングのコピーとムーブ");

    std::string valueAfter;
    {
        SlotRef<std::string> survivor(mesh, &mesh->name);
        mesh.Reset();
        valueAfter = *survivor;
    }
    CHECK(valueAfter == "AliasMesh", "所有者を手放してもエイリアシング参照が生存を維持する");
}

static void Test_SlotRef_Subscribe() {
    auto& pool = RefSlotSystem<Mesh>::GetInstance();
    {
        auto mesh = pool.Create(Mesh{ "Sub" });
        SlotRef<IDrawable> ref = mesh;
        bool notified = false;
        SubscriptionRef sub = ref.Subscribe([&] { notified = true; });
        CHECK(sub.IsValid(), "SlotRefから購読できる");
        ref.Reset();
        mesh.Reset();
        CHECK(notified, "解放時に通知される");
    }
    {
        auto mesh = pool.Create(Mesh{ "Unsub" });
        SlotRef<IDrawable> ref = mesh;
        bool notified = false;
        SubscriptionRef sub = ref.Subscribe([&] { notified = true; });
        sub.Unsubscribe();
        ref.Reset(); mesh.Reset();
        CHECK(!notified, "手動解除した購読は呼ばれない");
    }
    {
        auto mesh = pool.Create(Mesh{ "Update" });
        SlotRef<IDrawable> ref = mesh;
        int result = 0;
        SubscriptionRef sub = ref.Subscribe([&] { result = 1; });
        sub.UpdateCallback([&] { result = 2; });
        ref.Reset(); mesh.Reset();
        CHECK(result == 2, "UpdateCallbackで差し替えられる");
    }
    {
        auto mesh = pool.Create(Mesh{ "Auto" });
        bool notified = false;
        {
            SlotRef<IDrawable> ref = mesh;
            SubscriptionRef sub = ref.Subscribe([&] { notified = true; });
        }
        mesh.Reset();
        CHECK(!notified, "SubscriptionRefの破棄で購読が自動解除される");
    }
    {
        auto mesh = pool.Create(Mesh{ "Multi" });
        SlotRef<IDrawable> ref1 = mesh;
        SlotRef<IDrawable> ref2 = mesh;
        int count = 0;
        SubscriptionRef s1 = ref1.Subscribe([&] { ++count; });
        SubscriptionRef s2 = ref2.Subscribe([&] { ++count; });
        ref1.Reset(); ref2.Reset(); mesh.Reset();
        CHECK(count == 2, "複数のSlotRefからの購読が全て通知される");
    }
    {
        auto plain = ObjectSlotSystem<Mesh>::GetInstance().Create(Mesh{ "Plain" });
        SlotRef<IDrawable> ref = plain;
        SubscriptionRef sub = ref.Subscribe([] {});
        CHECK(!sub.IsValid(), "通知機能のないプールでは空のSubscriptionRefが返る");
    }
}

// ------------------------------------------------------
// EnableSlotFromThis
// ------------------------------------------------------

static void Test_FromThis_ObjectSlot() {
    auto& pool = ObjectSlotSystem<SelfAwareObject>::GetInstance();
    auto obj = pool.Create(SelfAwareObject{ "Self" });
    auto self = obj->GetSelf();
    CHECK(self == obj && self->name == "Self" && obj.UseCount() == 2, "自分自身へのSlotPtrを取得できる");
    auto weakSelf = obj->GetWeakSelf();
    CHECK(weakSelf.IsValid() && weakSelf.Lock() == obj, "自分自身へのWeakSlotPtrを取得できる");
}

static void Test_FromThis_SignalSlot() {
    auto& pool = SignalSlotSystem<SelfAwareSignalObject>::GetInstance();
    auto obj = pool.Create(SelfAwareSignalObject{ "Self" });
    auto self = obj->GetSelf();
    CHECK(self == obj && obj.UseCount() == 2, "自分自身へのSignalSlotPtrを取得できる");
    auto weakSelf = obj->GetWeakSelf();
    CHECK(weakSelf.IsValid() && weakSelf.Lock() == obj, "自分自身へのWeakSignalSlotPtrを取得できる");
    bool notified = false;
    auto sub = self.Subscribe([&] { notified = true; });
    self.Reset(); obj.Reset();
    CHECK(notified, "自分自身への参照からも購読できる");
}

// ------------------------------------------------------
// 古いポインタの安全性（新実装のみ）
// ------------------------------------------------------
// 従来実装では Clear() 後に古いポインタを触ると未定義動作になるため、
// このカテゴリの前半2件は SlotTraits::SUPPORTS_STALE_POINTER_SAFETY が真の実装でのみ実行する。


static void Test_Stale_AfterClear() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    auto a = pool.Create(Tracked(1));
    auto b = pool.Create(Tracked(2));
    WeakSlotPtr<Tracked> weak = a.GetWeak();
    pool.Clear();
    CHECK(weak.IsExpired() && pool.Count() == 0 && Tracked::aliveCount == 0, "Clear()で全要素が破棄され弱参照は期限切れ");
    SlotPtr<Tracked> copy = a;   // 古いポインタのコピー・破棄は何も起こさない
    a.Reset(); b.Reset(); copy.Reset();
    auto c = pool.Create(Tracked(3));
    CHECK(c.UseCount() == 1 && c->value == 3 && weak.IsExpired(), "再利用したスロットは正常で、古い弱参照は無効のまま");
}

static void Test_Stale_AfterShrinkToFit() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    auto a = pool.Create(Tracked(1));
    auto b = pool.Create(Tracked(2));
    WeakSlotPtr<Tracked> weakB = b.GetWeak();
    b.Reset();
    pool.ShrinkToFit();
    CHECK(pool.Capacity() == 1 && weakB.IsExpired() && !weakB.Lock(), "切り詰めた範囲の弱参照は安全に無効と判定される");
    auto reused = pool.Create(Tracked(4));
    CHECK(reused->value == 4 && weakB.IsExpired(), "再確保後も古い弱参照は無効");
}

static void Test_Signal_RevivedDuringNotify() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "Phoenix" });
    WeakSignalSlotPtr<Device> weak = device;
    SignalSlotPtr<Device> revived;
    int notifyCount = 0;
    auto sub = device.Subscribe([&] { ++notifyCount; revived = weak.Lock(); });
    device.Reset();
    CHECK(revived.IsValid() && revived->name == "Phoenix", "通知の中で弱参照から復活した要素は削除されない");
    CHECK(pool.Count() >= 1 && notifyCount == 1, "復活した要素は生存数に残り、通知は1回だけ");
    revived.Reset();
    CHECK(notifyCount == 1 && weak.IsExpired(), "再度手放した時は通知されず（購読は消費済み）、削除される");
}

static void Test_SlotRef_FromObjectSlotSystem() {
    auto& pool = ObjectSlotSystem<Mesh>::GetInstance();
    auto mesh = pool.Create(Mesh{ "Plain" });
    SlotRef<IDrawable> ref = mesh;
    CHECK(mesh.UseCount() == 2 && ref->GetName() == "Plain", "通知なしプールの要素からもSlotRefを作れる");
    SubscriptionRef sub = ref.Subscribe([] {});
    CHECK(!sub.IsValid(), "通知なしプールへの購読は無効な握りを返す");
    mesh.Reset();
    CHECK(ref.IsValid() && ref->GetName() == "Plain", "SlotRefだけで生存を維持する");
}

static void Test_Compact_PointersStayValid() {
    auto& pool = ObjectSlotSystem<Tracked>::GetInstance();
    pool.Clear();
    std::vector<SlotPtr<Tracked>> ptrs;
    for (int i = 0; i < 100; ++i) ptrs.push_back(pool.Create(Tracked(i)));
    for (int i = 0; i < 100; i += 2) ptrs[i].Reset();          // 偶数番を削除して歯抜けにする
    WeakSlotPtr<Tracked> weak = ptrs[51].GetWeak();
    WeakSlotPtr<Tracked> deadWeak = ptrs[1].GetWeak();
    ptrs[1].Reset();

    pool.Compact();

    bool allValid = true;
    for (int i = 3; i < 100; i += 2) {
        if (!ptrs[i] || ptrs[i]->value != i) allValid = false;
    }
    CHECK(allValid, "Compact() 後も全ての SlotPtr が同じ要素を指す");
    CHECK(weak.Lock() && weak.Lock()->value == 51, "Compact() 後も弱参照が有効");
    CHECK(deadWeak.IsExpired(), "Compact() 前に削除した要素の弱参照は無効のまま");
    CHECK(pool.Count() == 49 && Tracked::aliveCount == 49, "生存数は変わらない");

    // 本体が先頭から隙間なく並んでいる（アドレスが sizeof(T) 間隔で単調増加）
    std::vector<uintptr_t> addresses;
    pool.ForEach([&](SlotHandle, Tracked& t) { addresses.push_back(reinterpret_cast<uintptr_t>(&t)); });
    bool contiguous = addresses.size() == 49;
    for (size_t i = 1; i < addresses.size() && contiguous; ++i) {
        if (addresses[i] - addresses[i - 1] != sizeof(Tracked)) contiguous = false;
    }
    CHECK(contiguous, "Compact() 後は ForEach の走査順に本体が隙間なく並ぶ");

    auto fresh = pool.Create(Tracked(200));
    CHECK(fresh->value == 200 && pool.Count() == 50, "Compact() 後も生成できる");
    ptrs.clear(); fresh.Reset();
    CHECK(pool.Count() == 0 && Tracked::aliveCount == 0, "全て解放できる");
}

static void Test_Compact_SlotRefAndSubscription() {
    auto& pool = RefSlotSystem<Mesh>::GetInstance();
    std::vector<SignalSlotPtr<Mesh>> fillers;
    for (int i = 0; i < 20; ++i) fillers.push_back(pool.Create(Mesh{ "Filler" }));
    auto mesh = pool.Create(Mesh{ "Survivor", 7 });
    SlotRef<IDrawable> drawable = mesh;
    SlotRef<int> countRef(mesh, &mesh->vertexCount);
    bool notified = false;
    auto sub = mesh.Subscribe([&] { notified = true; });
    fillers.clear();                                    // 手前の20個を削除して歯抜けにする

    pool.Compact();

    CHECK(drawable->GetName() == "Survivor" && *countRef == 7, "Compact() 後も SlotRef とエイリアシング参照が有効");
    CHECK(&*mesh == static_cast<Mesh*>(drawable.Get()), "SlotRef が動いた後の本体を指している");
    CHECK(mesh.UseCount() == 3 && !notified, "Compact() で参照カウントは変わらず、通知もされない");
    drawable.Reset(); countRef.Reset(); mesh.Reset();
    CHECK(notified, "Compact() 後も解放通知が届く");
}

static void Test_Subscription_ToRef() {
    auto& pool = SignalSlotSystem<Device>::GetInstance();
    auto device = pool.Create(Device{ "ToRef" });
    bool fired = false;
    SubscriptionRef ref = device.Subscribe([&] { fired = true; }).ToRef();
    CHECK(ref.IsValid(), "ToRef()で型消去した握りに変換できる");
    device.Reset();
    CHECK(fired, "変換後も解放時に通知される");
}

// ------------------------------------------------------
// 圧縮ポインタの配置（SlotTraits::IS_COMPACT が真の実装のみ）
// ------------------------------------------------------
// 圧縮値やタグは新実装にしかない API なので、テンプレートにして
// 依存名にし、if constexpr の偽側が実体化されないようにしている。

template<typename ObjectPool, typename SignalPool>
static void CheckCompactLayout(ObjectPool& objectPool, SignalPool& signalPool) {
    if constexpr (SlotTraits::IS_COMPACT) {
        auto a = objectPool.Create(Mesh{ "A" });
        auto b = signalPool.Create(Mesh{ "B" });
        CHECK(objectPool.GetTag() != signalPool.GetTag(), "プールごとにタグが異なる");
        CHECK(a.GetTag() == objectPool.GetTag() && b.GetTag() == signalPool.GetTag(), "ポインタの上位4ビットが所属プールのタグ");

        using StorageType = typename ObjectPool::Storage;

        // スロット（見出し・位置表）と本体は別の並びにある
        {
            objectPool.Reserve(2);
            const auto& storage = objectPool.GetStorage();
            const uintptr_t header0 = reinterpret_cast<uintptr_t>(storage.Header(0));
            const uintptr_t header1 = reinterpret_cast<uintptr_t>(storage.Header(1));
            CHECK(header1 - header0 == StorageType::HEADER_BYTES, "見出しは16バイト間隔で並ぶ");
            CHECK(StorageType::IndexFromValue(a.GetValue()) == a.GetHandle().index, "圧縮値の下位ビットはスロット番号");
            CHECK(StorageType::HeaderAt(a.GetValue())->refCount == 1 && StorageType::ElementAt(a.GetValue()) == &*a, "圧縮値から本体と見出しの両方に辿れる");
            CHECK(*StorageType::EntryAt(a.GetValue()) != 0xFFFFFFFFu, "位置表の項目に本体の位置が入っている");
            CHECK(reinterpret_cast<uintptr_t>(StorageType::EntryAt(storage.ValueOf(0))) == reinterpret_cast<uintptr_t>(storage.RegionBase()), "位置表は区画の先頭にある（型に依存しない位置）");
        }

        // 共有領域の配置
        {
            const uintptr_t arena = StorageType::ArenaBase();
            const uintptr_t baseA = reinterpret_cast<uintptr_t>(objectPool.GetStorage().RegionBase());
            const uintptr_t baseB = reinterpret_cast<uintptr_t>(signalPool.GetStorage().RegionBase());
            CHECK((arena & (StorageType::ARENA_ALIGNMENT - 1)) == 0, "共有領域の基底が4GB境界に揃っている");
            CHECK(baseA == arena + objectPool.GetTag() * SlotTraits::REGION_BYTES, "プールは共有領域の自分の区画にある");
            CHECK(baseB == arena + signalPool.GetTag() * SlotTraits::REGION_BYTES, "別のプールも同じ共有領域の自分の区画にある");
            CHECK(reinterpret_cast<uintptr_t>(&*a) == arena + *StorageType::EntryAt(a.GetValue()), "本体のアドレス = 共有領域の基底 + 位置表の値");
        }
    }
    else {
        (void)objectPool;
        (void)signalPool;
        CHECK(true, "従来実装のため圧縮値の配置検証は対象外");
    }
}

static void Test_Compression_ArenaLayout() {
    CheckCompactLayout(ObjectSlotSystem<Mesh>::GetInstance(), SignalSlotSystem<Mesh>::GetInstance());
}

// ------------------------------------------------------
// 複合
// ------------------------------------------------------

static void Test_Combined_NotifyAndSlotRef() {
    auto& devicePool = RefSlotSystem<Device>::GetInstance();
    auto& meshPool = RefSlotSystem<Mesh>::GetInstance();
    auto device = devicePool.Create(Device{ "MainGPU" });
    auto mesh = meshPool.Create(Mesh{ "GpuMesh" });
    SlotRef<IDrawable> drawableRef = mesh;
    bool released = false;
    auto sub = device.Subscribe([&] { drawableRef.Reset(); released = true; });
    device.Reset();
    CHECK(released && !drawableRef.IsValid(), "解放通知の中でSlotRefを手放せる");
}

static void Test_Combined_WeakSubscribeAutoRelease() {
    auto& devicePool = SignalSlotSystem<Device>::GetInstance();
    auto& meshPool = RefSlotSystem<Mesh>::GetInstance();
    auto device = devicePool.Create(Device{ "AutoGPU" });
    auto mesh = meshPool.Create(Mesh{ "AutoMesh" });
    WeakSignalSlotPtr<Device> weakDevice(device);
    SlotRef<IDrawable> drawableRef = mesh;
    bool released = false;
    auto sub = weakDevice.Subscribe([&] { drawableRef.Reset(); released = true; });
    device.Reset();
    CHECK(released && !drawableRef.IsValid(), "弱参照経由の購読で従属リソースを自動解放できる");
}

static void Test_Combined_AliasingAndNotify() {
    auto& meshPool = RefSlotSystem<Mesh>::GetInstance();
    auto& devicePool = SignalSlotSystem<Device>::GetInstance();
    auto mesh = meshPool.Create(Mesh{ "AliasNotify", 256 });
    auto device = devicePool.Create(Device{ "GPU" });
    SlotRef<std::string> nameRef(mesh, &mesh->name);
    auto sub = device.Subscribe([&] { nameRef.Reset(); });
    CHECK(mesh.UseCount() == 2, "エイリアシング参照が所有権を持つ");
    device.Reset();
    CHECK(!nameRef.IsValid() && mesh.UseCount() == 1, "通知でエイリアシング参照が解放され、参照カウントが戻る");
}

// ======================================================
// 機能テストの実行
// ======================================================

static void RunAllTests() {
    std::cout << "========================================" << std::endl;
    std::cout << " ObjectSlot 機能テスト" << std::endl;
    std::cout << " （実装: " << SlotTraits::IMPLEMENTATION_NAME << "）" << std::endl;
    std::cout << "========================================" << std::endl;

    PrintCategory("SlotPtr");
    RunTest("SlotPtr - 作成と要素アクセス", Test_SlotPtr_CreateAndAccess);
    RunTest("SlotPtr - コピーと参照カウント", Test_SlotPtr_CopyAndRefCount);
    RunTest("SlotPtr - ムーブ", Test_SlotPtr_Move);
    RunTest("SlotPtr - nullptrと比較", Test_SlotPtr_NullptrAndComparison);
    RunTest("SlotPtr - Swap", Test_SlotPtr_Swap);
    RunTest("SlotPtr - 順序比較とコンテナ", Test_SlotPtr_OrderingAndContainers);
    RunTest("SlotPtr - サイズ", Test_SlotPtr_Size);

    PrintCategory("WeakSlotPtr");
    RunTest("WeakSlotPtr - Lockと期限切れ", Test_WeakSlotPtr_LockAndExpire);
    RunTest("WeakSlotPtr - 世代番号による再利用の検出", Test_WeakSlotPtr_GenerationGuard);
    RunTest("WeakSlotPtr - UseCountとSwap", Test_WeakSlotPtr_UseCountAndSwap);
    RunTest("WeakSlotPtr - サイズ", Test_WeakSlotPtr_Size);

    PrintCategory("ObjectSlotSystem");
    RunTest("ObjectSlotSystem - CountとForEach", Test_Pool_CountAndForEach);
    RunTest("ObjectSlotSystem - 走査中の削除", Test_Pool_ForEachDeleteDuringIteration);
    RunTest("ObjectSlotSystem - 走査中の生成", Test_Pool_ForEachCreateDuringIteration);
    RunTest("ObjectSlotSystem - SortActiveIndexList", Test_Pool_SortActiveIndexList);
    RunTest("ObjectSlotSystem - Reserve / ShrinkToFit / MaxCapacity", Test_Pool_ReserveShrinkMaxCapacity);
    RunTest("ObjectSlotSystem - Clear", Test_Pool_Clear);

    PrintCategory("SignalSlotPtr");
    RunTest("SignalSlotPtr - 基本的な購読と通知", Test_Signal_BasicNotify);
    RunTest("SignalSlotPtr - 逆順通知", Test_Signal_ReverseOrder);
    RunTest("SignalSlotPtr - 手動解除", Test_Signal_ManualUnsubscribe);
    RunTest("SignalSlotPtr - Subscriptionのムーブ", Test_Signal_SubscriptionMove);
    RunTest("SignalSlotPtr - Swapとstd::map", Test_Signal_SwapAndMap);
    RunTest("SignalSlotPtr - UpdateCallback", Test_Signal_UpdateCallback);
    RunTest("SignalSlotPtr - 通知中の削除の遅延", Test_Signal_DeferredRemovalDuringNotify);
    RunTest("SignalSlotPtr - サイズ", Test_Signal_Size);

    PrintCategory("WeakSignalSlotPtr");
    RunTest("WeakSignalSlotPtr - Lockと期限切れ", Test_WeakSignal_LockAndExpire);
    RunTest("WeakSignalSlotPtr - 弱参照からの購読", Test_WeakSignal_Subscribe);
    RunTest("WeakSignalSlotPtr - GetWeak / Swap / コンテナ", Test_WeakSignal_GetWeakSwapContainer);

    PrintCategory("SlotRef");
    RunTest("SlotRef - 変換と統一管理", Test_SlotRef_Conversion);
    RunTest("SlotRef - コピー・ムーブ・参照カウント", Test_SlotRef_CopyMoveRefCount);
    RunTest("SlotRef - SlotRefだけで生存維持", Test_SlotRef_KeepsAlive);
    RunTest("SlotRef - 大量生成後も有効", Test_SlotRef_ValidAfterManyCreates);
    RunTest("SlotRef - Swap", Test_SlotRef_Swap);
    RunTest("SlotRef - エイリアシング", Test_SlotRef_Aliasing);
    RunTest("SlotRef - Subscribe（SubscriptionRef）", Test_SlotRef_Subscribe);

    PrintCategory("EnableSlotFromThis");
    RunTest("EnableSlotFromThis - ObjectSlotSystem版", Test_FromThis_ObjectSlot);
    RunTest("EnableSlotFromThis - SignalSlotSystem版", Test_FromThis_SignalSlot);

    PrintCategory("古いポインタの安全性");
    if (SlotTraits::SUPPORTS_STALE_POINTER_SAFETY) {
        RunTest("古いポインタ - Clear後", Test_Stale_AfterClear);
        RunTest("古いポインタ - ShrinkToFit後", Test_Stale_AfterShrinkToFit);
    }
    RunTest("SignalSlotPtr - 通知中の復活", Test_Signal_RevivedDuringNotify);
    RunTest("SlotRef - 通知なしプールの要素から", Test_SlotRef_FromObjectSlotSystem);
    RunTest("Subscription - ToRef", Test_Subscription_ToRef);

    PrintCategory("Compact");
    RunTest("Compact - ポインタ・弱参照が有効なまま本体が詰まる", Test_Compact_PointersStayValid);
    RunTest("Compact - SlotRef と購読", Test_Compact_SlotRefAndSubscription);

    if (SlotTraits::IS_COMPACT) {
        PrintCategory("圧縮ポインタ");
        RunTest("圧縮ポインタ - 共有領域の配置", Test_Compression_ArenaLayout);
    }

    PrintCategory("複合");
    RunTest("複合 - 通知 + SlotRef", Test_Combined_NotifyAndSlotRef);
    RunTest("複合 - WeakSignalSlotPtr + Subscribe による自動解放", Test_Combined_WeakSubscribeAutoRelease);
    RunTest("複合 - エイリアシング + 通知", Test_Combined_AliasingAndNotify);
}

// ======================================================
// 4. 計測基盤
// ======================================================

/// 計測の規模（--quick で縮小される）
struct BenchmarkConfig {
    int repeatCount = 7;               ///< 各項目の繰り返し回数（中央値を採用）
    int createCount = 100000;          ///< 生成・破棄の回数
    int copyCount = 1000000;           ///< コピーの回数
    int accessCount = 1000000;         ///< アクセスの回数
    int polymorphicCount = 100000;     ///< ポリモーフィックアクセスの回数
    int sceneObjectCount = 10000;      ///< シーン内オブジェクト数
    int frameCount = 200;              ///< フレーム数
    int churnPerFrame = 50;            ///< フレームあたりの生成・破棄
    int notifyBufferCount = 10000;     ///< 解放通知の購読数
    size_t sparseLiveCount = 100000;   ///< 歯抜け走査での生存数
    size_t pointerArrayCount = 1000000;///< ポインタ配列走査の本数
};

static BenchmarkConfig g_config;

/// キャッシュ追い出し用バッファのサイズ（L2を大きく上回る量）
constexpr size_t CACHE_FLUSH_BYTES = 64 * 1024 * 1024;

/// キャッシュライン1本のサイズ
constexpr size_t CACHE_LINE_BYTES = 64;

/// 直前の計測で載ったデータをキャッシュから追い出す
static void FlushCache() {
    static std::vector<char> buffer(CACHE_FLUSH_BYTES, 0);
    for (size_t offset = 0; offset < buffer.size(); offset += CACHE_LINE_BYTES) ++buffer[offset];
}

/// 中央値を返す
static double Median(std::vector<double> samples) {
    std::sort(samples.begin(), samples.end());
    return samples[samples.size() / 2];
}

/**
 * @brief 処理を繰り返し計測し、1操作あたりの中央値をナノ秒で返す
 * @param operationsPerRun 1回の実行に含まれる操作数（この数で割って1操作あたりにする）
 * @param flushBeforeEachRun 各実行前にキャッシュを追い出すか
 * @param body 計測対象（引数なし）
 */
template<typename Func>
static double MeasureNanoseconds(size_t operationsPerRun, bool flushBeforeEachRun, Func&& body) {
    std::vector<double> samples;
    for (int run = 0; run < g_config.repeatCount; ++run) {
        if (flushBeforeEachRun) FlushCache();
        const auto start = std::chrono::high_resolution_clock::now();
        body();
        const auto end = std::chrono::high_resolution_clock::now();
        samples.push_back(std::chrono::duration<double, std::nano>(end - start).count() / static_cast<double>(operationsPerRun));
    }
    return Median(samples);
}

/// 最適化で計測対象が消えるのを防ぐための受け皿
static volatile uint64_t g_sink = 0;

/// 文字列の表示幅を返す（全角文字を2、半角を1として数える）
static size_t DisplayWidth(const std::string& text) {
    size_t width = 0;
    for (unsigned char c : text) {
        if ((c & 0xC0) == 0x80) continue;      // UTF-8の継続バイトは数えない
        width += (c >= 0x80) ? 2 : 1;          // 先頭バイトが0x80以上なら全角とみなす
    }
    return width;
}

/// 表示幅に合わせて右側を空白で埋める
static std::string PadRight(const std::string& text, size_t width) {
    const size_t current = DisplayWidth(text);
    return (current >= width) ? text : text + std::string(width - current, ' ');
}

/// 表示幅に合わせて左側を空白で埋める
static std::string PadLeft(const std::string& text, size_t width) {
    const size_t current = DisplayWidth(text);
    return (current >= width) ? text : std::string(width - current, ' ') + text;
}

/// ナノ秒を「12.3 ns」の形の文字列にする
static std::string FormatNs(double nanoseconds, int precision = 1) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(precision) << nanoseconds << " ns";
    return stream.str();
}

/// 項目名の列幅
constexpr size_t LABEL_WIDTH = 40;

/// 2方式の比較行を表示する
static void PrintRow(const std::string& label, double leftNs, double rightNs) {
    const double ratio = (rightNs > 0.0) ? leftNs / rightNs : 0.0;
    std::ostringstream ratioText;
    ratioText << std::fixed << std::setprecision(2) << ratio << "x";
    std::cout << "  " << PadRight(label, LABEL_WIDTH)
              << PadLeft(FormatNs(leftNs), 12) << PadLeft(FormatNs(rightNs), 12)
              << PadLeft(ratioText.str(), 9) << std::endl;
}

/// 比較表の見出しを表示する
static void PrintTableHeader(const std::string& leftName, const std::string& rightName) {
    std::cout << "  " << PadRight("項目", LABEL_WIDTH)
              << PadLeft(leftName, 12) << PadLeft(rightName, 12) << PadLeft("比率", 9) << std::endl;
}

// ======================================================
// 5. 性能計測
// ======================================================

// ------------------------------------------------------
// 5-A. 基本操作（shared_ptr 比較）
// ------------------------------------------------------

static void Bench_BasicOperations() {
    std::cout << "\n--- 基本操作（ObjectSlot vs shared_ptr） ---" << std::endl;
    PrintTableHeader("ObjectSlot", "shared_ptr");

    auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
    auto& signalPool = SignalSlotSystem<BenchData>::GetInstance();
    auto& refPool = RefSlotSystem<BenchObject>::GetInstance();
    pool.Clear(); signalPool.Clear(); refPool.Clear();
    pool.Reserve(g_config.createCount);

    // 生成＋破棄
    {
        const size_t count = g_config.createCount;
        double slot = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto p = pool.Create(BenchData{}); g_sink += p->id; } });
        double shared = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto p = std::make_shared<BenchData>(); g_sink += p->id; } });
        PrintRow("生成 + 破棄（SlotPtr）", slot, shared);
    }
    {
        const size_t count = g_config.createCount;
        double slot = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto p = signalPool.Create(BenchData{}); g_sink += p->id; } });
        double shared = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto p = std::make_shared<BenchData>(); g_sink += p->id; } });
        PrintRow("生成 + 破棄（SignalSlotPtr）", slot, shared);
    }
    // コピー＋破棄
    {
        const size_t count = g_config.copyCount;
        auto slotPtr = pool.Create(BenchData{});
        auto sharedPtr = std::make_shared<BenchData>();
        double slot = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto copy = slotPtr; g_sink += copy->id; } });
        double shared = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto copy = sharedPtr; g_sink += copy->id; } });
        PrintRow("コピー + 破棄", slot, shared);
    }
    // 要素アクセス（同じポインタを繰り返し辿る）
    {
        const size_t count = g_config.accessCount;
        auto slotPtr = pool.Create(BenchData{ 1, 2, 3, 4 });
        auto sharedPtr = std::make_shared<BenchData>(BenchData{ 1, 2, 3, 4 });
        double slot = MeasureNanoseconds(count, false, [&] { uint64_t t = 0; for (size_t i = 0; i < count; ++i) t += slotPtr->id; g_sink += t; });
        double shared = MeasureNanoseconds(count, false, [&] { uint64_t t = 0; for (size_t i = 0; i < count; ++i) t += sharedPtr->id; g_sink += t; });
        PrintRow("要素アクセス（同じポインタ）", slot, shared);
    }
    // ポリモーフィックアクセス
    {
        const size_t count = g_config.polymorphicCount;
        std::vector<SlotRef<IBenchObject>> refs;
        std::vector<std::shared_ptr<IBenchObject>> shareds;
        for (size_t i = 0; i < count; ++i) {
            refs.push_back(SlotRef<IBenchObject>(refPool.Create(BenchObject(static_cast<float>(i)))));
            shareds.push_back(std::make_shared<BenchObject>(static_cast<float>(i)));
        }
        double slot = MeasureNanoseconds(count, true, [&] { float t = 0; for (auto& r : refs) t += r->GetValue(); g_sink += static_cast<uint64_t>(t); });
        double shared = MeasureNanoseconds(count, true, [&] { float t = 0; for (auto& s : shareds) t += s->GetValue(); g_sink += static_cast<uint64_t>(t); });
        PrintRow("ポリモーフィックアクセス（SlotRef）", slot, shared);
    }
    // 弱参照 Lock + アクセス + 破棄
    {
        const size_t count = g_config.accessCount;
        auto slotPtr = pool.Create(BenchData{});
        auto weakSlot = slotPtr.GetWeak();
        auto sharedPtr = std::make_shared<BenchData>();
        std::weak_ptr<BenchData> weakShared = sharedPtr;
        double slot = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto l = weakSlot.Lock(); g_sink += l->id; } });
        double shared = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { auto l = weakShared.lock(); g_sink += l->id; } });
        PrintRow("弱参照 Lock + アクセス + 破棄", slot, shared);
    }
    // SlotRef 生成＋破棄
    {
        const size_t count = g_config.createCount;
        double slot = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { SlotRef<IBenchObject> r(refPool.Create(BenchObject())); g_sink += static_cast<uint64_t>(r->GetValue()); } });
        double shared = MeasureNanoseconds(count, false, [&] { for (size_t i = 0; i < count; ++i) { std::shared_ptr<IBenchObject> s = std::make_shared<BenchObject>(); g_sink += static_cast<uint64_t>(s->GetValue()); } });
        PrintRow("生成 + 破棄（SlotRef<Base>）", slot, shared);
    }
    pool.Clear(); signalPool.Clear(); refPool.Clear();
}

// ------------------------------------------------------
// 5-B. 実使用パターン
// ------------------------------------------------------

static void Bench_UsagePatterns() {
    std::cout << "\n--- 実使用パターン（ObjectSlot vs shared_ptr） ---" << std::endl;
    std::cout << "  オブジェクト数 " << g_config.sceneObjectCount << " / フレーム数 " << g_config.frameCount
              << " / フレームあたり生成・破棄 " << g_config.churnPerFrame << std::endl;
    PrintTableHeader("ObjectSlot", "shared_ptr");

    auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
    auto& refPool = RefSlotSystem<BenchObject>::GetInstance();
    auto& devicePool = SignalSlotSystem<BenchData>::GetInstance();
    pool.Clear(); refPool.Clear(); devicePool.Clear();
    const size_t objectCount = g_config.sceneObjectCount;

    // シーンロード（一括生成）
    {
        std::vector<SlotPtr<BenchData>> slots;
        std::vector<std::shared_ptr<BenchData>> shareds;
        double slot = MeasureNanoseconds(objectCount, true, [&] {
            slots.clear(); pool.Clear(); slots.reserve(objectCount);
            for (size_t i = 0; i < objectCount; ++i) slots.push_back(pool.Create(BenchData{ 0, 0, 0, static_cast<int>(i) }));
        });
        double shared = MeasureNanoseconds(objectCount, true, [&] {
            shareds.clear(); shareds.reserve(objectCount);
            for (size_t i = 0; i < objectCount; ++i) shareds.push_back(std::make_shared<BenchData>(BenchData{ 0, 0, 0, static_cast<int>(i) }));
        });
        PrintRow("シーンロード（1オブジェクトあたり）", slot, shared);

        // 毎フレーム位置更新（全走査）
        const size_t updates = objectCount * g_config.frameCount;
        double slotUpdate = MeasureNanoseconds(updates, true, [&] {
            for (int f = 0; f < g_config.frameCount; ++f) pool.ForEach([](SlotHandle, BenchData& d) { d.x += 1.0f; });
        });
        double sharedUpdate = MeasureNanoseconds(updates, true, [&] {
            for (int f = 0; f < g_config.frameCount; ++f) for (auto& s : shareds) s->x += 1.0f;
        });
        PrintRow("毎フレーム位置更新（1オブジェクトあたり）", slotUpdate, sharedUpdate);

        // ゲーム中の生成・破棄（フレームごとにランダムに入れ替え）
        const size_t churnTotal = static_cast<size_t>(g_config.frameCount) * g_config.churnPerFrame;
        std::mt19937 randomEngine(777);
        double slotChurn = MeasureNanoseconds(churnTotal, false, [&] {
            for (int f = 0; f < g_config.frameCount; ++f) for (int c = 0; c < g_config.churnPerFrame; ++c) {
                const size_t i = randomEngine() % slots.size();
                slots[i] = pool.Create(BenchData{});
            }
        });
        double sharedChurn = MeasureNanoseconds(churnTotal, false, [&] {
            for (int f = 0; f < g_config.frameCount; ++f) for (int c = 0; c < g_config.churnPerFrame; ++c) {
                const size_t i = randomEngine() % shareds.size();
                shareds[i] = std::make_shared<BenchData>();
            }
        });
        PrintRow("ゲーム中の生成・破棄（1回あたり）", slotChurn, sharedChurn);

        // 所有権共有（コピー＋アクセス＋破棄）
        double slotShare = MeasureNanoseconds(objectCount, true, [&] { for (auto& p : slots) { auto c = p; g_sink += c->id; } });
        double sharedShare = MeasureNanoseconds(objectCount, true, [&] { for (auto& p : shareds) { auto c = p; g_sink += c->id; } });
        PrintRow("所有権共有（コピー+アクセス+破棄）", slotShare, sharedShare);
    }

    // 描画ループ（基底型で走査）
    {
        std::vector<SlotRef<IBenchObject>> refs;
        std::vector<std::shared_ptr<IBenchObject>> shareds;
        for (size_t i = 0; i < objectCount; ++i) {
            refs.push_back(SlotRef<IBenchObject>(refPool.Create(BenchObject(1.0f))));
            shareds.push_back(std::make_shared<BenchObject>(1.0f));
        }
        const size_t draws = objectCount * g_config.frameCount;
        double slot = MeasureNanoseconds(draws, true, [&] { float t = 0; for (int f = 0; f < g_config.frameCount; ++f) for (auto& r : refs) t += r->GetValue(); g_sink += static_cast<uint64_t>(t); });
        double shared = MeasureNanoseconds(draws, true, [&] { float t = 0; for (int f = 0; f < g_config.frameCount; ++f) for (auto& s : shareds) t += s->GetValue(); g_sink += static_cast<uint64_t>(t); });
        PrintRow("描画ループ（基底型、1オブジェクトあたり）", slot, shared);
    }

    // リソース解放通知（1デバイスに多数のバッファが従属。通知の実行だけを計測する）
    {
        const size_t bufferCount = g_config.notifyBufferCount;
        std::vector<double> slotSamples, sharedSamples;
        for (int run = 0; run < g_config.repeatCount; ++run) {
            // ObjectSlot: 購読を準備してから、デバイス解放（＝1万個のコールバック）だけを計る
            auto device = devicePool.Create(BenchData{});
            std::vector<SlotPtr<BenchData>> buffers; buffers.reserve(bufferCount);
            std::vector<Subscription<BenchData>> subs; subs.reserve(bufferCount);
            for (size_t i = 0; i < bufferCount; ++i) {
                auto buffer = pool.Create(BenchData{});
                buffers.push_back(buffer);
                subs.push_back(device.Subscribe([buffer]() mutable { buffer.Reset(); }));
            }
            FlushCache();
            auto start = std::chrono::high_resolution_clock::now();
            device.Reset();
            auto end = std::chrono::high_resolution_clock::now();
            slotSamples.push_back(std::chrono::duration<double, std::nano>(end - start).count() / bufferCount);

            // shared_ptr: 従属側が weak_ptr で生存確認して自分を解放する
            auto sharedDevice = std::make_shared<BenchData>();
            std::weak_ptr<BenchData> weakDevice = sharedDevice;
            std::vector<std::shared_ptr<BenchData>> sharedBuffers; sharedBuffers.reserve(bufferCount);
            for (size_t i = 0; i < bufferCount; ++i) sharedBuffers.push_back(std::make_shared<BenchData>());
            FlushCache();
            start = std::chrono::high_resolution_clock::now();
            sharedDevice.reset();
            for (auto& b : sharedBuffers) if (weakDevice.expired()) b.reset();
            end = std::chrono::high_resolution_clock::now();
            sharedSamples.push_back(std::chrono::duration<double, std::nano>(end - start).count() / bufferCount);
        }
        PrintRow("リソース解放通知（1バッファあたり）", Median(slotSamples), Median(sharedSamples));
    }
    pool.Clear(); refPool.Clear(); devicePool.Clear();
}

// ------------------------------------------------------
// 5-C. 歯抜け走査（生存インデックス一覧の効果）
// ------------------------------------------------------

static void Bench_SparseTraversal() {
    std::cout << "\n--- 歯抜け走査（生存数を固定し、密度だけを変える） ---" << std::endl;
    std::cout << "  生存数 " << g_config.sparseLiveCount << " 個 / 要素 " << sizeof(SparseData) << " バイト" << std::endl;
    std::cout << "  " << PadRight("密度", 8) << PadRight("総スロット数", 14)
              << PadLeft("ForEach 整列前", 16) << PadLeft("ForEach 整列後", 16)
              << PadLeft("Compact後", 12) << PadLeft("Compact所要", 12)
              << PadLeft("vector<shared_ptr>", 20) << std::endl;

    constexpr double DENSITY_LIST[] = { 1.0, 0.25, 0.05 };
    auto& pool = ObjectSlotSystem<SparseData>::GetInstance();
    const size_t live = g_config.sparseLiveCount;

    for (double density : DENSITY_LIST) {
        const size_t total = static_cast<size_t>(live / density);
        std::mt19937 randomEngine(20260924);

        // 削除する位置を決める（生存 live 個を残す）
        std::vector<uint32_t> order(total);
        std::iota(order.begin(), order.end(), 0u);
        std::shuffle(order.begin(), order.end(), randomEngine);

        // ObjectSlot 側
        pool.Clear();
        std::vector<SlotPtr<SparseData>> slots; slots.reserve(total);
        for (size_t i = 0; i < total; ++i) slots.push_back(pool.Create(SparseData{ 1 }));
        for (size_t i = live; i < total; ++i) slots[order[i]].Reset();

        double unsorted = MeasureNanoseconds(live, true, [&] { uint64_t t = 0; pool.ForEach([&](SlotHandle, SparseData& d) { t += d.value; }); g_sink += t; });
        pool.SortActiveIndexList();
        double sorted = MeasureNanoseconds(live, true, [&] { uint64_t t = 0; pool.ForEach([&](SlotHandle, SparseData& d) { t += d.value; }); g_sink += t; });
        const auto compactStart = std::chrono::steady_clock::now();
        pool.Compact();
        const double compactPerElement = std::chrono::duration<double, std::nano>(std::chrono::steady_clock::now() - compactStart).count() / static_cast<double>(live);
        double compacted = MeasureNanoseconds(live, true, [&] { uint64_t t = 0; pool.ForEach([&](SlotHandle, SparseData& d) { t += d.value; }); g_sink += t; });
        slots.clear();
        pool.Clear();

        // shared_ptr 側（同じ位置を nullptr にした vector を全走査）
        std::vector<std::shared_ptr<SparseData>> shareds; shareds.reserve(total);
        for (size_t i = 0; i < total; ++i) shareds.push_back(std::make_shared<SparseData>(SparseData{ 1 }));
        for (size_t i = live; i < total; ++i) shareds[order[i]].reset();
        double shared = MeasureNanoseconds(live, true, [&] { uint64_t t = 0; for (auto& s : shareds) if (s) t += s->value; g_sink += t; });
        shareds.clear();

        std::cout << "  " << PadRight(std::to_string(static_cast<int>(density * 100.0)) + "%", 8)
                  << PadRight(std::to_string(total), 14)
                  << PadLeft(FormatNs(unsorted, 2), 16) << PadLeft(FormatNs(sorted, 2), 16)
                  << PadLeft(FormatNs(compacted, 2), 12) << PadLeft(FormatNs(compactPerElement, 1), 12)
                  << PadLeft(FormatNs(shared, 2), 20) << std::endl;
    }
    std::cout << "  ※ 1生存要素あたりのナノ秒。整列は SortActiveIndexList()、Compact後は Compact() 呼び出し後の走査。Compact所要は生存要素1個あたりの詰め直しコスト" << std::endl;
}

// ------------------------------------------------------
// 5-D. ポインタ配列の走査（4バイト化の効果）
// ------------------------------------------------------

static void Bench_PointerArrayTraversal() {
    std::cout << "\n--- ポインタ配列の走査（ポインタ本数 " << g_config.pointerArrayCount << "） ---" << std::endl;
    std::cout << "  " << PadRight("参照先の並び", 16)
              << PadLeft("SlotPtr配列", 16) << PadLeft("shared_ptr配列", 18) << PadLeft("生ポインタ", 14) << std::endl;

    auto& pool = ObjectSlotSystem<SparseData>::GetInstance();
    pool.Clear();
    const size_t count = g_config.pointerArrayCount;

    std::vector<SlotPtr<SparseData>> owners; owners.reserve(count);
    std::vector<std::shared_ptr<SparseData>> sharedOwners; sharedOwners.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        owners.push_back(pool.Create(SparseData{ 1 }));
        sharedOwners.push_back(std::make_shared<SparseData>(SparseData{ 1 }));
    }

    for (int mode = 0; mode < 2; ++mode) {
        std::vector<uint32_t> order(count);
        std::iota(order.begin(), order.end(), 0u);
        if (mode == 1) std::shuffle(order.begin(), order.end(), std::mt19937(99));

        std::vector<SlotPtr<SparseData>> slotArray; slotArray.reserve(count);
        std::vector<std::shared_ptr<SparseData>> sharedArray; sharedArray.reserve(count);
        std::vector<SparseData*> rawArray; rawArray.reserve(count);
        for (size_t i = 0; i < count; ++i) {
            slotArray.push_back(owners[order[i]]);
            sharedArray.push_back(sharedOwners[order[i]]);
            rawArray.push_back(owners[order[i]].Get());
        }

        double slot = MeasureNanoseconds(count, true, [&] { uint64_t t = 0; for (auto& p : slotArray) t += p->value; g_sink += t; });
        double shared = MeasureNanoseconds(count, true, [&] { uint64_t t = 0; for (auto& p : sharedArray) t += p->value; g_sink += t; });
        double raw = MeasureNanoseconds(count, true, [&] { uint64_t t = 0; for (auto* p : rawArray) t += p->value; g_sink += t; });

        std::cout << "  " << PadRight(mode == 0 ? "順次" : "ランダム", 16)
                  << PadLeft(FormatNs(slot, 2), 16) << PadLeft(FormatNs(shared, 2), 18) << PadLeft(FormatNs(raw, 2), 14) << std::endl;
    }
    std::cout << "  ※ 1本あたりのナノ秒。配列サイズ: SlotPtr " << sizeof(SlotPtr<SparseData>) * count / 1024 / 1024
              << " MB / shared_ptr " << sizeof(std::shared_ptr<SparseData>) * count / 1024 / 1024 << " MB" << std::endl;
    owners.clear();
    pool.Clear();
}

// ------------------------------------------------------
// 5-E. メモリ使用量
// ------------------------------------------------------

/// バイト数を読みやすい単位で整形する
static std::string FormatBytes(size_t bytes) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(1);
    if (bytes >= 1024ull * 1024 * 1024) stream << bytes / (1024.0 * 1024 * 1024) << " GB";
    else if (bytes >= 1024ull * 1024)   stream << bytes / (1024.0 * 1024) << " MB";
    else if (bytes >= 1024)             stream << bytes / 1024.0 << " KB";
    else                                stream << bytes << " B";
    return stream.str();
}

/// 型のサイズを1行表示する
static void PrintSizeRow(const std::string& name, size_t bytes, const std::string& note = "") {
    std::cout << "  " << PadRight(name, 34) << PadLeft(std::to_string(bytes) + " B", 8)
              << (note.empty() ? "" : "   " + note) << std::endl;
}

/// プールのメモリ使用量を1行表示する
template<typename Usage>
static void PrintUsageRow(const std::string& name, const Usage& usage, size_t elementCount, size_t holderBytesPerElement) {
    const double perElement = static_cast<double>(usage.TotalPhysicalBytes()) / elementCount + holderBytesPerElement;
    std::ostringstream perElementText;
    perElementText << std::fixed << std::setprecision(1) << perElement << " B";
    std::cout << "  " << PadRight(name, 20)
              << PadLeft(FormatBytes(usage.elementBytes), 12)
              << PadLeft(FormatBytes(usage.metadataBytes), 12)
              << PadLeft(FormatBytes(usage.TotalPhysicalBytes()), 12)
              << PadLeft(perElementText.str(), 14) << std::endl;
}

static void Bench_MemoryUsage() {
    std::cout << "\n--- 型のサイズ ---" << std::endl;
    PrintSizeRow("SlotPtr<T>", sizeof(SlotPtr<BenchData>));
    PrintSizeRow("SignalSlotPtr<T>", sizeof(SignalSlotPtr<BenchData>));
    PrintSizeRow("WeakSlotPtr<T>", sizeof(WeakSlotPtr<BenchData>));
    PrintSizeRow("WeakSignalSlotPtr<T>", sizeof(WeakSignalSlotPtr<BenchData>));
    PrintSizeRow("SlotRef<T>", sizeof(SlotRef<IBenchObject>));
    PrintSizeRow("Subscription<T>", sizeof(Subscription<BenchData>));
    PrintSizeRow("SubscriptionRef", sizeof(SubscriptionRef));
    PrintSizeRow("SlotHandle", sizeof(SlotHandle));
    if (SlotTraits::HEADER_BYTES > 0) {
        PrintSizeRow("SlotHeader（要素ごとの見出し）", SlotTraits::HEADER_BYTES);
    }
    PrintSizeRow("std::shared_ptr<T>", sizeof(std::shared_ptr<BenchData>), "（比較用）");
    PrintSizeRow("std::weak_ptr<T>", sizeof(std::weak_ptr<BenchData>), "（比較用）");
    PrintSizeRow("std::unique_ptr<T>", sizeof(std::unique_ptr<BenchData>), "（比較用）");
    PrintSizeRow("T* （生ポインタ）", sizeof(BenchData*), "（比較用）");

    std::cout << "\n--- 要素1個あたりのプール側の管理データ（理論値） ---" << std::endl;
    std::cout << "  要素本体（sizeof(T)）とは別に、スロット1つにつき以下が追加で必要になる" << std::endl;
    PrintSizeRow("ObjectSlotSystem", SlotTraits::OBJECT_POOL_OVERHEAD_BYTES, SlotTraits::OBJECT_POOL_OVERHEAD_NOTE);
    PrintSizeRow("SignalSlotSystem", SlotTraits::SIGNAL_POOL_OVERHEAD_BYTES + sizeof(std::vector<int>), SlotTraits::SIGNAL_POOL_OVERHEAD_NOTE);
    if (SlotTraits::HAS_SEPARATE_REF_POOL) {
        PrintSizeRow("RefSlotSystem", SlotTraits::REF_POOL_OVERHEAD_BYTES + sizeof(std::vector<int>) * 2, SlotTraits::REF_POOL_OVERHEAD_NOTE);
    }
    PrintSizeRow("shared_ptr（make_shared）", 16, "制御ブロック（仮想関数表8 + カウント4×2）+ 確保のオーバーヘッド");

    const size_t count = g_config.sceneObjectCount;
    std::cout << "\n--- 実測: " << count << " 個の要素（sizeof(T) = " << sizeof(BenchData) << " B）を保持した状態 ---" << std::endl;
    std::cout << "  " << PadRight("プール", 20) << PadLeft("要素本体", 12) << PadLeft("管理データ", 12)
              << PadLeft("物理合計", 12) << PadLeft("1要素あたり※", 14) << std::endl;

    // ObjectSlotSystem
    {
        auto& pool = ObjectSlotSystem<BenchData>::GetInstance();
        pool.Clear(); pool.Reserve(count);
        std::vector<SlotPtr<BenchData>> holders; holders.reserve(count);
        for (size_t i = 0; i < count; ++i) holders.push_back(pool.Create(BenchData{}));
        PrintUsageRow("ObjectSlotSystem", pool.GetMemoryUsage(), count, sizeof(SlotPtr<BenchData>));
        holders.clear(); pool.Clear();
    }
    // SignalSlotSystem（購読なし）
    {
        auto& pool = SignalSlotSystem<BenchData>::GetInstance();
        pool.Clear(); pool.Reserve(count);
        std::vector<SignalSlotPtr<BenchData>> holders; holders.reserve(count);
        for (size_t i = 0; i < count; ++i) holders.push_back(pool.Create(BenchData{}));
        PrintUsageRow("SignalSlotSystem", pool.GetMemoryUsage(), count, sizeof(SignalSlotPtr<BenchData>));
        holders.clear(); pool.Clear();
    }
    // RefSlotSystem（各要素にSlotRefを1つ。新実装では SignalSlotSystem の別名）
    {
        auto& pool = RefSlotSystem<BenchObject>::GetInstance();
        pool.Clear(); pool.Reserve(count);
        std::vector<SlotRef<IBenchObject>> holders; holders.reserve(count);
        for (size_t i = 0; i < count; ++i) holders.push_back(SlotRef<IBenchObject>(pool.Create(BenchObject())));
        PrintUsageRow("RefSlotSystem+SlotRef", pool.GetMemoryUsage(), count, sizeof(SlotRef<IBenchObject>));
        holders.clear(); pool.Clear();
    }
    // shared_ptr（ヒープ確保の要求バイト数を実測）
    {
        HeapMark mark;
        std::vector<std::shared_ptr<BenchData>> holders; holders.reserve(count);
        for (size_t i = 0; i < count; ++i) holders.push_back(std::make_shared<BenchData>());
        const size_t bytes = mark.BytesSince();
        const size_t allocations = mark.CountSince();
        std::ostringstream perElementText;
        perElementText << std::fixed << std::setprecision(1) << static_cast<double>(bytes) / count << " B";
        std::cout << "  " << PadRight("shared_ptr", 20)
                  << PadLeft("-", 12) << PadLeft("-", 12)
                  << PadLeft(FormatBytes(bytes), 12) << PadLeft(perElementText.str(), 14)
                  << "   （ヒープ確保 " << allocations << " 回、malloc自体の見出し分は含まない）" << std::endl;
        holders.clear();
    }
    std::cout << "  ※ 1要素あたり = 物理合計 ÷ 要素数 + 保持側のポインタ1本分" << std::endl;
    std::cout << "  ※ 各プールは事前に Reserve(要素数) を呼んで計測。Reserveせずに1個ずつ生成すると、" << std::endl;
    std::cout << "    内部配列の倍々確保により管理データは最大2倍まで膨らむ" << std::endl;
    std::cout << "  ※ shared_ptr の値は要求バイト数のみ。実際は確保1回ごとに malloc の見出し（通常8〜16 B）が上乗せされる" << std::endl;
    std::cout << "  ※ 要素本体（見出し込み）はページ単位（" << virtual_memory_allocator::get_page_size() << " B）で切り上げてコミットされる" << std::endl;
    std::cout << "  ※ 各プールはこの他に仮想アドレス空間を " << FormatBytes(ObjectSlotSystem<BenchData>::GetInstance().GetMemoryUsage().reservedBytes)
              << " 予約しているが、物理メモリは消費しない" << std::endl;
}

static void RunAllBenchmarks() {
    std::cout << "\n========================================" << std::endl;
    std::cout << " 性能計測（各項目 " << g_config.repeatCount << " 回の中央値）" << std::endl;
    std::cout << "========================================" << std::endl;
    Bench_MemoryUsage();
    Bench_BasicOperations();
    Bench_UsagePatterns();
    Bench_SparseTraversal();
    Bench_PointerArrayTraversal();
}

// ======================================================
// 6. main
// ======================================================

int main(int argc, char** argv) {
    bool runTests = true;
    bool runBenchmarks = true;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--no-bench") == 0) runBenchmarks = false;
        else if (std::strcmp(argv[i], "--bench-only") == 0) runTests = false;
        else if (std::strcmp(argv[i], "--quick") == 0) {
            g_config.repeatCount = 3;
            g_config.createCount = 10000;
            g_config.copyCount = 100000;
            g_config.accessCount = 100000;
            g_config.polymorphicCount = 10000;
            g_config.sceneObjectCount = 1000;
            g_config.frameCount = 50;
            g_config.notifyBufferCount = 1000;
            g_config.sparseLiveCount = 10000;
            g_config.pointerArrayCount = 100000;
        }
    }

    if (runTests) {
        RunAllTests();
        std::cout << "\n========================================" << std::endl;
        std::cout << " テスト " << g_report.passedTests << " / " << (g_report.passedTests + g_report.failedTests) << " 成功"
                  << "（検証 " << g_report.passedChecks << " 件成功 / " << g_report.failedChecks << " 件失敗）" << std::endl;
        if (!g_report.failureLog.empty()) {
            std::cout << " 失敗した検証:" << std::endl;
            for (const auto& line : g_report.failureLog) std::cout << line << std::endl;
        }
        std::cout << "========================================" << std::endl;
    }

    if (runBenchmarks) {
        RunAllBenchmarks();
    }

    return g_report.failedChecks;
}
