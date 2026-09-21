module;

#include <entt/entt.hpp>

#include <DirectXMath.h>
export module App.ReflectionRegistration;

import Engine.Core.Reflection;
import App.Graphics.SkyLight;
import App.Graphics.PassTags;
import App.Luminous.PlayerSystem;
import App.Luminous.OrbOcclusionData;
import App.Luminous.Types;

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
        registry.Register<App::Graphics::GBufferPassTag>("GBufferPassTag");
        registry.Register<App::Graphics::ShadowPassTag>("ShadowPassTag");
        registry.Register<App::Graphics::TranslucentPassTag>("TranslucentPassTag");
        registry.Register<App::Graphics::CharacterPassTag>("CharacterPassTag");
        registry.Register<App::Luminous::OrbOccluderTag>("OrbOccluderTag");

        // ---- 環境光 ----
        registry.Register<App::Graphics::SkyLightComponent>("SkyLight")
            .Field("Environment", &App::Graphics::SkyLightComponent::Environment)
            .Field("DiffuseIntensity", &App::Graphics::SkyLightComponent::DiffuseIntensity)
            .Field("SpecularIntensity", &App::Graphics::SkyLightComponent::SpecularIntensity)
            .Field("DrawSky", &App::Graphics::SkyLightComponent::DrawSky);

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
