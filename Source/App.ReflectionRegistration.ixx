module;

#include <entt/entt.hpp>

#include <DirectXMath.h>
export module App.ReflectionRegistration;

import Engine.Core.Reflection;
import Engine.Renderer.SkyLight;
import Engine.Renderer.PassTags;
import App.Luminous.PlayerSystem;
import App.Luminous.OrbOcclusionData;
import App.Luminous.Types;
import App.Luminous.SceneTags;
import App.Luminous.TitleRuntime;   // LuminousTitleView

// ============================================================================
// ゲーム側のコンポーネントをリフレクションへ登録する
//
//   エンジン側は層ごとの登録モジュール (Engine.*.ReflectionRegistration) で
//   済ませてある。ここはその続きで、App 以下のものを登録する。
//
//   新しいコンポーネントを足したら、ここに 1 行足すだけでよい。
//   型はメンバーポインタから決まるので、列挙も std::vector も入れ子の
//   構造体もそのまま書ける (扱えない型はコンパイルエラーになる)。
//
//   **登録しない欄について**
//     毎フレーム作り直す値や、実行中しか意味のない値は登録しない。
//     保存すると、次の実行で意味のない値を復元してしまうため。
//     どれを外したかは、それぞれの登録の近くに書いてある。
// ============================================================================

export namespace App {

    void RegisterAppComponents();

} // namespace App

namespace App {

