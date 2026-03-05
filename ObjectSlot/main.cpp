#include "include/ObjectSlot.h"
#include <iostream>
#include <vector>

struct Device
{
    std::string name;
};

struct Buffer
{
    std::string name;
    bool released = false;

    // デバイスの解放通知を受け取る購読オブジェクト
    Subscription<Device> deviceSubscription;

    void Release()
    {
        released = true;
        std::cout << name << " を解放しました" << std::endl;
    }
};

int main()
{
    auto& deviceSlot = SignalSlotSystem<Device>::GetInstance();
    auto& bufferSlot = ObjectSlotSystem<Buffer>::GetInstance();

    // === 基本テスト ===
    std::cout << "=== 基本テスト ===" << std::endl;
    {
        auto device = deviceSlot.Create(Device{ "GPU_0" });
        auto buffer1 = bufferSlot.Create(Buffer{ "VertexBuffer" });
        auto buffer2 = bufferSlot.Create(Buffer{ "IndexBuffer" });

        // 生ポインタで要素本体を取得してからコールバックに渡す
        Buffer* pBuffer1 = buffer1.Get();
        Buffer* pBuffer2 = buffer2.Get();

        buffer1->deviceSubscription = device.Subscribe([pBuffer1]() {
            std::cout << "通知受信: ";
            pBuffer1->Release();
            });

        buffer2->deviceSubscription = device.Subscribe([pBuffer2]() {
            std::cout << "通知受信: ";
            pBuffer2->Release();
            });

        std::cout << "deviceを解放します" << std::endl;
        device.Reset();
    }

    // === 購読者が先に破棄されるテスト ===
    std::cout << "\n=== 購読者が先に破棄されるテスト ===" << std::endl;
    {
        auto device = deviceSlot.Create(Device{ "GPU_1" });

        {
            auto buffer = bufferSlot.Create(Buffer{ "TempBuffer" });

            Buffer* pBuffer = buffer.Get();

            buffer->deviceSubscription = device.Subscribe([pBuffer]() {
                std::cout << "通知受信: ";
                pBuffer->Release();
                });

            std::cout << "bufferがスコープを抜けます" << std::endl;
        }
        // → bufferのSlotPtrが破棄
        // → 参照カウント0 → Buffer削除 → deviceSubscription破棄 → 購読解除

        std::cout << "deviceを解放します（通知は来ない）" << std::endl;
        device.Reset();
    }

    // === 複数購読のテスト ===
    std::cout << "\n=== 複数購読テスト ===" << std::endl;
    {
        auto device = deviceSlot.Create(Device{ "GPU_2" });

        std::vector<Subscription<Device>> subscriptions;

        for (int i = 0; i < 3; ++i)
        {
            subscriptions.push_back(
                device.Subscribe([i]() {
                    std::cout << "購読者 " << i << " が通知を受信" << std::endl;
                    })
            );
        }

        std::cout << "deviceを解放します" << std::endl;
        device.Reset();
    }

    // === SlotPtr（軽量版）のテスト ===
    std::cout << "\n=== SlotPtr（軽量版）テスト ===" << std::endl;
    {
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