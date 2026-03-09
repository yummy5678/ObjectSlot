#include "include/objectSlot/ObjectSlot.h"
#include <iostream>
#include <vector>
#include <string>

// === テスト用のインターフェースと具体型 ===

class IDrawable {
public:
    virtual ~IDrawable() = default;
    virtual void Draw() const = 0;
    virtual std::string GetName() const = 0;
};

class Mesh : public IDrawable {
public:
    std::string name;

    Mesh() = default;
    Mesh(const std::string& n) : name(n) {}

    void Draw() const override {
        std::cout << "メッシュ描画: " << name << std::endl;
    }

    std::string GetName() const override { return name; }
};

class Sprite : public IDrawable {
public:
    std::string name;

    Sprite() = default;
    Sprite(const std::string& n) : name(n) {}

    void Draw() const override {
        std::cout << "スプライト描画: " << name << std::endl;
    }

    std::string GetName() const override { return name; }
};

// === 通知テスト用 ===

struct Device {
    std::string name;
};

struct Buffer {
    std::string name;
    bool released = false;

    Subscription<Device> deviceSubscription;

    void Release() {
        released = true;
        std::cout << name << " を解放しました" << std::endl;
    }
};

int main()
{
    // === SlotRef基本テスト ===
    std::cout << "=== SlotRef基本テスト ===" << std::endl;
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "Box" });

        // SignalSlotPtr<Mesh> → SlotRef<IDrawable> に変換
        SlotRef<IDrawable> drawable = mesh;
        drawable->Draw();

        std::cout << "mesh参照カウント: " << mesh.UseCount() << std::endl;
    }

    // === 異なる具体型を基底型で統一管理 ===
    std::cout << "\n=== 異なる型を基底型で統一管理 ===" << std::endl;
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto& spriteSlot = RefSlotSystem<Sprite>::GetInstance();

        auto mesh = meshSlot.Create(Mesh{ "Cube" });
        auto sprite = spriteSlot.Create(Sprite{ "Player" });

        // 異なる型を同じvectorで管理
        std::vector<SlotRef<IDrawable>> drawables;
        drawables.push_back(SlotRef<IDrawable>(mesh));
        drawables.push_back(SlotRef<IDrawable>(sprite));

        for (auto& d : drawables) {
            d->Draw();
        }

        std::cout << "mesh参照カウント: " << mesh.UseCount() << std::endl;
        std::cout << "sprite参照カウント: " << sprite.UseCount() << std::endl;
    }

    // === SlotRefの寿命管理テスト ===
    std::cout << "\n=== SlotRef寿命管理テスト ===" << std::endl;
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        auto mesh = meshSlot.Create(Mesh{ "Sphere" });

        std::cout << "参照カウント(初期): " << mesh.UseCount() << std::endl;

        {
            SlotRef<IDrawable> ref1 = mesh;
            std::cout << "参照カウント(ref1追加): " << mesh.UseCount() << std::endl;

            SlotRef<IDrawable> ref2 = ref1;
            std::cout << "参照カウント(ref2追加): " << mesh.UseCount() << std::endl;
        }
        // ref1, ref2が破棄 → 参照カウント減少

        std::cout << "参照カウント(ref破棄後): " << mesh.UseCount() << std::endl;
    }

    // === SlotRefだけで生存維持テスト ===
    std::cout << "\n=== SlotRefだけで生存維持テスト ===" << std::endl;
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        SlotRef<IDrawable> ref;

        {
            auto mesh = meshSlot.Create(Mesh{ "Plane" });
            ref = mesh;
            std::cout << "参照カウント: " << mesh.UseCount() << std::endl;
        }
        // meshのSignalSlotPtrが破棄されたが、refが参照を保持している

        if (ref.IsValid()) {
            ref->Draw();
            std::cout << "SlotRefで生存維持成功" << std::endl;
        }

        ref.Reset();
        std::cout << "ref解放後: IsValid = " << ref.IsValid() << std::endl;
    }

    // === 再アロケーションテスト ===
    std::cout << "\n=== 再アロケーションテスト ===" << std::endl;
    {
        auto& meshSlot = RefSlotSystem<Mesh>::GetInstance();
        meshSlot.Clear();

        auto mesh0 = meshSlot.Create(Mesh{ "Mesh_0" });
        SlotRef<IDrawable> ref = mesh0;

        std::cout << "再アロケーション前: ";
        ref->Draw();

        // 大量に追加して再アロケーションを誘発
        std::vector<SignalSlotPtr<Mesh>> meshes;
        for (int i = 1; i <= 100; ++i) {
            meshes.push_back(meshSlot.Create(Mesh{ "Mesh_" + std::to_string(i) }));
        }

        std::cout << "再アロケーション後: ";
        if (ref.IsValid()) {
            ref->Draw();
            std::cout << "再アロケーション後もSlotRef有効" << std::endl;
        }
        else {
            std::cout << "エラー: SlotRefが無効になりました" << std::endl;
        }
    }

    // === SignalSlotPtr通知テスト（逆順） ===
    std::cout << "\n=== 通知逆順テスト ===" << std::endl;
    {
        auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
        auto& bufferSlot = ObjectSlotSystem<Buffer>::GetInstance();

        auto device = deviceSlot.Create(Device{ "GPU_0" });
        auto buffer1 = bufferSlot.Create(Buffer{ "Buffer_1st" });
        auto buffer2 = bufferSlot.Create(Buffer{ "Buffer_2nd" });
        auto buffer3 = bufferSlot.Create(Buffer{ "Buffer_3rd" });

        Buffer* pBuffer1 = buffer1.Get();
        Buffer* pBuffer2 = buffer2.Get();
        Buffer* pBuffer3 = buffer3.Get();

        buffer1->deviceSubscription = device.Subscribe([pBuffer1]() {
            std::cout << "通知受信: ";
            pBuffer1->Release();
            });

        buffer2->deviceSubscription = device.Subscribe([pBuffer2]() {
            std::cout << "通知受信: ";
            pBuffer2->Release();
            });

        buffer3->deviceSubscription = device.Subscribe([pBuffer3]() {
            std::cout << "通知受信: ";
            pBuffer3->Release();
            });

        std::cout << "deviceを解放（3rd→2nd→1stの順で通知されるはず）" << std::endl;
        device.Reset();
    }

    // === SlotPtr（軽量版）テスト ===
    std::cout << "\n=== SlotPtr（軽量版）テスト ===" << std::endl;
    {
        auto& bufferSlot = ObjectSlotSystem<Buffer>::GetInstance();
        auto buffer = bufferSlot.Create(Buffer{ "SimpleBuffer" });
        SlotPtr<Buffer> bufferCopy = buffer;

        std::cout << "参照カウント: " << buffer.UseCount() << std::endl;

        buffer.Reset();
        std::cout << "コピーの参照カウント: " << bufferCopy.UseCount() << std::endl;

        bufferCopy.Reset();
        std::cout << "全て解放完了" << std::endl;
    }

    std::cout << "\n=== 全テスト完了 ===" << std::endl;
    return 0;
}