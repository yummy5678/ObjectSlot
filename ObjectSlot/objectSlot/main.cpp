// ConsoleApplication1.cpp : このファイルには 'main' 関数が含まれています。プログラム実行の開始と終了がそこで行われます。
//
#include "ObjectSlot.h"
#include <iostream>

struct Mesh 
{
    std::string name;
    uint32_t vertexCount = 0;

    void Draw() const 
    {
        std::cout << "描画: " << name << std::endl;
    }
};

int main()
{
    auto& slot = ObjectSlot<Mesh>::GetInstance();

    // メッシュ作成
    auto box = slot.Create(Mesh{ "Box" });
    auto sphere = slot.Create(Mesh{ "Sphere" });

    // 破棄時コールバックを設定
    box.SetOnDestroy([]() {
        std::cout << "Boxが破棄された" << std::endl;
        });

    sphere.SetOnDestroy([]() {
        std::cout << "Sphereが破棄された" << std::endl;
        });

    // コピー (参照カウント増加)
    std::cout << "=== コピー ===" << std::endl;
    SlotPtr<Mesh> boxCopy = box;
    std::cout << "box UseCount: " << box.UseCount() << std::endl;

    // boxを解放 (まだboxCopyがある)
    std::cout << "\n=== box.Reset() ===" << std::endl;
    box.Reset();
    std::cout << "boxCopy UseCount: " << boxCopy.UseCount() << std::endl;

    // boxCopyを解放 (参照カウント0 → コールバック実行)
    std::cout << "\n=== boxCopy.Reset() ===" << std::endl;
    boxCopy.Reset();

    // sphereはスコープ終了時に自動削除
    std::cout << "\n=== スコープ終了 ===" << std::endl;
    return 0;
}


// プログラムの実行: Ctrl + F5 または [デバッグ] > [デバッグなしで開始] メニュー
// プログラムのデバッグ: F5 または [デバッグ] > [デバッグの開始] メニュー

// 作業を開始するためのヒント: 
//    1. ソリューション エクスプローラー ウィンドウを使用してファイルを追加/管理します 
//   2. チーム エクスプローラー ウィンドウを使用してソース管理に接続します
//   3. 出力ウィンドウを使用して、ビルド出力とその他のメッセージを表示します
//   4. エラー一覧ウィンドウを使用してエラーを表示します
//   5. [プロジェクト] > [新しい項目の追加] と移動して新しいコード ファイルを作成するか、[プロジェクト] > [既存の項目の追加] と移動して既存のコード ファイルをプロジェクトに追加します
//   6. 後ほどこのプロジェクトを再び開く場合、[ファイル] > [開く] > [プロジェクト] と移動して .sln ファイルを選択します
