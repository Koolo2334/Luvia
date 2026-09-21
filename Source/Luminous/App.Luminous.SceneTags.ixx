module;

#include <entt/entt.hpp>

export module App.Luminous.SceneTags;

// ============================================================================
// シーンを .scene.json から起こすための印
//
//   **エンティティを鍵にした脇の表を持たない。エンティティに載せる。**
//
//   これまで持ち物 (LuminousTitleRuntime など) は
//   「どのエンティティがカメラか」を `entt::entity` の欄で覚えていた。
//   これはメモリ上の番号なので、保存して読み直すと意味を失う。
//   代わりに**そのエンティティに印を載せて**おけば、読み込んだ後に
//   view で引き直せる。
//
//   ここに置くのは、**その 1 つしか居ないもの**の印と、
//   **毎フレームの演出を組み直すのに要る種**だけ。
//   ステージの中身 (PlacedObject) のような権威データは Types 側にある。
// ============================================================================

export namespace App::Luminous {

    // タイトル画面で宝玉の周りを回るカメラ
    struct LuminousTitleCameraTag {};

    // タイトル画面の台座に浮かぶ宝玉 (上下に揺れて回る)
    struct LuminousTitleOrbTag {};

    // 火の粉を出すもの (壁掛けの松明・燭台)。
    //   粒そのものはエンティティではなく、毎フレーム組み直す派生物なので
    //   保存しない。**組み直すのに要る種だけ**をここに置く。
    //   出どころの位置は、載っているエンティティの Transform から取る
    struct LuminousFlameEmitterComponent {
        // 出す粒の数
        int Count = 8;
        // 螺旋の半径 (燭台は細いので小さい)
        float Radius = 0.115f;
        // 粒の大きさ
        float BaseScale = 0.082f;
    };

} // namespace App::Luminous
