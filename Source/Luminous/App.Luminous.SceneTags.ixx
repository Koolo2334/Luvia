module;

#include <string>

#include <entt/entt.hpp>

export module App.Luminous.SceneTags;

import App.Luminous.TitleRuntime;   // LuminousTitleView (どの画面から始めるか)

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

    // ========================================================================
    // シーンごとの設定 (計画 18 の段 2)
    //
    //   コードのシーンは**作るときの引数**で変種を分けていた。
    //
    //     LuminousTitleScene(TitleView::StageSelect)
    //     LuminousEditorScene(1)
    //     LuminousPlayScene(LuminousStage::CreateMultiFloorSampleStage())
    //
    //   引数はデータに書けない。代わりに**部品にして、シーンのファイルの中に
    //   1 体置く**。組み立てるシステムが起動時にそれを読む。
    //
    //   こうすると、変種は「別の .scene.json」になる。
    //   エディタで開けて、インスペクターでそのまま直せて、絵で確かめられる。
    //   引数の受け渡しの仕組みを足す必要もない。
    //
    //   **どれも「そのシーンを開いたときの初期値」**。遊んでいる間に変わる値は
    //   持ち物 (LuminousTitleRuntime など) の側にあり、ここには書き戻さない。
    //   シーンをまたいで受け渡す値は LuminousSession (App.Luminous.Session)
    // ========================================================================

    // タイトル画面 — どの画面から始めるか
    struct LuminousTitleSettings {
        LuminousTitleView View = LuminousTitleView::MainMenu;
    };

    // 本編 — どのステージを建てるか。
    //   **遊ぶ人が選んだもの (LuminousSession) のほうが強い**。
    //   ここに書くのは「そのシーンを直に開いたときに何が建つか」で、
    //   ステージ選択から来たときは上書きされる
    struct LuminousPlaySettings {
        // 建てるステージのファイル (空なら基本ステージ 1)
        std::string StagePath;
        // 基本ステージの登録番号 (1 始まり)。0 は実績を記録しない
        int StageNumber = 0;
        // 終わったらステージエディタへ戻る
        bool ReturnToEditor = false;
    };

    // ステージエディタ — どの並び (パレット) を開いた状態で始めるか
    struct LuminousEditorSettings {
        // 0=床 1=壁 2=階段 3=台座 4=特殊 5=小物
        int PaletteCategory = 0;
    };

} // namespace App::Luminous
