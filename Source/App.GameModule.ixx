module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <memory>
#include <string>

#include <entt/entt.hpp>
#include <DirectXMath.h>
#include <EngineDebug.h>

export module App.GameModule;

import Engine.Core.GameModule;
import Engine.Core.SystemManager;
import Engine.Core.SystemContext;
import Engine.Core.SceneFile;   // DataScene (シーンのファイルから起こす)
import App.Luminous.Transition;   // LuminousScenes (シーンのファイルのパス)
import Engine.Debug.Log;
import Engine.Graphics.RenderPipeline;

import App.ReflectionRegistration;
import App.Luminous.Scenes;
import App.Luminous.StageData;
import App.Luminous.StageCatalog;
import App.Luminous.InputConfig;
import App.Luminous.DebugTools;
import App.Luminous.OrbOcclusionFeature;

// ============================================================================
// ゲームがエンジンに自分を差し込む入口 (S-4)
//
//   exe はこの 1 つを呼ぶだけで、ゲームの型を 1 つも知らずに済む。
//   シーンは名前で引き、起動オプションとの対応もここで決める。
//
//   このプロジェクトには **Luvia (Luminous Shift) だけ** が入っている。
// ============================================================================

export namespace App {

    void RegisterGameModule(Engine::Core::GameModuleContext& ctx);

}

namespace App {

    using namespace Engine::Core;
    using namespace App::Luminous;

    // 自動テストでシーンを順に巡回して、全部立ち上がることを確かめる
    void MakeLuminousLifecycleTestSystem(GameModuleContext& ctx) {
        GameModuleContext* owner = &ctx;
        ctx.RegisterSystem("LuminousLifecycleTestSystem", SystemPhase::Update,
            [owner](SystemContext& sys) {
                if (!owner->IsAutoTest()) return;

                static int frame = 0;
                frame++;
                ENGINE_LOG_INFO("App", "[AutoTest] Luminous SystemContext Update Frame {}", frame);

                // **行き先はファイルのパス** (計画 18 の段 3)。
                //   コードのシーンを作って渡していた頃の巡回を、そのまま
                //   データのシーンへ置き換えたもの。シーンの入れ替わりと
                //   片付け (Teardown のシステム) が通ることを、ここで見ている
                const auto go = [&sys](const char* path) {
                    sys.RequestSceneLoad(std::make_shared<Engine::Core::DataScene>(path));
                };
                if (frame == 10) {
                    ENGINE_LOG_INFO("App", "[AutoTest] Frame 10: Title verified. Requesting Editor...");
                    go(LuminousScenes::Editor);
                }
                else if (frame == 25) {
                    ENGINE_LOG_INFO("App", "[AutoTest] Frame 25: Editor verified. Requesting Play...");
                    go(LuminousScenes::Play);
                }
                else if (frame == 45) {
                    ENGINE_LOG_INFO("App", "[AutoTest] Frame 45: Play verified. Requesting StageSelect...");
                    go(LuminousScenes::StageSelect);
                }
                else if (frame == 55) {
                    ENGINE_LOG_INFO("App", "[AutoTest] Frame 55: StageSelect verified. Requesting Title...");
                    go(LuminousScenes::Title);
                }
                else if (frame == 70) {
                    ENGINE_LOG_INFO("App", "[AutoTest] ALL LUMINOUS SHIFT SCENES & PIPELINES VERIFIED SUCCESSFULLY WITH 0 RUNTIME ERRORS!");
                    owner->NotifyAutoTestCompleted();
                    PostQuitMessage(0);
                }
            });
    }

    // 起動オプションとシーンの対応。**長い綴りを先に**書く
    // (--editor-walls が --editor に食われないようにするため)
    //
    //   行き先は**データのシーン (.scene.json) のパス**。
    //   コードで登録した名前を指していた頃は、エディタで開けず、保存もできず、
    //   中身を直すにはコンパイルが要った (計画 18 の段 2)。
    //   `--base-stage=N` だけはシーンが 1 つで、番号は
    //   LuminousStageBuildSystem が起動オプションから読む
    void RegisterSceneAliases(GameModuleContext& ctx) {
        ctx.RegisterSceneAlias("--editor-specials", "Assets/Scenes/Luminous.Editor.Specials.scene.json");
        ctx.RegisterSceneAlias("--editor-props", "Assets/Scenes/Luminous.Editor.Props.scene.json");
        ctx.RegisterSceneAlias("--editor-walls", "Assets/Scenes/Luminous.Editor.Walls.scene.json");
        ctx.RegisterSceneAlias("--editor", "Assets/Scenes/Luminous.Editor.scene.json");
        ctx.RegisterSceneAlias("--base-stage=", "Assets/Scenes/Luminous.Play.scene.json");
        ctx.RegisterSceneAlias("--stageselect", "Assets/Scenes/Luminous.StageSelect.scene.json");
        ctx.RegisterSceneAlias("--play-shadow-edge", "Assets/Scenes/Luminous.Play.ShadowEdge.scene.json");
        ctx.RegisterSceneAlias("--play-shadow-open", "Assets/Scenes/Luminous.Play.ShadowOpen.scene.json");
        ctx.RegisterSceneAlias("--play-shadow", "Assets/Scenes/Luminous.Play.Shadow.scene.json");
        ctx.RegisterSceneAlias("--play2", "Assets/Scenes/Luminous.Play.MultiFloor.scene.json");
        ctx.RegisterSceneAlias("--play", "Assets/Scenes/Luminous.Play.scene.json");
    }

    void RegisterGameModule(GameModuleContext& ctx) {
        // 1. リフレクションへの型登録 (保存・インスペクター・プレイ / 停止が効くようになる)
        RegisterAppComponents();

        // 2. システムを名前で引けるようにする
        //   **シーンより先に**済ませる。.scene.json の "systems" はここを引く
        RegisterLuminousSystems();
        MakeLuminousLifecycleTestSystem(ctx);

        // 3. シーン。**コードのシーンはもう無い** (計画 18 の段 3)。
        //   起動オプションの行き先も、すべて .scene.json のパス
        RegisterSceneAliases(ctx);
        ctx.SetDefaultScene("Assets/Scenes/Luminous.Title.scene.json");

        // 4. 描画に差し込む機能
        ctx.RegisterPipelineFeature([](Engine::Graphics::RenderPipelineBase& pipeline) {
            pipeline.AddFeature<OrbOcclusionFeature>();   // 宝玉の遮蔽
            });

        // 5. ゲーム固有の立ち上げ
        LuminousInput::RegisterBindings();               // 入力バインド
        LuminousStageCatalog::EnsureBaseStageFiles();    // 基本ステージ JSON の実体化
        LuminousStageCatalog::EnsureTestStageFiles();    // 確認用ステージ JSON の実体化 (計画 18)

        // 開発用にシーンを直接指定して起動したときだけデバッグキーを使えるようにする。
        // 通常起動 (タイトルから) ではレギュレーション外の入力を受け付けない
        LuminousDebugKeys::Enabled =
            ctx.HasOption("--play") || ctx.HasOption("--base-stage=") ||
            ctx.HasOption("--editor") || ctx.HasOption("--stageselect");
#ifdef SHIPPING
        LuminousDebugTools::Enabled = ctx.HasOption("--debug-tools");
#else
        LuminousDebugTools::Enabled = true;
#endif
    }

} // namespace App
