module;

#include <entt/entt.hpp>

#include <DirectXMath.h>
export module App.ReflectionRegistration;

import Engine.Core.Reflection;
import App.Graphics.SkyLight;
import App.Graphics.PassTags;
import App.Luminous.PlayerSystem;
import App.Luminous.OrbOcclusionData;

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

        // ---- プレイヤー ----
        // 視線の先の対象・クリア演出・グライドなどは毎フレーム決まるので保存しない
        registry.Register<App::Luminous::LuminousPlayerComponent>("LuminousPlayer")
            .Field("Position", &App::Luminous::LuminousPlayerComponent::Position)
            .Field("Yaw", &App::Luminous::LuminousPlayerComponent::Yaw)
            .Field("Pitch", &App::Luminous::LuminousPlayerComponent::Pitch)
            .Field("EyeHeight", &App::Luminous::LuminousPlayerComponent::EyeHeight)
            .Field("Radius", &App::Luminous::LuminousPlayerComponent::Radius)
            .Field("MoveSpeed", &App::Luminous::LuminousPlayerComponent::MoveSpeed)
            .Field("LookSpeed", &App::Luminous::LuminousPlayerComponent::LookSpeed)
            .Field("IsHoldingOrb", &App::Luminous::LuminousPlayerComponent::IsHoldingOrb).ReadOnly()
            .Field("HeldOrbId", &App::Luminous::LuminousPlayerComponent::HeldOrbId).ReadOnly()
            .Field("Velocity", &App::Luminous::LuminousPlayerComponent::Velocity).ReadOnly()
            .Field("IsGrounded", &App::Luminous::LuminousPlayerComponent::IsGrounded).ReadOnly()
            .Field("CurrentFloorHeight",
                   &App::Luminous::LuminousPlayerComponent::CurrentFloorHeight).ReadOnly()
            .Field("OrbPickupCount", &App::Luminous::LuminousPlayerComponent::OrbPickupCount)
            .Field("PedestalInsertCount",
                   &App::Luminous::LuminousPlayerComponent::PedestalInsertCount);
    }

} // namespace App
