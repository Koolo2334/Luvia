module;

#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.Luminous.PlayRuntime;

import App.Luminous.Types;
import Engine.Core.Components.Lifecycle;
import App.Luminous.StageData;
import App.Luminous.Optics;
import App.Luminous.OrbSystem;
import App.Luminous.DebugTools;
import App.Luminous.Transition;
import App.Luminous.MenuCursor;
import Engine.UI.Types;
import Engine.UI.System;
import App.Luminous.UIScreens;

// ============================================================================
// 本編を動かしている間の持ち物 (LuminousPlayRuntime)
//
//   ここに入れるのは「ステージから作り直せるもの」だけ。
//     ステージそのもの (どの床・壁・台座が、どこに在るか)
//     そこから組み立てた宝玉・遮蔽壁・当たり判定の格子・台座の焼き込み
//     経過時間と一時停止
//
//   **保存しない**。リフレクションに登録していないので .scene.json には出ない。
//   出す必要も無い。ステージ (PlacedObject) さえ在れば、同じものが組み上がる。
//
//   置き場を**シーンからレジストリへ移した**のが要点。
//   シーンの持ち物だった頃は、更新処理が `this` を捕まえたラムダ 1 つでしか
//   書けず、名前つきのシステムに分けられなかった。レジストリに在れば、
//   どのシステムからも registry だけで辿り着ける。
//
//   取り出すのは PlayRuntime(registry)。ぶら下げる先のエンティティは
//   シーンが作る (シーンが終わるときに一緒に片付くように)。
// ============================================================================

export namespace App::Luminous {

    struct LuminousPlayRuntime {
        LuminousStage Stage;

        // ステージから組み立てたもの
        std::vector<OrbRuntimeState> Orbs;
        std::vector<OcclusionWall> OcclusionWalls;
        SpatialGrid Grid;
        std::vector<PedestalColliderBake> BakedPedestals;

        // 遮蔽ジオメトリの世代番号。ステージを組み直すたびに進めて、
        // OrbOcclusionPass 側の深度アトラスキャッシュを確実に捨てさせる
        uint32_t GeometryRevision = 0;

        float GameTime = 0.0f;
        bool Paused = false;

        // ---- 画面に出ているもの (どのエンティティが何を表しているか) ----
        entt::entity PlayerCamera = entt::null;
        entt::entity HeldOrbMesh = entt::null;
        entt::entity HeldOrbLight = entt::null;
        std::unordered_map<uint64_t, entt::entity> ObjectEntities;
        std::unordered_map<uint64_t, entt::entity> PedestalOrbEntities;
        std::unordered_map<uint64_t, entt::entity> PedestalLightEntities;
        std::vector<entt::entity> StageLightEntities;

        // 松明・ろうそくの炎
        struct FlameParticleData {
            entt::entity Entity = entt::null;
            DirectX::XMFLOAT3 Origin = { 0, 0, 0 };
            float LocalY = 0.0f;
            float Phase = 0.0f;
            float Speed = 0.0f;
            float Radius = 0.0f;
            float BaseScale = 0.010f;
            DirectX::XMFLOAT3 Color = { 1.0f, 0.7f, 0.3f };
            DirectX::XMFLOAT3 CurrentPosition = { 0, 0, 0 };
            float CurrentScale = 0.0f;
        };
        std::vector<entt::entity> StageFlameEntities;
        std::vector<FlameParticleData> StageFlameParticles;

        // ゴール誘導パーティクル (放出時に軌道が確定し、移動しても残像のように残る)
        GoalGuidanceStream GuidanceStream;
        std::vector<GuidanceParticleSprite> GuidanceSprites;

        // ---- ゴールの宝箱 (開口・光の噴水・リザルト演出) ----
        struct ChestBurstParticle {
            DirectX::XMFLOAT3 pos;
            DirectX::XMFLOAT3 vel;
            float life = 0.0f;
            float maxLife = 2.0f;
            float baseScale = 0.05f;
        };
        entt::entity GoalChestBase = entt::null;
        entt::entity GoalChestLid = entt::null;
        entt::entity GoalChestLight = entt::null;
        DirectX::XMFLOAT3 GoalChestPos = { 0.0f, 0.0f, 0.0f };
        float GoalChestYaw = 0.0f;
        float ChestAnimTimer = 0.0f;
        bool ChestBurstTriggered = false;
        std::vector<ChestBurstParticle> ChestBurstParticles;

        // ---- 遊び方の取り決め ----
        // エディタから始めたか (終わったらエディタへ戻る)
        bool ReturnToEditorOnExit = false;
        // 基本ステージの登録番号 (1 始まり)。0 はユーザー作成ステージ等で実績を記録しない
        int StageNumber = 0;
        // いま建っているステージのファイル (計画 18)。
        //   「もう一度」で建て直すときに要る。コードのシーンへ
        //   LuminousStage を丸ごと渡していた頃は、ここが要らなかった
        std::string StagePath;

        // ---- 画面に出す小物 ----
        StageIntroOverlay StageIntro;
        bool StageIntroStarted = false;
        LuminousDebugTool DebugTool;
        LuminousMenuCursor Cursor;

        // ---- 音と画像 UI の状態 ----
        bool FootstepToggle = false;
        int LastFootstepIndex = 0;      // ヘッドボブ位相から求めた歩数
        bool FootstepPhaseValid = false;
        bool PrevHoldingOrb = false;
        bool PrevGliding = false;
        bool ClearFanfarePlayed = false;
        int LastHoveredBtn = -1;
        bool WasMouseDown = false;

        // 画面の UI (21 の U8。片付けで閉じる)
        Engine::UI::UIInstanceId HudUi;
        int HudGlyphSet = -1;   // 操作の案内の絵を入れた機器 (変わったら入れ直す)
        Engine::UI::UIInstanceId PauseUi;
        int PauseGlyphSet = -1;
        Engine::UI::UIElement PauseLastFocus;   // 選択が変わったら音を鳴らす
        Engine::UI::UIInstanceId IntroUi;       // ステージ名 (ステージを始めたとき)
        Engine::UI::UIInstanceId ClearUi;
        int ClearGlyphSet = -1;
        Engine::UI::UIElement ClearLastFocus;
        std::unique_ptr<LuminousClearModel> ClearModel;   // クリアの画面の数字 (結んでいる間は動かさないので別に持つ)
    };

    // いま走っている本編の持ち物。無ければ作って返す
    inline LuminousPlayRuntime& PlayRuntime(entt::registry& registry) {
        const auto view = registry.view<LuminousPlayRuntime>();
        if (!view.empty()) return registry.get<LuminousPlayRuntime>(view.front());
        const entt::entity entity = registry.create();
        // **シーンが終われば一緒に片付く**。付けないと、データだけのシーンから
        // 起こしたときに前のシーンの持ち物が居残る
        registry.emplace<Engine::Core::SceneScopeTag>(entity);
        // 走っている間だけの持ち物。.scene.json には書き出さない
        registry.emplace<Engine::Core::GeneratedTag>(entity);
        return registry.emplace<LuminousPlayRuntime>(entity);
    }

} // namespace App::Luminous