    void RegisterAppComponents() {
        using namespace Engine::Core;
        auto& registry = ComponentRegistry::Get();

        // ---- 名札だけのもの (中身は無いが、付いているかどうかを保存する) ----
        registry.Register<Engine::Renderer::GBufferPassTag>("GBufferPassTag");
        registry.Register<Engine::Renderer::ShadowPassTag>("ShadowPassTag");
        registry.Register<Engine::Renderer::TranslucentPassTag>("TranslucentPassTag");
        registry.Register<Engine::Renderer::CharacterPassTag>("CharacterPassTag");
        registry.Register<App::Luminous::OrbOccluderTag>("OrbOccluderTag");
        // シーンを .scene.json から起こすための印。
        //   持ち物が覚えていた entt::entity は保存できない (メモリ上の番号なので)。
        //   代わりに印を載せておき、読み込んだ後に view で引き直す
        registry.Register<App::Luminous::LuminousTitleCameraTag>("LuminousTitleCameraTag");
        registry.Register<App::Luminous::LuminousTitleOrbTag>("LuminousTitleOrbTag");

        // ---- 火の粉の種 ----
        //
        //   粒そのものは毎フレーム組み直す派生物なので保存しない。
        //   組み直すのに要る種だけを載せる
        registry.Register<App::Luminous::LuminousFlameEmitterComponent>("LuminousFlameEmitter")
            .Field("Count", &App::Luminous::LuminousFlameEmitterComponent::Count)
            .Field("Radius", &App::Luminous::LuminousFlameEmitterComponent::Radius)
            .Field("BaseScale", &App::Luminous::LuminousFlameEmitterComponent::BaseScale);

        // ---- シーンごとの設定 (計画 18 の段 2) ----
        //
        //   コードのシーンが**作るときの引数**で分けていた変種を、部品にした。
        //   シーンのファイルに 1 体置いておくと、組み立てるシステムが読む。
        //   登録するとインスペクターにも出るので、開いたまま直せる
        registry.RegisterEnum<App::Luminous::LuminousTitleView>("LuminousTitleView")
            .Value("MainMenu", App::Luminous::LuminousTitleView::MainMenu)
            .Value("StageSelect", App::Luminous::LuminousTitleView::StageSelect)
            .Value("CustomStageList", App::Luminous::LuminousTitleView::CustomStageList);

        registry.Register<App::Luminous::LuminousTitleSettings>("LuminousTitleSettings")
            .Field("View", &App::Luminous::LuminousTitleSettings::View);

        registry.Register<App::Luminous::LuminousPlaySettings>("LuminousPlaySettings")
            .Field("StagePath", &App::Luminous::LuminousPlaySettings::StagePath)
            .Field("StageNumber", &App::Luminous::LuminousPlaySettings::StageNumber)
            .Field("ReturnToEditor", &App::Luminous::LuminousPlaySettings::ReturnToEditor);

        registry.Register<App::Luminous::LuminousEditorSettings>("LuminousEditorSettings")
            .Field("PaletteCategory", &App::Luminous::LuminousEditorSettings::PaletteCategory);

        // ---- 環境光 ----
        registry.Register<Engine::Renderer::SkyLightComponent>("SkyLight")
            .Field("Environment", &Engine::Renderer::SkyLightComponent::Environment)
            .Field("DiffuseIntensity", &Engine::Renderer::SkyLightComponent::DiffuseIntensity)
            .Field("SpecularIntensity", &Engine::Renderer::SkyLightComponent::SpecularIntensity)
            .Field("DrawSky", &Engine::Renderer::SkyLightComponent::DrawSky);

        // ---- ステージに置いたもの ----
        //
        //   ステージの中身 (壁・床・台座・小物) は PlacedObject という 1 つの型で
        //   表されている。これを**置いたエンティティに載せる**ことで、
        //   インスペクターから種類・位置・相 (Phase)・光源をそのまま直せるようになる。
        //   以前はシーンが抱える std::vector の中にあり、エディタからは見えなかった。
        using App::Luminous::AssetCategory;
        using App::Luminous::PlacementType;
        using App::Luminous::MaterialPhase;
        using App::Luminous::CellEdge;
        using App::Luminous::SpawnFacing;

        registry.RegisterEnum<AssetCategory>("LuminousAssetCategory")
            .Value("Floor", AssetCategory::Floor)
            .Value("Wall", AssetCategory::Wall)
            .Value("Stairs", AssetCategory::Stairs)
            .Value("Pedestal", AssetCategory::Pedestal)
            .Value("Special", AssetCategory::Special)
            .Value("Prop", AssetCategory::Prop);

        registry.RegisterEnum<PlacementType>("LuminousPlacementType")
            .Value("CellSnap", PlacementType::CellSnap)
            .Value("EdgeSnap", PlacementType::EdgeSnap)
            .Value("FreeAttach", PlacementType::FreeAttach);

        registry.RegisterEnum<MaterialPhase>("LuminousMaterialPhase")
            .Value("Normal", MaterialPhase::Normal)
            .Value("Phase", MaterialPhase::Phase);

        registry.RegisterEnum<CellEdge>("LuminousCellEdge")
            .Value("North", CellEdge::North)
            .Value("South", CellEdge::South)
            .Value("East", CellEdge::East)
            .Value("West", CellEdge::West);

        registry.RegisterEnum<SpawnFacing>("LuminousSpawnFacing")
            .Value("North", SpawnFacing::North)
            .Value("East", SpawnFacing::East)
            .Value("South", SpawnFacing::South)
            .Value("West", SpawnFacing::West);

        registry.RegisterStruct<App::Luminous::GridCoord>("LuminousGridCoord")
            .Field("X", &App::Luminous::GridCoord::X)
            .Field("Z", &App::Luminous::GridCoord::Z)
            .Field("Floor", &App::Luminous::GridCoord::Floor);

        registry.Register<App::Luminous::PlacedObject>("LuminousPlacedObject")
            // InstanceId はステージが配るもの。人が書き換えると対応が壊れる
            .Field("InstanceId", &App::Luminous::PlacedObject::InstanceId).ReadOnly()
            .Field("AssetId", &App::Luminous::PlacedObject::AssetId)
            .Field("Category", &App::Luminous::PlacedObject::Category)
            .Field("Placement", &App::Luminous::PlacedObject::Placement)
            .Field("FloorIndex", &App::Luminous::PlacedObject::FloorIndex)
            .Field("Cell", &App::Luminous::PlacedObject::Cell)
            .Field("Edge", &App::Luminous::PlacedObject::Edge)
            .Field("Position", &App::Luminous::PlacedObject::Position)
            .Field("Rotation", &App::Luminous::PlacedObject::Rotation)
            .Field("Scale", &App::Luminous::PlacedObject::Scale)
            .Field("Phase", &App::Luminous::PlacedObject::Phase)
            .Field("HasInitialOrb", &App::Luminous::PlacedObject::HasInitialOrb)
            .Field("Facing", &App::Luminous::PlacedObject::Facing)
            .Field("HasLight", &App::Luminous::PlacedObject::HasLight)
            .Field("LightColor", &App::Luminous::PlacedObject::LightColor)
            .Field("LightIntensity", &App::Luminous::PlacedObject::LightIntensity)
            .Field("LightRadius", &App::Luminous::PlacedObject::LightRadius);

        // ---- プレイヤー ----
        //
        //   欄を 2 つに分けてある。
        //     **作ったときの値** (保存する) … 開始位置・向き・体の寸法・速さの設定
        //     **走っている間の値** (.Runtime() で保存しない)
        //                          … いまの速度・接地・掴んでいる宝玉・拾った数
        //
        //   混ぜて保存すると、シーンのファイルが「レベル」ではなく
        //   「セーブデータ」になってしまう (E-05)。
        //   Position と Yaw/Pitch は開始位置でもあるので保存する
        //   (書き出すのは編集中だけ、という約束で使う)
        registry.Register<App::Luminous::LuminousPlayerComponent>("LuminousPlayer")
            .Field("Position", &App::Luminous::LuminousPlayerComponent::Position)
            .Field("Yaw", &App::Luminous::LuminousPlayerComponent::Yaw)
            .Field("Pitch", &App::Luminous::LuminousPlayerComponent::Pitch)
            .Field("EyeHeight", &App::Luminous::LuminousPlayerComponent::EyeHeight)
            .Field("Radius", &App::Luminous::LuminousPlayerComponent::Radius)
            .Field("MoveSpeed", &App::Luminous::LuminousPlayerComponent::MoveSpeed)
            .Field("LookSpeed", &App::Luminous::LuminousPlayerComponent::LookSpeed)
            .Field("IsHoldingOrb", &App::Luminous::LuminousPlayerComponent::IsHoldingOrb)
                .ReadOnly().Runtime()
            .Field("HeldOrbId", &App::Luminous::LuminousPlayerComponent::HeldOrbId)
                .ReadOnly().Runtime()
            .Field("Velocity", &App::Luminous::LuminousPlayerComponent::Velocity)
                .ReadOnly().Runtime()
            .Field("IsGrounded", &App::Luminous::LuminousPlayerComponent::IsGrounded)
                .ReadOnly().Runtime()
            .Field("CurrentFloorHeight",
                   &App::Luminous::LuminousPlayerComponent::CurrentFloorHeight)
                .ReadOnly().Runtime()
            // 拾った数・差した数は、そのプレイでの成績 (レベルの一部ではない)
            .Field("OrbPickupCount", &App::Luminous::LuminousPlayerComponent::OrbPickupCount)
                .Runtime()
            .Field("PedestalInsertCount",
                   &App::Luminous::LuminousPlayerComponent::PedestalInsertCount)
                .Runtime();
    }

} // namespace App
