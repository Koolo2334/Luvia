module;

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>
#include <vector>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <format>
#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <EngineDebug.h>

#ifndef IMGUI_DISABLE
#include <imGui/imgui.h>
#endif

export module App.Luminous.Scenes;

import Engine.Core.Scene;
import Engine.Core.SystemContext;
import Engine.Core.Components.Transform;
import Engine.Core.Components.Lifecycle;
import Engine.Core.TransformAPI;
import Engine.Graphics.Components.MeshHierarchy;
import Engine.Core.Prefab;
import Engine.Graphics.Components.Camera;
import Engine.Graphics.Components.Light;
import Engine.Graphics.Components.Material;
import Engine.Graphics.Components.Mesh;
import Engine.Graphics.CameraAPI;
import Engine.Graphics.MeshAttacher;
import Engine.Graphics.MaterialManager;
import Engine.Graphics.MeshManager;
import Engine.Graphics.TextureManager;
import Engine.Input;
import Engine.Math;
import App.Graphics.DeferredPipeline;
import App.Graphics.PassTags;
import App.Luminous.OrbOcclusionData;
import App.Luminous.Types;
import Engine.Core.SystemRegistry;
import Engine.Debug.Log;
import Engine.Common.Config;
import App.Luminous.Optics;
import App.Luminous.MenuCursor;
import App.Luminous.TitleRuntime;
import App.Luminous.EditorRuntime;
import App.Luminous.PlayRuntime;
import App.Luminous.Session;
import App.Luminous.SceneTags;
import App.Luminous.StageData;
import App.Luminous.TerrainSystem;
import App.Luminous.PlayerSystem;
import App.Luminous.OrbSystem;
import App.Luminous.EditorSystem;
import Engine.Audio.Core;
import Engine.Graphics.UIRenderer;
import App.Luminous.AudioConfig;
import App.Luminous.UIConfig;
import App.Luminous.StageCatalog;
import App.Luminous.Transition;
import App.Luminous.InputConfig;
import App.Luminous.Profile;
import App.Luminous.DebugTools;
import App.Graphics.ParticleData;
import Engine.Core.EngineState;
import Engine.Core.Services;

export namespace App::Luminous {

    using namespace DirectX;
    using namespace Engine::Input;

    // ========================================================================
    // マテリアル定数データ構造体
    // ========================================================================
    struct PBRColorData {
        float Color[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
        float Emissive[3] = { 0.0f, 0.0f, 0.0f };
        float Roughness = 0.5f;
        float Metallic = 0.0f;
        float Padding[3] = { 0.0f, 0.0f, 0.0f };
    };

    struct LuminousOrbMaterialCB {
        float CoreColor[4] = { 1.0f, 0.85f, 0.25f, 1.0f };
        float RimColor[4]  = { 0.25f, 0.85f, 1.0f, 0.95f };
        float FresnelPower = 3.5f;
        float PulseSpeed = 2.5f * 3.14159265f;
        float Dispersion = 0.20f;
        float EmissiveIntensity = 2.0f;
    };

    struct PhaseWallMaterialCB {
        float DarkColor[4]     = { 0.08f, 0.02f, 0.15f, 1.0f };
        float LitColor[4]      = { 0.20f, 0.85f, 1.0f, 1.0f };
        // 同時に持ち運べる宝玉は 1 個までで、他はすべて台座に固定される。
        // 最大 8 個ぶんの座標を渡し、シェーダーは OR 合成で反転状態を求める。
        float OrbPositions[8][4] = {
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS }
        };
        float ActiveOrbCount   = 0.0f;
        float DissolveEdge     = 0.12f;
        float PulseSpeed       = 2.5f;
        float EmissiveBoost    = 2.5f;

        // Phase 2: 宝玉遮蔽アトラス (OrbOcclusionPass の出力) の参照情報
        float OrbOcclusionAtlasIndex = 0.0f;
        float OrbOcclusionNearZ      = App::Luminous::ORB_OCCLUSION_NEAR_Z;
        float OrbOcclusionFarZ       = DEFAULT_ORB_RADIUS;
        float OrbOcclusionEnabled    = 0.0f;
    };

    struct PhaseFloorMaterialCB {
        float DarkColor[4]     = { 0.45f, 0.55f, 0.65f, 1.0f };
        float VoidEdgeColor[4] = { 0.15f, 0.80f, 1.0f, 1.0f };
        // 同時に持ち運べる宝玉は 1 個までで、他はすべて台座に固定される。
        // 最大 8 個ぶんの座標を渡し、シェーダーは OR 合成で反転状態を求める。
        float OrbPositions[8][4] = {
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS },
            { 0.0f, -100.0f, 0.0f, DEFAULT_ORB_RADIUS }
        };
        float ActiveOrbCount   = 0.0f;
        float DissolveEdge     = 0.12f;
        float PulseSpeed       = 2.5f;
        float EmissiveBoost    = 2.5f;

        // Phase 2: 宝玉遮蔽アトラス (OrbOcclusionPass の出力) の参照情報
        float OrbOcclusionAtlasIndex = 0.0f;
        float OrbOcclusionNearZ      = App::Luminous::ORB_OCCLUSION_NEAR_Z;
        float OrbOcclusionFarZ       = DEFAULT_ORB_RADIUS;
        float OrbOcclusionEnabled    = 0.0f;
    };

    // ========================================================================
    // マテリアルプレハブ生成ヘルパー
    // ========================================================================
    inline Engine::Core::Prefab CreatePBRMaterialPrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        const XMFLOAT3& color,
        float roughness,
        float metallic,
        const XMFLOAT3& emissive = { 0, 0, 0 },
        bool castShadow = true)
    {
        PBRColorData data;
        data.Color[0] = color.x; data.Color[1] = color.y; data.Color[2] = color.z; data.Color[3] = 1.0f;
        data.Emissive[0] = emissive.x; data.Emissive[1] = emissive.y; data.Emissive[2] = emissive.z;
        data.Roughness = roughness;
        data.Metallic = metallic;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("OneColor"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::GBufferPassTag>();
        if (castShadow) prefab.AddTag<App::Graphics::ShadowPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreatePBRMaterialPrefabTwoSided(
        Engine::Graphics::MaterialManager* materialMgr,
        const XMFLOAT3& color,
        float roughness,
        float metallic,
        const XMFLOAT3& emissive = { 0, 0, 0 })
    {
        PBRColorData data;
        data.Color[0] = color.x; data.Color[1] = color.y; data.Color[2] = color.z; data.Color[3] = 1.0f;
        data.Emissive[0] = emissive.x; data.Emissive[1] = emissive.y; data.Emissive[2] = emissive.z;
        data.Roughness = roughness;
        data.Metallic = metallic;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("OneColorTwoSided"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::GBufferPassTag>();
        prefab.AddTag<App::Graphics::ShadowPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateFlameEmissivePrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        const XMFLOAT3& color,
        float intensity)
    {
        LuminousOrbMaterialCB data;
        data.CoreColor[0] = color.x;
        data.CoreColor[1] = color.y;
        data.CoreColor[2] = color.z;
        data.CoreColor[3] = 0.95f;
        data.RimColor[0]  = std::min(color.x * 1.35f, 1.0f);
        data.RimColor[1]  = std::min(color.y * 1.20f, 1.0f);
        data.RimColor[2]  = std::min(color.z * 1.10f, 1.0f);
        data.RimColor[3]  = 0.90f;
        data.FresnelPower = 1.6f;
        data.EmissiveIntensity = std::clamp(intensity * 0.22f, 2.5f, 6.0f);

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("GuidanceSpark"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::TranslucentPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateTranslucentMaterialPrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        const XMFLOAT4& color,
        const XMFLOAT3& emissive = { 0, 0, 0 })
    {
        PBRColorData data;
        data.Color[0] = color.x; data.Color[1] = color.y; data.Color[2] = color.z; data.Color[3] = color.w;
        data.Emissive[0] = emissive.x; data.Emissive[1] = emissive.y; data.Emissive[2] = emissive.z;
        data.Roughness = 0.5f;
        data.Metallic = 0.0f;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("OneColorTranslucent"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::TranslucentPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateLuminousOrbPrefab(
        Engine::Graphics::MaterialManager* materialMgr)
    {
        LuminousOrbMaterialCB data;
        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("LuminousOrb"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::TranslucentPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateGuidanceSparkPrefab(
        Engine::Graphics::MaterialManager* materialMgr)
    {
        LuminousOrbMaterialCB data;
        data.CoreColor[0] = 1.0f; data.CoreColor[1] = 0.88f; data.CoreColor[2] = 0.35f; data.CoreColor[3] = 0.28f; // 高透明度
        data.RimColor[0]  = 0.30f; data.RimColor[1]  = 0.85f; data.RimColor[2]  = 1.00f; data.RimColor[3] = 0.40f; // 高透明度
        data.FresnelPower = 2.0f;
        data.EmissiveIntensity = 2.0f;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("GuidanceSpark"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::TranslucentPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateChestSparkPrefab(
        Engine::Graphics::MaterialManager* materialMgr)
    {
        LuminousOrbMaterialCB data;
        data.CoreColor[0] = 1.0f; data.CoreColor[1] = 0.90f; data.CoreColor[2] = 0.35f; data.CoreColor[3] = 0.95f;
        data.RimColor[0]  = 1.0f; data.RimColor[1]  = 0.65f; data.RimColor[2]  = 0.15f; data.RimColor[3] = 0.90f;
        data.FresnelPower = 2.0f;
        data.EmissiveIntensity = 2.8f;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("GuidanceSpark"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::TranslucentPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreateStoneCeilingPrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        const XMFLOAT3& color,
        float roughness,
        float metallic)
    {
        PBRColorData data;
        data.Color[0] = color.x; data.Color[1] = color.y; data.Color[2] = color.z; data.Color[3] = 1.0f;
        data.Emissive[0] = 0.0f; data.Emissive[1] = 0.0f; data.Emissive[2] = 0.0f;
        data.Roughness = roughness;
        data.Metallic = metallic;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("StoneCeiling"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::GBufferPassTag>();
        prefab.AddTag<App::Graphics::ShadowPassTag>();
        return prefab;
    }

    inline Engine::Core::Prefab CreatePhaseWallPrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        PhaseWallMaterialCB data,
        const DirectX::XMFLOAT3& baseColor = { 0.159f, 0.205f, 0.337f })
    {
        data.DarkColor[0] = baseColor.x;
        data.DarkColor[1] = baseColor.y;
        data.DarkColor[2] = baseColor.z;
        data.DarkColor[3] = 1.0f;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("PhaseWall"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::GBufferPassTag>();
        // 反転マテリアルは影を落とさない (ShadowPassTag を付与しない)
        return prefab;
    }

    inline Engine::Core::Prefab CreatePhaseFloorPrefab(
        Engine::Graphics::MaterialManager* materialMgr,
        PhaseFloorMaterialCB data,
        const DirectX::XMFLOAT3& baseColor = { 0.159f, 0.205f, 0.337f })
    {
        data.DarkColor[0] = baseColor.x;
        data.DarkColor[1] = baseColor.y;
        data.DarkColor[2] = baseColor.z;
        data.DarkColor[3] = 1.0f;

        Engine::Core::Prefab prefab;
        prefab.AddComponent<Engine::Graphics::MaterialComponent>(Engine::Graphics::MaterialComponent{
            .Handle = materialMgr->GetOrCreateConstant(materialMgr->GetAssetHandle("PhaseFloor"), &data, sizeof(data))
        });
        prefab.AddTag<App::Graphics::GBufferPassTag>();
        // 反転マテリアルは影を落とさない (ShadowPassTag を付与しない)
        return prefab;
    }

    inline std::vector<DirectX::XMFLOAT3> GetAssetSubmeshColors(const std::string& assetId) {
        constexpr DirectX::XMFLOAT3 colGrey       = { 0.159f, 0.205f, 0.337f };
        constexpr DirectX::XMFLOAT3 colDarkGrey   = { 0.095f, 0.117f, 0.188f };
        constexpr DirectX::XMFLOAT3 colLightGrey  = { 0.356f, 0.429f, 0.578f };
        constexpr DirectX::XMFLOAT3 colLightBrown = { 0.381f, 0.133f, 0.032f };
        constexpr DirectX::XMFLOAT3 colBrown      = { 0.188f, 0.033f, 0.036f };
        constexpr DirectX::XMFLOAT3 colDarkBrown  = { 0.084f, 0.019f, 0.018f };
        constexpr DirectX::XMFLOAT3 colBeige      = { 0.965f, 0.839f, 0.462f };
        constexpr DirectX::XMFLOAT3 colBlack      = { 0.020f, 0.020f, 0.020f };
        constexpr DirectX::XMFLOAT3 colRed        = { 0.323f, 0.002f, 0.014f };
        constexpr DirectX::XMFLOAT3 colYellow     = { 1.000f, 0.617f, 0.047f };

        if (assetId == "Floor_1x1" || assetId == "Floor_PatternB" || assetId == "Floor_SawBlade" || assetId == "Floor_Spikes") {
            return { colDarkGrey, colGrey };
        } else if (assetId == "Wall_Brick" || assetId == "Wall_Ruined") {
            return { colDarkGrey, colGrey };
        } else if (assetId == "Wall_Smooth") {
            return { colGrey };
        } else if (assetId == "Wall_Window") {
            return { colDarkGrey, colGrey, colBlack };
        } else if (assetId == "Wall_DoorArc") {
            return { colDarkGrey, colRed };
        } else if (assetId == "Wall_Door") {
            return { colDarkGrey, colBrown, colDarkBrown };
        } else if (assetId == "Wall_Grate" || assetId == "Wall_Railing") {
            return { colBlack };
        } else if (assetId == "GoalChest_Base") {
            return { colLightGrey, colLightBrown, colDarkBrown, colYellow };
        } else if (assetId == "GoalChest_Lid") {
            return { colLightGrey, colLightBrown, colDarkBrown, colBlack };
        } else if (assetId == "GoalChest") {
            return { colLightGrey, colLightBrown, colDarkBrown, colBlack, colYellow };
        } else if (assetId == "Prop_Candle") {
            return { colBlack, colBeige };
        } else if (assetId == "Prop_Torch") {
            return { colDarkGrey, colLightBrown };
        } else if (assetId == "Prop_Skull" || assetId == "Prop_Bone") {
            return { colBeige };
        } else if (assetId == "Prop_WoodenBox") {
            return { colLightBrown };
        } else if (assetId == "Prop_Barrel") {
            return { colLightGrey, colLightBrown };
        } else if (assetId == "Prop_Brick" || assetId == "Prop_BrickPile") {
            return { colGrey };
        }
        return { colGrey };
    }

    inline std::vector<Engine::Core::Prefab> GetAssetSubmeshPrefabs(
        const std::string& assetId,
        Engine::Graphics::MaterialManager* materialMgr)
    {
        // 10 MTL Materials matching Assets/Thumbnails/Dungeon designs
        auto matGrey       = CreatePBRMaterialPrefab(materialMgr, { 0.159f, 0.205f, 0.337f }, 0.80f, 0.05f); // grey.002
        auto matDarkGrey   = CreatePBRMaterialPrefab(materialMgr, { 0.095f, 0.117f, 0.188f }, 0.85f, 0.10f); // dark_grey.002
        auto matLightGrey  = CreatePBRMaterialPrefab(materialMgr, { 0.356f, 0.429f, 0.578f }, 0.70f, 0.20f); // light_grey.002
        auto matLightBrown = CreatePBRMaterialPrefab(materialMgr, { 0.381f, 0.133f, 0.032f }, 0.65f, 0.02f); // light_brown.002
        auto matBrown      = CreatePBRMaterialPrefab(materialMgr, { 0.188f, 0.033f, 0.036f }, 0.70f, 0.05f); // brown.002
        auto matDarkBrown  = CreatePBRMaterialPrefab(materialMgr, { 0.084f, 0.019f, 0.018f }, 0.75f, 0.05f); // dark_brown.002
        auto matBeige      = CreatePBRMaterialPrefab(materialMgr, { 0.965f, 0.839f, 0.462f }, 0.55f, 0.00f); // beige.002
        auto matBlack      = CreatePBRMaterialPrefab(materialMgr, { 0.020f, 0.020f, 0.020f }, 0.60f, 0.30f); // black.002
        auto matRed        = CreatePBRMaterialPrefab(materialMgr, { 0.323f, 0.002f, 0.014f }, 0.70f, 0.02f); // red.002
        auto matYellow     = CreatePBRMaterialPrefab(materialMgr, { 1.000f, 0.617f, 0.047f }, 0.30f, 0.80f, { 0.8f, 0.5f, 0.1f }); // yellow / gold

        if (assetId == "Floor_1x1" || assetId == "Floor_PatternB" || assetId == "Floor_SawBlade" || assetId == "Floor_Spikes") {
            return { matDarkGrey, matGrey };
        } else if (assetId == "GoalChest") {
            return { matLightGrey, matLightBrown, matDarkBrown, matBlack, matYellow };
        } else if (assetId == "GoalChest_Base") {
            return { matLightGrey, matLightBrown, matDarkBrown, matYellow };
        } else if (assetId == "GoalChest_Lid") {
            return { matLightGrey, matLightBrown, matDarkBrown, matBlack };
        } else if (assetId == "Prop_Barrel") {
            return { matLightGrey, matLightBrown }; // 金属タガ + 木樽
        } else if (assetId == "Prop_Torch") {
            return { matDarkGrey, matLightBrown }; // 金属留め具 + 木柄
        } else if (assetId == "Prop_Candle") {
            return { matBlack, matBeige }; // 鉄台座 + 蝋
        } else if (assetId == "Prop_Debris") {
            return { matDarkGrey, matGrey, matLightBrown };
        } else if (assetId == "Wall_Brick" || assetId == "Wall_Ruined") {
            return { matDarkGrey, matGrey }; // 2色レンガ / 崩落壁
        } else if (assetId == "Wall_Door") {
            return { matDarkGrey, matBrown, matDarkBrown };
        } else if (assetId == "Wall_DoorArc") {
            return { matDarkGrey, matRed };
        } else if (assetId == "Wall_Window") {
            return { matDarkGrey, matGrey, matBlack }; // 石枠 + 石壁 + 鉄格子
        } else if (assetId == "Wall_Grate" || assetId == "Wall_Railing") {
            return { matBlack };
        } else if (assetId == "Prop_Bone" || assetId == "Prop_Skull") {
            return { matBeige };
        } else if (assetId == "Prop_WoodenBox") {
            return { matLightBrown };
        } else if (assetId == "Prop_Brick" || assetId == "Prop_BrickPile") {
            return { matGrey };
        } else if (assetId == "LuminousOrb") {
            return { matYellow };
        } else if (assetId == "Pedestal") {
            return { CreatePBRMaterialPrefabTwoSided(materialMgr, { 0.28f, 0.28f, 0.30f }, 0.60f, 0.15f) };
        } else if (assetId == "Stairs_Straight" || assetId == "Prop_Rock") {
            return { matDarkGrey };
        } else {
            // Floor_1x1, Floor_Tile, StartDais, Prop_Pillar, Wall_Smooth, Wall_Railing 等
            return { matGrey };
        }
    }

    // ====================================================================
    // Phase 2: 宝玉光の遮蔽体サブメッシュ判定 (OrbOcclusionPass 用タグ付け)
    // 仕様書 5.2「光は通常壁によってのみ完全に遮蔽される」「格子・柵は光を完全に
    // 透過する」「プレイヤー・台座・小物はいかなる光も遮蔽しない」に準拠。
    // 通常床スラブは多層フロア間の立体光伝播を止める水平遮蔽体として含める。
    // ====================================================================
    inline bool AssetOccludesOrbLight(
        const std::string& assetId,
        AssetCategory category,
        MaterialPhase phase)
    {
        if (phase == MaterialPhase::Phase) return false;                        // 反転マテリアルは光を透過
        if (assetId == "Wall_Grate" || assetId == "Wall_Railing") return false; // 格子・柵は光を透過
        if (assetId.rfind("Prop_", 0) == 0) return false;                     // 小物は光を遮蔽しない
        // 階段も通常の石材として光を遮る (CPU 側 OpticsEngine::GetAssetLocalOccluders と一致)
        return category == AssetCategory::Wall || category == AssetCategory::Floor ||
               category == AssetCategory::Stairs || assetId == "Stairs_Straight";
    }

    // 1オブジェクト内に遮蔽部と透過部が混在する場合の、透過サブメッシュ番号 (無ければ -1)
    inline int AssetLightPassThroughSubmesh(const std::string& assetId)
    {
        // Wall_Window: 0=石枠 / 1=石壁 は遮蔽、2=鉄格子 は光を透過
        if (assetId == "Wall_Window") return 2;
        return -1;
    }

    inline void ApplyOrbOccluderTags(
        const std::string& assetId,
        AssetCategory category,
        MaterialPhase phase,
        std::vector<Engine::Core::Prefab>& prefabs)
    {
        if (!AssetOccludesOrbLight(assetId, category, phase)) return;

        const int passThroughIdx = AssetLightPassThroughSubmesh(assetId);
        for (size_t i = 0; i < prefabs.size(); ++i) {
            if (static_cast<int>(i) == passThroughIdx) continue;
            prefabs[i].AddTag<App::Luminous::OrbOccluderTag>();
        }
    }

    inline std::vector<Engine::Core::Prefab> GetAssetPhasePrefabs(
        const std::string& assetId,
        AssetCategory category,
        Engine::Graphics::MaterialManager* materialMgr,
        const PhaseWallMaterialCB& wallCb,
        const PhaseFloorMaterialCB& floorCb)
    {
        auto colors = GetAssetSubmeshColors(assetId);
        std::vector<Engine::Core::Prefab> prefabs;
        prefabs.reserve(colors.size());
        for (const auto& col : colors) {
            if (category == AssetCategory::Wall) {
                prefabs.push_back(CreatePhaseWallPrefab(materialMgr, wallCb, col));
            } else {
                prefabs.push_back(CreatePhaseFloorPrefab(materialMgr, floorCb, col));
            }
        }
        return prefabs;
    }

    inline void ApplySubmeshPrefabs(
        entt::registry& registry,
        entt::entity targetEntity,
        const std::vector<Engine::Core::Prefab>& prefabs)
    {
        if (!registry.valid(targetEntity)) return;
        if (prefabs.empty()) return;
        if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(targetEntity)) {
            entt::entity sub = rootComp->FirstSubMesh;
            size_t idx = 0;
            while (sub != entt::null && registry.valid(sub)) {
                const auto& p = (idx < prefabs.size()) ? prefabs[idx] : prefabs.back();
                p.ApplyTo(registry, sub);
                idx++;
                if (auto* subComp = registry.try_get<Engine::Graphics::SubMeshComponent>(sub)) {
                    sub = subComp->NextSubMesh;
                } else {
                    break;
                }
            }
        } else {
            prefabs[0].ApplyTo(registry, targetEntity);
        }
    }

        inline void ApplyMaterialToSubMeshes(
        entt::registry& registry,
        entt::entity targetEntity,
        const Engine::Core::Prefab& matPrefab)
    {
        if (!registry.valid(targetEntity)) return;
        if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(targetEntity)) {
            entt::entity sub = rootComp->FirstSubMesh;
            while (sub != entt::null && registry.valid(sub)) {
                matPrefab.ApplyTo(registry, sub);
                if (auto* subComp = registry.try_get<Engine::Graphics::SubMeshComponent>(sub)) {
                    sub = subComp->NextSubMesh;
                } else {
                    break;
                }
            }
        } else {
            matPrefab.ApplyTo(registry, targetEntity);
        }
    }


    // 細い枠線 (選択中のカードの強調など)
    inline void DrawUIFrame(float x, float y, float w, float h, float t, const XMFLOAT4& color) {
        auto& ui = Engine::Graphics::UIRenderer::Get();
        ui.DrawPanel(x, y, w, t, color);
        ui.DrawPanel(x, y + h - t, w, t, color);
        ui.DrawPanel(x, y, t, h, color);
        ui.DrawPanel(x + w - t, y, t, h, color);
    }

    // 操作説明 1 項目 (操作中デバイスのボタン画像 + 文字) を描き、占有した幅を返す
    inline float DrawControlHint(float x, float y, LuminousButtonGlyph glyph, const std::string& label, float height = 40.0f) {
        auto& ui = Engine::Graphics::UIRenderer::Get();
        const float gw = height * LuminousInputDevice::GlyphAspect(glyph);
        ui.DrawImage(LuminousInputDevice::GlyphPath(glyph), x, y, gw, height);
        const float textSize = height * 0.48f;
        ui.DrawString(label, x + gw + 10.0f, y + (height - textSize) * 0.5f, textSize, { 0.86f, 0.96f, 0.99f, 0.95f });
        return gw + 10.0f + static_cast<float>(label.size()) * textSize * 0.56f + 36.0f;
    }

    inline float MeasureControlHint(LuminousButtonGlyph glyph, const std::string& label, float height = 40.0f) {
        const float textSize = height * 0.48f;
        return height * LuminousInputDevice::GlyphAspect(glyph) + 10.0f + static_cast<float>(label.size()) * textSize * 0.56f + 36.0f;
    }

    // ========================================================================
    // 前方宣言
    // ========================================================================
    class LuminousTitleScene;
    class LuminousPlayScene;
    class LuminousEditorScene;

    // ========================================================================
    // LuminousTitleScene (タイトルシーン - 完全ImGuiフリー＆360度パノラマ回転背景)
    // ========================================================================
    // タイトル画面の処理 (レジストリだけを見る自由関数)
    void LuminousTitleRenderMenu(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick);
    void LuminousTitleRenderStageSelect(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick);
    void LuminousTitleRenderCustomStageList(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick);
    void LuminousTitleUpdate(const Engine::Core::SystemContext& ctx);

    class LuminousTitleScene : public Engine::Core::IScene {
    public:
        // 実体は App.Luminous.TitleRuntime にある。
        //   これまでどおり LuminousTitleScene::TitleView と書けるように残す
        using TitleView = LuminousTitleView;

    private:
        // 持ち物は **レジストリ** にある (LuminousTitleRuntime)。
        //   取り出すのは TitleRuntime(registry)。
        //   pendingView_ は、作るときに渡された初期画面の置き場
        //   (レジストリはまだ無いので、OnSetup で移す)
        TitleView pendingView_ = TitleView::MainMenu;

    public:
        LuminousTitleScene() = default;
        explicit LuminousTitleScene(TitleView initialView) : pendingView_(initialView) {}

        // ゲームシーンは100% ImGui非使用 (完全画像UI化)
        bool IsImGuiEnabled() const override { return false; }

        void OnSetup(Engine::Core::SceneContext& ctx) override;
        void OnTeardown(Engine::Core::SceneContext& ctx) override;
    };

    // ========================================================================
    // LuminousPlayScene (FPSゲーム本編シーン - 完全ImGuiフリー画像UI＆空間音響)
    // ========================================================================
    // ========================================================================
    // 本編の処理 (シーンの持ち物ではなく、**レジストリだけを見る自由関数**)
    //
    //   `this` を捕まえていないので、SystemRegistry に名前で登録できる。
    //   そうすると .scene.json の "systems" に名前で並べるだけで回せる
    //   (シーンの C++ の実体が無くてもよい)。
    // ========================================================================

    // プレイヤーの状態を、載っているエンティティから取り出す。
    //   **ここが唯一の置き場**。写しを持つと、エディタで直した値が次のフレームで消える
    inline LuminousPlayerComponent& PlayerState(entt::registry& registry) {
        return registry.get<LuminousPlayerComponent>(PlayRuntime(registry).PlayerCamera);
    }

    inline LuminousDebugContext LuminousPlayDebugContext(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        return LuminousDebugContext{ registry, rt.Stage, PlayerState(registry), rt.Orbs,
                                     rt.OcclusionWalls, rt.BakedPedestals,
                                     rt.PlayerCamera, rt.GameTime, rt.GuidanceStream.Count() };
    }

    // ステージの中身を、エンティティとして建てる。
    //   **SceneContext ではなく registry を取る**。名前つきのシステムからも
    //   同じものを呼べるようにするため
    void LuminousPlaySpawnStageObjects(entt::registry& registry);

    // シーンに属するエンティティを 1 つ作る。
    //   SceneContext / SystemContext の CreateEntity と同じ扱い (シーン遷移で消える)
    inline entt::entity CreateSceneEntity(entt::registry& registry) {
        const entt::entity entity = registry.create();
        registry.emplace<Engine::Core::SceneScopeTag>(entity);
        return entity;
    }

    // ステージや設定から**組み立てた**エンティティを 1 つ作る。
    //   GeneratedTag が付くので .scene.json には書き出されない。
    //   同じものを毎回ここから作り直せる、というのが付ける条件
    inline entt::entity CreateGeneratedEntity(entt::registry& registry) {
        const entt::entity entity = CreateSceneEntity(registry);
        registry.emplace<Engine::Core::GeneratedTag>(entity);
        return entity;
    }
    void LuminousPlaySyncPhaseVisuals(Engine::Graphics::MaterialManager* materialMgr,
                                      entt::registry& registry);
    void LuminousPlaySubmitParticles(entt::registry& registry);
    void LuminousPlayRenderHUD(const Engine::Core::SystemContext& ctx);

    // ------------------------------------------------------------------------
    // ステージ (PlacedObject) から、遊びに要るものを組み立てる。
    //   宝玉・遮蔽壁・当たり判定の格子・台座の焼き込み・演出の入れもの。
    //   **保存しない派生データ**なので、いつでもここから作り直せる
    // ------------------------------------------------------------------------
    inline void LuminousPlayBuildRuntime(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        // プレイヤーがまだ居ないうちは組み立てられない。
        //   データだけのシーンでは、この順番が読み込みの都合で前後しうる
        if (rt.PlayerCamera == entt::null || !registry.valid(rt.PlayerCamera)) return;
        auto& player = PlayerState(registry);
        // 5. プレイヤー初期位置の検出
        for (const auto& obj : rt.Stage.Objects) {
            if (obj.AssetId == "StartDais") {
                player.Position = XMFLOAT3(obj.Position.x, obj.Position.y + 0.1f, obj.Position.z);
                player.Yaw = XMConvertToRadians(SpawnFacingToYawDegrees(obj.Facing));
                player.Pitch = 0.0f;
                break;
            }
        }

        // 6. 宝玉ランタイム状態の初期化
        rt.Orbs.clear();
        for (const auto& obj : rt.Stage.Objects) {
            if (obj.Category == AssetCategory::Pedestal && obj.HasInitialOrb) {
                OrbRuntimeState orbState;
                orbState.OrbId = obj.InstanceId;
                orbState.Carrier = OrbCarrier::Pedestal;
                orbState.PedestalInstanceId = obj.InstanceId;
                orbState.WorldPosition = XMFLOAT3(obj.Position.x, obj.Position.y + PEDESTAL_SOCKET_HEIGHT, obj.Position.z);
                orbState.Radius = g_LuminousConfig.OrbInfluenceRadius;
                rt.Orbs.push_back(orbState);
            }
        }

        // 7. 遮蔽壁リストおよび空間グリッドの初期化
        rt.OcclusionWalls = OpticsEngine::BuildOcclusionWalls(rt.Stage.Objects);
        rt.Grid.Build(rt.OcclusionWalls);
        static uint32_t s_GeometryRevisionCounter = 0;
        rt.GeometryRevision = ++s_GeometryRevisionCounter;

        rt.BakedPedestals = TerrainSystem::BakePedestalColliders(rt.Stage.Objects);

        // 8. パーティクル (すべて丸スプライトのビルボードとして ParticleBillboardPass で描画)
        rt.GuidanceStream.Reset();
        App::Graphics::ParticleDrawList::Get(registry).Clear();

        // 9. 黄金の光粒子噴水バースト (300粒子)
        rt.ChestAnimTimer = 0.0f;
        rt.ChestBurstTriggered = false;
        rt.ChestBurstParticles.clear();
        rt.ChestBurstParticles.resize(300);
    }

    // ------------------------------------------------------------------------
    // 遊ぶのに欠かせないものを揃える (プレイヤーのカメラ・環境光・手持ちの宝玉)。
    //
    //   **もう在れば何もしない**。C++ のシーンから来ても、.scene.json から
    //   起こしても、同じここを通る。
    //   ステージの中身とは別扱いにしてある。ステージは差し替わるが、
    //   この 3 つはどのステージでも要るため
    // ------------------------------------------------------------------------
    inline void LuminousPlayEnsureCoreEntities(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        if (rt.PlayerCamera != entt::null && registry.valid(rt.PlayerCamera)) return;

        // 暗転した状態で始め、マテリアルのコンパイルが済んでから明転する
        LuminousTransition::Get().BeginSceneEnter();
        Input::SetCursorLocked(true);

        // 保存されたシーンから来たときは、ファイルに載っているものを拾う
        const auto players = registry.view<LuminousPlayerComponent>();
        if (!players.empty()) {
            rt.PlayerCamera = players.front();
            Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.PlayerCamera);
            return;
        }

        auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        auto* meshMgr = Engine::Core::GetService<Engine::Graphics::MeshManager>(registry);

        // プレイヤーエンティティと FPS カメラ。
        //   **組み立てた印 (GeneratedTag) を付ける**。開始位置はステージの
        //   StartDais から決まる派生物なので、シーンのファイルには残さない
        rt.PlayerCamera = CreateGeneratedEntity(registry);
        // プレイヤーの状態をここに載せる。これでインスペクターから見えるようになる
        registry.emplace<LuminousPlayerComponent>(rt.PlayerCamera);
        auto& player = PlayerState(registry);
        auto& camTransform = registry.emplace<Engine::Core::TransformComponent>(rt.PlayerCamera);
        camTransform.LocalPosition = { player.Position.x, player.Position.y + player.EyeHeight, player.Position.z };
        XMStoreFloat4(&camTransform.LocalRotation, XMQuaternionIdentity());

        registry.emplace<Engine::Graphics::CameraComponent>(rt.PlayerCamera, Engine::Graphics::CameraComponent{
            .FovY = XMConvertToRadians(75.0f),
            .AspectRatio = 1920.0f / 1080.0f,
            .NearZ = 0.05f,
            .FarZ = 300.0f
        });
        Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.PlayerCamera);

        // ダンジョン環境光 (Ambient Light)
        auto ambientEnt = CreateGeneratedEntity(registry);
        auto& ambTrans = registry.emplace<Engine::Core::TransformComponent>(ambientEnt);
        ambTrans.LocalPosition = { 10.0f, 30.0f, 10.0f };
        XMStoreFloat4(&ambTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(70.0f), XMConvertToRadians(30.0f), 0.0f));
        registry.emplace<Engine::Graphics::DirectionalLightComponent>(ambientEnt, Engine::Graphics::DirectionalLightComponent{
            .Color = { 0.85f, 0.90f, 1.0f },
            .Intensity = g_LuminousConfig.AmbientLightIntensity,
            .CastShadows = false
        });

        // 手持ち宝玉用エンティティとポイントライト
        auto orbMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");
        auto orbMat  = CreateLuminousOrbPrefab(materialMgr);

        rt.HeldOrbMesh = CreateGeneratedEntity(registry);
        registry.emplace<Engine::Core::TransformComponent>(rt.HeldOrbMesh, Engine::Core::TransformComponent{
            .LocalPosition = { 0.0f, -100.0f, 0.0f },
            .LocalScale = { 1.0f, 1.0f, 1.0f }
        });
        Engine::Graphics::MeshAttacher::AttachToEntity(registry, rt.HeldOrbMesh, orbMesh, orbMat);

        rt.HeldOrbLight = CreateGeneratedEntity(registry);
        registry.emplace<Engine::Core::TransformComponent>(rt.HeldOrbLight, Engine::Core::TransformComponent{
            .LocalPosition = { 0.0f, -100.0f, 0.0f }
        });
        registry.emplace<Engine::Graphics::PointLightComponent>(rt.HeldOrbLight, Engine::Graphics::PointLightComponent{
            .Color = { 1.0f, 0.88f, 0.28f },
            .Intensity = 0.0f,
            .Radius = g_LuminousConfig.OrbInfluenceRadius,
            .CastShadows = true,
            .ShadowNearZ = g_LuminousConfig.OrbLightNearClip
        });
    }

    // ------------------------------------------------------------------------
    // シーンのファイルが置いた「そのシーンの設定」を取る (計画 18 の段 2)。
    //
    //   コードのシーンが**作るときの引数**で変種を分けていたものの行き先。
    //   置いてなければ既定値 (= 引数なしで作ったときと同じ) を返すので、
    //   設定を書いていない古いシーンのファイルもそのまま動く
    // ------------------------------------------------------------------------
    template <typename T>
    [[nodiscard]] inline T SceneSetting(entt::registry& registry) {
        const auto view = registry.view<T>();
        if (view.empty()) return T{};
        return registry.get<T>(view.front());
    }

    // ------------------------------------------------------------------------
    // ステージの資産から世界を建てる (名前つきのシステム)。
    //
    //   .scene.json の "systems" に並べておけば、シーンの C++ が無くても建つ。
    //   どのステージを建てるかは 2 段で決まる。
    //     1. シーンをまたいで残る LuminousSession (ステージ選択から来たとき)
    //     2. シーンのファイルが置いた LuminousPlaySettings (直に開いたとき)
    //   どちらも空なら基本ステージ 1。
    //   建てたものには GeneratedTag が付くので、シーンのファイルには残らない
    // ------------------------------------------------------------------------
    inline void LuminousStageBuildSystem(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        auto& rt = PlayRuntime(registry);
        if (!rt.Stage.Objects.empty()) return;   // もう建っている

        // 1. 遊ぶ人が選んだもの (ステージ選択・エディタのテストプレイから来たとき)
        const LuminousSession& session = Session(registry);
        // 2. シーンのファイルが言っているもの (そのシーンを直に開いたとき)
        const LuminousPlaySettings settings = SceneSetting<LuminousPlaySettings>(registry);

        // どれを使うかは**細かく言った側が勝つ**。
        //   遊ぶ人の選択 > 起動オプション > シーンのファイルの既定 > 基本ステージ 1
        std::string path = session.StagePath;
        int stageNumber = session.StageNumber;
        bool returnToEditor = session.ReturnToEditor;

        // 2. 開発用の起動オプション --base-stage=N。
        //    **シーンを作るときの引数ではなく、ここで読む**。
        //    そうするとシーンのファイルは 1 つで済み、番号だけが外から変わる
        if (path.empty()) {
            if (const auto option = Engine::Common::CommandLine::Get().Value("base-stage")) {
                const int number = std::atoi(option->c_str());
                if (const auto* entry = LuminousStageCatalog::GetByStageNumber(number)) {
                    path = entry->StageFilePath;
                    stageNumber = number;
                }
            }
        }
        // 3. シーンのファイルが言っている既定
        if (path.empty()) {
            path = settings.StagePath;
            stageNumber = settings.StageNumber;
            returnToEditor = settings.ReturnToEditor;
        }
        // 4. どれも無ければ基本ステージ 1
        if (path.empty()) {
            path = "Assets/Data/BaseStages/stage_01.json";
            stageNumber = 1;
        }

        std::string error;
        // パスはプロジェクトからの相対で書く。実際の場所は Paths が決める
        if (!rt.Stage.LoadFromFile(Engine::Common::Paths::ResolveString(path), error)) {
            ENGINE_LOG_ERROR("Luminous", "ステージを読めませんでした '{}': {}", path, error);
            return;
        }
        rt.StageNumber = stageNumber;
        rt.ReturnToEditorOnExit = returnToEditor;

        LuminousPlayEnsureCoreEntities(registry);
        LuminousPlaySpawnStageObjects(registry);
        LuminousPlayBuildRuntime(registry);
        ENGINE_LOG_INFO("Luminous", "ステージを建てました '{}' ({} 個)",
                        path, static_cast<int>(rt.Stage.Objects.size()));
    }

    // ------------------------------------------------------------------------
    // .scene.json から起動したときの組み直し。
    //
    //   データだけのシーンには C++ のシーンが無いので、持ち物が空のまま。
    //   権威データは**エンティティに載っている PlacedObject と
    //   LuminousPlayerComponent** なので、そこから戻す。
    //   何もしなくてよいとき (C++ のシーンから来たとき) は、すぐ帰る
    // ------------------------------------------------------------------------
    inline void LuminousPlayRestoreFromEntities(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        if (!rt.Stage.Objects.empty()) return;   // もう組んである

        // プレイヤーは、載っているエンティティを探す
        if (rt.PlayerCamera == entt::null || !registry.valid(rt.PlayerCamera)) {
            const auto players = registry.view<LuminousPlayerComponent>();
            if (players.empty()) return;   // まだ読み込み途中
            rt.PlayerCamera = players.front();
        }

        // ステージの中身と、どのエンティティが何なのかを集め直す。
        //   **番号の順に並べる**。エンティティを見る順は決まっていないので、
        //   そのままだと起動のたびに開始地点などが変わりかねない
        rt.ObjectEntities.clear();
        for (auto [entity, placed] : registry.view<PlacedObject>().each()) {
            rt.Stage.Objects.push_back(placed);
            rt.ObjectEntities[placed.InstanceId] = entity;
        }
        std::sort(rt.Stage.Objects.begin(), rt.Stage.Objects.end(),
                  [](const PlacedObject& a, const PlacedObject& b) {
                      return a.InstanceId < b.InstanceId;
                  });
        if (rt.Stage.Objects.empty()) return;

        LuminousPlayBuildRuntime(registry);

        // **見る目を決める**。C++ のシーンなら OnSetup がやっていたところ。
        //   これが抜けていると、組み上がっていても真っ暗なままになる
        Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.PlayerCamera);
        Input::SetCursorLocked(true);
    }
    void LuminousPlayUpdate(const Engine::Core::SystemContext& ctx);

    // タイトルとエディタの更新も、名前で登録する (定義はこのあと)
    void LuminousTitleUpdate(const Engine::Core::SystemContext& ctx);
    void LuminousEditorUpdate(const Engine::Core::SystemContext& ctx);
    void LuminousEditorBuildSystem(const Engine::Core::SystemContext& ctx);

    // ゲームの立ち上げで 1 度だけ呼ぶ。
    //   ここで名前を通しておくと、.scene.json の "systems" から引けるようになる
    inline void RegisterLuminousSystems() {
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousStageBuildSystem", Engine::Core::SystemPhase::Update, &LuminousStageBuildSystem);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousPlayUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousPlayUpdate);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousTitleUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousTitleUpdate);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousEditorBuildSystem", Engine::Core::SystemPhase::Update, &LuminousEditorBuildSystem);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousEditorUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousEditorUpdate);
    }

    class LuminousPlayScene : public Engine::Core::IScene {
    private:
        // 本編の持ち物は **レジストリ** にある (LuminousPlayRuntime)。
        //   取り出すのは PlayRuntime(registry)。
        //   ここに置いてあった頃は、更新処理を名前つきのシステムに
        //   分けられなかった (`this` を捕まえたラムダでしか書けないため)。
        //   pendingStage_ は、シーンを作るときに渡されたステージの置き場。
        //   レジストリはまだ無いので、OnSetup で実行時データへ移す
        LuminousStage pendingStage_;
        bool pendingReturnToEditor_ = false;
        int pendingStageNumber_ = 0;
        entt::entity runtimeEntity_ = entt::null;
        // IsImGuiEnabled はエンジンが registry 抜きで呼ぶので、ここで覚えておく
        entt::registry* world_ = nullptr;
        // プレイヤーの状態は **エンティティに載っている** (rt.PlayerCamera)。
        //   シーンが抱えていた頃は、インスペクターにも出ず、保存もされなかった。
        //   取り出すのは PlayerState(registry)

    public:
        // ゲーム本編は画像 UI のみ。F12 デバッグツール使用中だけ ImGui を有効にする
        bool IsImGuiEnabled() const override {
            return world_ ? PlayRuntime(*world_).DebugTool.IsActive() : false;
        }

        LuminousPlayScene() {
            pendingStage_ = LuminousStage::CreateSampleStage();
        }

        explicit LuminousPlayScene(LuminousStage stage, bool returnToEditor = false, int stageNumber = 0)
            : pendingStage_(std::move(stage)), pendingReturnToEditor_(returnToEditor),
              pendingStageNumber_(stageNumber) {}

        void OnSetup(Engine::Core::SceneContext& ctx) override;
        void OnTeardown(Engine::Core::SceneContext& ctx) override;
    };

    // ========================================================================
    // LuminousEditorScene (統合ステージエディタシーン - エディタのみImGui使用)
    // ========================================================================
    // ステージエディタの処理 (レジストリだけを見る自由関数)
    void LuminousEditorRefreshSceneEntities(entt::registry& registry);
    // 編集に欠かせないもの (カメラ・環境光・下見のゴースト) を揃える
    void LuminousEditorEnsureCoreEntities(entt::registry& registry);

    void LuminousEditorRenderUI(const Engine::Core::SystemContext& ctx);
    void LuminousEditorUpdate(const Engine::Core::SystemContext& ctx);

    class LuminousEditorScene : public Engine::Core::IScene {
    private:
        // 持ち物は **レジストリ** にある (LuminousEditorRuntime)。
        //   取り出すのは EditorRuntime(registry)。
        //   pending* は、作るときに渡されたものの置き場
        //   (レジストリはまだ無いので、OnSetup で移す)
        LuminousStage pendingStage_;
        bool hasPendingStage_ = false;
        int pendingCategory_ = 0;

    public:
        // エディタシーンのみImGuiを許可 (Shipping構成でも動作)
        bool IsImGuiEnabled() const override { return true; }

        LuminousEditorScene() = default;
        explicit LuminousEditorScene(int initialCategory) : pendingCategory_(initialCategory) {}
        explicit LuminousEditorScene(LuminousStage stage)
            : pendingStage_(std::move(stage)), hasPendingStage_(true) {}
        LuminousEditorScene(LuminousStage stage, int initialCategory)
            : pendingStage_(std::move(stage)), hasPendingStage_(true),
              pendingCategory_(initialCategory) {}

        void OnSetup(Engine::Core::SceneContext& ctx) override;
        void OnTeardown(Engine::Core::SceneContext& ctx) override;
    };

    // ========================================================================
    // LuminousTitleScene 実装
    // ========================================================================
    inline void LuminousTitleScene::OnSetup(Engine::Core::SceneContext& ctx) {
        // 持ち物をレジストリに作る (シーンが終わるときに一緒に片付く)
        const entt::entity titleRuntimeEnt = ctx.CreateEntity();
        ctx.GetRegistry().emplace<Engine::Core::GeneratedTag>(titleRuntimeEnt);
        auto& rt = ctx.GetRegistry().emplace<LuminousTitleRuntime>(titleRuntimeEnt);
        rt.View = pendingView_;
        // 暗転した状態でシーンを開始し、マテリアルのコンパイル完了後に明転する
        LuminousTransition::Get().BeginSceneEnter();
        Input::SetCursorLocked(false);
        auto& registry = ctx.GetRegistry();
        auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        auto* meshMgr = Engine::Core::GetService<Engine::Graphics::MeshManager>(registry);

        rt.RoomEntities.clear();





        auto floorPrefabs      = GetAssetSubmeshPrefabs("Floor_1x1", materialMgr);
        auto wallPrefabs       = GetAssetSubmeshPrefabs("Wall_Brick", materialMgr);
        auto smoothWallPrefabs = GetAssetSubmeshPrefabs("Wall_Smooth", materialMgr);
        auto pedPrefabs        = GetAssetSubmeshPrefabs("Pedestal", materialMgr);
        auto pillarPrefabs     = GetAssetSubmeshPrefabs("Prop_Pillar", materialMgr);
        auto torchPrefabs      = GetAssetSubmeshPrefabs("Prop_Torch", materialMgr);
        auto woodenBoxPrefabs  = GetAssetSubmeshPrefabs("Prop_WoodenBox", materialMgr);
        auto barrelPrefabs     = GetAssetSubmeshPrefabs("Prop_Barrel", materialMgr);
        auto orbMat            = CreateLuminousOrbPrefab(materialMgr);

        auto floorMesh      = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Floor_1x1.obj");
        auto wallMesh       = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Wall_Brick.obj");
        auto smoothWallMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Wall_Smooth.obj");
        auto pedMesh        = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Pedestal.obj");
        auto pillarMesh     = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Prop_Pillar.obj");
        auto torchMesh      = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Prop_Torch.obj");
        auto boxMesh        = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Prop_WoodenBox.obj");
        auto barrelMesh     = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/Prop_Barrel.obj");
        auto orbMesh        = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");

        // 5x5 床面 & 天井 (Y = GRID_FLOOR_HEIGHT = 3.063m)
        for (int x = -2; x <= 2; ++x) {
            for (int z = -2; z <= 2; ++z) {
                // 床 (Y = 0)
                auto fEnt = ctx.CreateEntity();
                registry.emplace<Engine::Core::TransformComponent>(fEnt, Engine::Core::TransformComponent{
                    .LocalPosition = { static_cast<float>(x) * GRID_CELL_SIZE, 0.0f, static_cast<float>(z) * GRID_CELL_SIZE }
                });
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, fEnt, floorMesh, floorPrefabs);
                rt.RoomEntities.push_back(fEnt);

                // 天井 (Y = 3.063m, 下向きに反転)
                auto cEnt = ctx.CreateEntity();
                auto& cTrans = registry.emplace<Engine::Core::TransformComponent>(cEnt);
                cTrans.LocalPosition = { static_cast<float>(x) * GRID_CELL_SIZE, GRID_FLOOR_HEIGHT, static_cast<float>(z) * GRID_CELL_SIZE };
                XMStoreFloat4(&cTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(180.0f), 0.0f, 0.0f));
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, cEnt, floorMesh, floorPrefabs);
                rt.RoomEntities.push_back(cEnt);
            }
        }

        // 周囲4面の外壁 (North, South, East, West)
        for (int i = -2; i <= 2; ++i) {
            float coord = static_cast<float>(i) * GRID_CELL_SIZE;
            float bound = 2.5f * GRID_CELL_SIZE;

            // North Wall
            auto wN = ctx.CreateEntity();
            registry.emplace<Engine::Core::TransformComponent>(wN, Engine::Core::TransformComponent{
                .LocalPosition = { coord, 0.0f, bound }
            });
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, wN, (i % 2 == 0) ? wallMesh : smoothWallMesh, (i % 2 == 0) ? wallPrefabs : smoothWallPrefabs);
            rt.RoomEntities.push_back(wN);

            // South Wall
            auto wS = ctx.CreateEntity();
            auto& tS = registry.emplace<Engine::Core::TransformComponent>(wS);
            tS.LocalPosition = { coord, 0.0f, -bound };
            XMStoreFloat4(&tS.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(180.0f), 0.0f));
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, wS, (i % 2 == 0) ? wallMesh : smoothWallMesh, (i % 2 == 0) ? wallPrefabs : smoothWallPrefabs);
            rt.RoomEntities.push_back(wS);

            // East Wall
            auto wE = ctx.CreateEntity();
            auto& tE = registry.emplace<Engine::Core::TransformComponent>(wE);
            tE.LocalPosition = { bound, 0.0f, coord };
            XMStoreFloat4(&tE.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(90.0f), 0.0f));
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, wE, (i % 2 == 0) ? smoothWallMesh : wallMesh, (i % 2 == 0) ? smoothWallPrefabs : wallPrefabs);
            rt.RoomEntities.push_back(wE);

            // West Wall
            auto wW = ctx.CreateEntity();
            auto& tW = registry.emplace<Engine::Core::TransformComponent>(wW);
            tW.LocalPosition = { -bound, 0.0f, coord };
            XMStoreFloat4(&tW.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(270.0f), 0.0f));
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, wW, (i % 2 == 0) ? smoothWallMesh : wallMesh, (i % 2 == 0) ? smoothWallPrefabs : wallPrefabs);
            rt.RoomEntities.push_back(wW);
        }

        // 四隅の装飾石柱 (Prop_Pillar)
        float cornerOffsets[4][2] = { { -4.2f, -4.2f }, { 4.2f, -4.2f }, { -4.2f, 4.2f }, { 4.2f, 4.2f } };
        for (int c = 0; c < 4; ++c) {
            auto cEnt = ctx.CreateEntity();
            registry.emplace<Engine::Core::TransformComponent>(cEnt, Engine::Core::TransformComponent{
                .LocalPosition = { cornerOffsets[c][0], 0.0f, cornerOffsets[c][1] }
            });
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, cEnt, pillarMesh, pillarPrefabs);
            rt.RoomEntities.push_back(cEnt);
        }

        // 4壁面の松明と暖色炎ライト (部屋の内側を向く正規Yaw角: North=0, South=180, East=90, West=270)
        float wallTorchDist = 2.5f * GRID_CELL_SIZE - 0.22f;
        struct WallTorchDef { XMFLOAT3 pos; float yaw; };
        WallTorchDef torches[4] = {
            { { 0.0f, 1.8f, wallTorchDist }, 0.0f },    // North壁: 室内(南)を向く
            { { 0.0f, 1.8f, -wallTorchDist }, 180.0f }, // South壁: 室内(北)を向く
            { { wallTorchDist, 1.8f, 0.0f }, 90.0f },   // East壁:  室内(西)を向く
            { { -wallTorchDist, 1.8f, 0.0f }, 270.0f }  // West壁:  室内(東)を向く
        };
        auto flameMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");
        auto flameMat  = CreateFlameEmissivePrefab(materialMgr, { 1.0f, 0.62f, 0.22f }, 20.0f);

        for (const auto& tDef : torches) {
            auto tEnt = ctx.CreateEntity();
            auto& tTrans = registry.emplace<Engine::Core::TransformComponent>(tEnt);
            tTrans.LocalPosition = tDef.pos;
            XMStoreFloat4(&tTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(tDef.yaw), 0.0f));
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, tEnt, torchMesh, torchPrefabs);
            rt.RoomEntities.push_back(tEnt);

            float radY = XMConvertToRadians(tDef.yaw);
            constexpr float tipOffsetY = 0.5512f;
            constexpr float tipOffsetZ = -0.1101f;
            XMFLOAT3 flamePos = {
                tDef.pos.x + tipOffsetZ * std::sin(radY),
                tDef.pos.y + tipOffsetY,
                tDef.pos.z + tipOffsetZ * std::cos(radY)
            };

            // 松明の炎の明かり
            auto lEnt = ctx.CreateEntity();
            registry.emplace<Engine::Core::TransformComponent>(lEnt, Engine::Core::TransformComponent{
                .LocalPosition = flamePos
            });
            registry.emplace<Engine::Graphics::PointLightComponent>(lEnt, Engine::Graphics::PointLightComponent{
                .Color = { 1.0f, 0.62f, 0.22f },
                .Intensity = 20.0f,
                .Radius = 10.0f,
                .CastShadows = true,
                .ShadowNearZ = g_LuminousConfig.PropLightNearClip
            });
            rt.RoomEntities.push_back(lEnt);

            // 炎エミッシブオブジェクト (3倍サイズ)
            auto fEnt = ctx.CreateEntity();
            registry.emplace<Engine::Core::TransformComponent>(fEnt, Engine::Core::TransformComponent{
                .LocalPosition = flamePos,
                .LocalScale = { 0.45f, 0.72f, 0.45f }
            });
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, fEnt, flameMesh, flameMat);
            // 火の粉を組み直す種を**そのエンティティに載せる**。
            //   これで .scene.json から起こしても同じ火の粉が出る
            registry.emplace<LuminousFlameEmitterComponent>(fEnt,
                LuminousFlameEmitterComponent{ .Count = 8, .Radius = 0.115f, .BaseScale = 0.082f });
            rt.RoomEntities.push_back(fEnt);

            // 火の粉パーティクル (各ランプ8個、上昇・螺旋拡散。丸スプライトのビルボードで描画)
            for (int pIdx = 0; pIdx < 8; ++pIdx) {
                constexpr float pScale = 0.082f;
                LuminousTitleRuntime::TitleFlameParticle fp;
                fp.CurrentPosition = flamePos;
                fp.Origin = flamePos;
                fp.Phase = static_cast<float>(pIdx) * (XM_2PI / 8.0f);
                fp.Speed = 0.14f + static_cast<float>(pIdx % 4) * 0.035f;
                fp.Radius = 0.115f;
                fp.BaseScale = pScale;
                rt.FlameParticles.push_back(fp);
            }
        }

        // ダンジョン小道具 (木箱・木樽)
        struct PropDef { const char* meshKey; XMFLOAT3 pos; float yaw; };
        PropDef props[] = {
            { "box", { -3.6f, 0.0f, -3.7f }, 15.0f },
            { "barrel", { -3.8f, 0.0f, -3.0f }, 45.0f },
            { "barrel", { 3.7f, 0.0f, 3.5f }, -30.0f },
            { "box", { 3.4f, 0.0f, -3.7f }, 70.0f },
            { "barrel", { 3.8f, 0.0f, -3.1f }, 10.0f }
        };
        for (const auto& p : props) {
            auto pEnt = ctx.CreateEntity();
            auto& pTrans = registry.emplace<Engine::Core::TransformComponent>(pEnt);
            pTrans.LocalPosition = p.pos;
            XMStoreFloat4(&pTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(p.yaw), 0.0f));
            Engine::Graphics::MeshAttacher::AttachToEntity(registry, pEnt, (p.meshKey == "box") ? boxMesh : barrelMesh, (p.meshKey == "box") ? woodenBoxPrefabs : barrelPrefabs);
            rt.RoomEntities.push_back(pEnt);
        }

        // 中央の台座
        auto pedEnt = ctx.CreateEntity();
        registry.emplace<Engine::Core::TransformComponent>(pedEnt, Engine::Core::TransformComponent{
            .LocalPosition = { 0.0f, 0.0f, 0.0f }
        });
        Engine::Graphics::MeshAttacher::AttachToEntity(registry, pedEnt, pedMesh, pedPrefabs);
        rt.RoomEntities.push_back(pedEnt);

        // 台座上の浮遊宝玉
        rt.OrbMesh = ctx.CreateEntity();
        registry.emplace<Engine::Core::TransformComponent>(rt.OrbMesh, Engine::Core::TransformComponent{
            .LocalPosition = { 0.0f, PEDESTAL_SOCKET_HEIGHT - 0.02f, 0.0f },
            .LocalScale = { 1.0f, 1.0f, 1.0f }
        });
        Engine::Graphics::MeshAttacher::AttachToEntity(registry, rt.OrbMesh, orbMesh, orbMat);
        registry.emplace<LuminousTitleOrbTag>(rt.OrbMesh);
        rt.RoomEntities.push_back(rt.OrbMesh);

        // 宝玉の温かな黄金ポイントライト
        rt.OrbLight = ctx.CreateEntity();
        registry.emplace<Engine::Core::TransformComponent>(rt.OrbLight, Engine::Core::TransformComponent{
            .LocalPosition = { 0.0f, PEDESTAL_SOCKET_HEIGHT - 0.02f, 0.0f }
        });
        registry.emplace<Engine::Graphics::PointLightComponent>(rt.OrbLight, Engine::Graphics::PointLightComponent{
            .Color = { 1.0f, 0.88f, 0.35f },
            .Intensity = 42.0f,
            .Radius = 16.0f,
            .CastShadows = true
        });
        rt.RoomEntities.push_back(rt.OrbLight);

        // 部屋隅の青色神秘アンビエントライト
        auto blueLightEnt = ctx.CreateEntity();
        registry.emplace<Engine::Core::TransformComponent>(blueLightEnt, Engine::Core::TransformComponent{
            .LocalPosition = { 3.5f, 2.2f, 3.5f }
        });
        registry.emplace<Engine::Graphics::PointLightComponent>(blueLightEnt, Engine::Graphics::PointLightComponent{
            .Color = { 0.20f, 0.50f, 0.90f },
            .Intensity = 16.0f,
            .Radius = 14.0f,
            .CastShadows = false
        });
        rt.RoomEntities.push_back(blueLightEnt);

        // カメラ: 宝玉周回軌道初期位置
        rt.Camera = ctx.CreateEntity();
        auto& camTrans = registry.emplace<Engine::Core::TransformComponent>(rt.Camera);
        camTrans.LocalPosition = { 2.85f, 1.32f, 0.0f };
        XMStoreFloat4(&camTrans.LocalRotation, XMQuaternionIdentity());
        registry.emplace<Engine::Graphics::CameraComponent>(rt.Camera, Engine::Graphics::CameraComponent{
            .FovY = XMConvertToRadians(60.0f),
            .AspectRatio = 1920.0f / 1080.0f,
            .NearZ = 0.1f,
            .FarZ = 200.0f
        });
        registry.emplace<LuminousTitleCameraTag>(rt.Camera);
        Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.Camera);

        ctx.RegisterSystem(Engine::Core::SystemPhase::Update, "LuminousTitleUpdateSystem",
                           &LuminousTitleUpdate);
    }

    inline void LuminousTitleScene::OnTeardown(Engine::Core::SceneContext& ctx) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        Input::SetCursorLocked(false);
        auto& registry = ctx.GetRegistry();
        for (auto e : rt.RoomEntities) {
            if (registry.valid(e)) {
                if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(e)) {
                    entt::entity sub = rootComp->FirstSubMesh;
                    while (sub != entt::null && registry.valid(sub)) {
                        entt::entity nextSub = registry.get<Engine::Graphics::SubMeshComponent>(sub).NextSubMesh;
                        Engine::Core::TransformAPI::DetachParent(registry, sub);
                        registry.destroy(sub);
                        sub = nextSub;
                    }
                }
                registry.destroy(e);
            }
        }
        rt.RoomEntities.clear();
        rt.FlameParticles.clear();
        if (rt.Camera != entt::null && registry.valid(rt.Camera)) {
            registry.destroy(rt.Camera);
            rt.Camera = entt::null;
        }
    }

    // ------------------------------------------------------------------------
    // .scene.json から起動したときの組み直し (タイトル)。
    //
    //   保存できるのはエンティティだけなので、「どれがカメラか」「火の粉を
    //   どこから出すか」は**エンティティに載せた印**から引き直す。
    //   コードのシーンから来たときは、すぐ帰る
    // ------------------------------------------------------------------------
    inline void LuminousTitleRestoreFromEntities(entt::registry& registry) {
        auto& rt = TitleRuntime(registry);
        if (rt.Camera != entt::null && registry.valid(rt.Camera)) return;

        const auto cameras = registry.view<LuminousTitleCameraTag>();
        if (cameras.empty()) return;   // まだ読み込み途中

        // シーンのファイルが「どの画面から始めるか」を言っていれば、それに従う
        //   (コードのシーンでは作るときの引数だったもの。計画 18 の段 2)
        rt.View = SceneSetting<LuminousTitleSettings>(registry).View;

        // 入場時の一手。コードのシーンでは OnSetup がやっていたもの
        rt.Camera = cameras.front();
        Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.Camera);
        Input::SetCursorLocked(false);
        LuminousTransition::Get().BeginSceneEnter();

        const auto orbs = registry.view<LuminousTitleOrbTag>();
        if (!orbs.empty()) rt.OrbMesh = orbs.front();

        // 火の粉を種から組み直す。
        //   **番号の順に並べる**。エンティティを見る順は決まっていないので、
        //   そのままだと起動のたびに粒の位相が入れ替わりかねない
        rt.FlameParticles.clear();
        std::vector<entt::entity> emitters;
        for (const auto entity : registry.view<LuminousFlameEmitterComponent,
                                               Engine::Core::TransformComponent>()) {
            emitters.push_back(entity);
        }
        std::sort(emitters.begin(), emitters.end());
        for (const auto entity : emitters) {
            const auto& emitter = registry.get<LuminousFlameEmitterComponent>(entity);
            const auto& trans = registry.get<Engine::Core::TransformComponent>(entity);
            for (int i = 0; i < emitter.Count; ++i) {
                LuminousTitleRuntime::TitleFlameParticle fp;
                fp.CurrentPosition = trans.LocalPosition;
                fp.Origin = trans.LocalPosition;
                fp.Phase = static_cast<float>(i) * (XM_2PI / static_cast<float>(emitter.Count));
                fp.Speed = 0.14f + static_cast<float>(i % 4) * 0.035f;
                fp.Radius = emitter.Radius;
                fp.BaseScale = emitter.BaseScale;
                rt.FlameParticles.push_back(fp);
            }
        }
    }

    inline void LuminousTitleUpdate(const Engine::Core::SystemContext& ctx) {
        // データだけのシーンから来たときは、ここで持ち物を組み直す
        LuminousTitleRestoreFromEntities(ctx.GetRegistry());
        auto& rt = TitleRuntime(ctx.GetRegistry());
        float dt = ctx.GetDeltaTime();
        rt.Timer += dt;

        auto& registry = ctx.GetRegistry();
        auto& particles = App::Graphics::ParticleDrawList::Get(registry);
        particles.Clear();

        // シーン遷移フェードの更新。暗転しきったところで実際の切り替えを発行する
        // (マテリアルの非同期コンパイル完了待ちも Tick 内で行う)
        LuminousInputDevice::Update();
        LuminousTransition::Get().Tick(dt, &registry);
        if (auto pendingScene = LuminousTransition::Get().TakePendingScene()) {
            ctx.RequestSceneLoad(pendingScene);
            // このフレームは以降の描画を行わないため、暗幕だけはここで描いておく。
            LuminousTransition::Get().Draw(1920.0f, 1080.0f);
            return;
        }

        // タイトル BGM: 暗転 (マテリアル読み込み待ち) が明け始めたら導入部をフェードインで再生し、
        // 導入部が終わるとループ部へ続く。読み込み待ちの間に導入部が無音で進まないよう、ここで開始する。
        if (!rt.BgmStarted && !LuminousTransition::Get().IsCoveringScreen()) {
            rt.BgmStarted = true;
            Engine::Audio::AudioEngine::Get().PlayBGMWithIntro(
                LuminousAudioConfig::BGM_Title_Start.FilePath, LuminousAudioConfig::BGM_Title_Loop.FilePath,
                LuminousAudioConfig::BGM_Title_Start.DefaultVolume, LuminousAudioConfig::BGM_IntroFadeInSeconds);
        }

        // 1. 台座の宝玉の周囲を回るカメラ
        rt.CameraYaw += XMConvertToRadians(10.0f) * dt;
        if (rt.CameraYaw > XM_2PI) rt.CameraYaw -= XM_2PI;

        float orbitRadius = 2.85f;
        float camX = std::cos(rt.CameraYaw) * orbitRadius;
        float camZ = std::sin(rt.CameraYaw) * orbitRadius;
        float camY = 1.32f + std::sin(rt.Timer * 0.45f) * 0.07f;

        float targetY = 0.38f;
        float dx = -camX;
        float dz = -camZ;
        float dy = targetY - camY;
        float distXZ = std::sqrt(dx * dx + dz * dz);

        // ロゴとメニューを画面左に置くので、宝玉は画面右寄りに映るよう視線を左へずらす
        // (台座を注視したままだと宝玉がロゴの真後ろに来て、ロゴが読みにくくなる)
        constexpr float ORB_SCREEN_OFFSET_DEG = 24.0f;
        float lookYaw = std::atan2(dx, dz) - XMConvertToRadians(ORB_SCREEN_OFFSET_DEG);
        float lookPitch = std::atan2(-dy, distXZ);

        if (registry.valid(rt.Camera)) {
            auto& camTrans = registry.get<Engine::Core::TransformComponent>(rt.Camera);
            camTrans.LocalPosition = { camX, camY, camZ };
            XMStoreFloat4(&camTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(lookPitch, lookYaw, 0.0f));
            camTrans.IsDirty = true;
        }

        // 2. 中央宝玉の呼吸浮遊アニメーション＆常時低速回転
        if (registry.valid(rt.OrbMesh)) {
            auto& trans = registry.get<Engine::Core::TransformComponent>(rt.OrbMesh);
            trans.LocalPosition.y = PEDESTAL_SOCKET_HEIGHT - 0.02f + std::sin(rt.Timer * 2.2f) * 0.025f;
            XMStoreFloat4(&trans.LocalRotation, XMQuaternionRotationRollPitchYaw(
                std::sin(rt.Timer * 0.25f) * 0.12f,
                rt.Timer * 0.40f,
                std::cos(rt.Timer * 0.35f) * 0.10f
            ));
            trans.IsDirty = true;
        }

        // 3. 背景壁掛け松明の火の粉 (丸スプライトのビルボード)
        for (auto& fp : rt.FlameParticles) {
            fp.Phase += dt * 2.2f;
            fp.LocalY += fp.Speed * dt;
            constexpr float maxH = 0.35f;
            if (fp.LocalY > maxH) fp.LocalY = 0.0f;
            float normY = fp.LocalY / maxH;
            float alpha = 0.40f + 0.60f * std::sin(normY * XM_PI);
            float curR = fp.Radius * (0.65f + 0.55f * normY);

            fp.CurrentScale = fp.BaseScale * alpha;
            fp.CurrentPosition = XMFLOAT3(
                fp.Origin.x + std::cos(fp.Phase) * curR,
                fp.Origin.y + fp.LocalY,
                fp.Origin.z + std::sin(fp.Phase) * curR
            );
            particles.Add(fp.CurrentPosition, fp.CurrentScale * 0.25f, { 3.0f, 1.86f, 0.66f, 0.9f });
        }

        // 4. マウス・キーボード・コントローラー入力受付
        Vector2 mPos = Input::GetMousePosition();
        bool isMouseDown = Input::GetKeyHold(KeyCode::MOUSE_LEFT);
        bool isMouseClick = Input::GetKeyDown(KeyCode::MOUSE_LEFT);

        float screenW = 1920.0f;
        float screenH = 1080.0f;

        if (rt.View == LuminousTitleView::MainMenu) {
            LuminousTitleRenderMenu(ctx, screenW, screenH, mPos.x, mPos.y, isMouseDown, isMouseClick);
        } else if (rt.View == LuminousTitleView::StageSelect) {
            LuminousTitleRenderStageSelect(ctx, screenW, screenH, mPos.x, mPos.y, isMouseDown, isMouseClick);
        } else if (rt.View == LuminousTitleView::CustomStageList) {
            LuminousTitleRenderCustomStageList(ctx, screenW, screenH, mPos.x, mPos.y, isMouseDown, isMouseClick);
        }

        // 5. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(screenW, screenH);
    }

    inline void LuminousTitleRenderMenu(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto& ui = Engine::Graphics::UIRenderer::Get();

        // 画面左側を沈めるグラデーション (ロゴとメニューの背景。宝玉は右側に映る)
        const auto& shade = LuminousUIConfig::Image_Title_Shade;
        ui.DrawImage(shade.FilePath, 0.0f, 0.0f, screenW, screenH);

        // タイトルロゴ「Luvia」(ゆっくり明滅)
        const auto& logo = LuminousUIConfig::Image_Title_Logo;
        const float logoX = 150.0f;
        const float logoY = 70.0f;
        const float breathe = 0.92f + 0.08f * std::sin(rt.Timer * 1.1f);
        ui.DrawImage(logo.FilePath, logoX, logoY, logo.DefaultWidth, logo.DefaultHeight, { 1.0f, 1.0f, 1.0f, breathe });
        const float menuCenterX = logoX + logo.DefaultWidth * 0.5f;

        // ステージセレクトの出現判定: 1ステージ以上クリア済み OR エディタ自作ステージ作成済み
        const bool isStageSelectAvail = ProfileManager::Get().IsStageSelectAvailable();

        enum class Action { Start, StageSelect, Editor, Exit };
        struct Item { const UIImageConfig* Normal; const UIImageConfig* Hover; const UIImageConfig* Pressed; Action Act; };
        std::vector<Item> items;
        items.push_back({ &LuminousUIConfig::Image_Btn_Start_Normal, &LuminousUIConfig::Image_Btn_Start_Hover, &LuminousUIConfig::Image_Btn_Start_Pressed, Action::Start });
        if (isStageSelectAvail) {
            items.push_back({ &LuminousUIConfig::Image_Btn_StageSelect_Normal, &LuminousUIConfig::Image_Btn_StageSelect_Hover, &LuminousUIConfig::Image_Btn_StageSelect_Pressed, Action::StageSelect });
        }
        items.push_back({ &LuminousUIConfig::Image_Btn_Editor_Normal, &LuminousUIConfig::Image_Btn_Editor_Hover, &LuminousUIConfig::Image_Btn_Editor_Pressed, Action::Editor });
        items.push_back({ &LuminousUIConfig::Image_Btn_Exit_Normal, &LuminousUIConfig::Image_Btn_Exit_Hover, &LuminousUIConfig::Image_Btn_Exit_Pressed, Action::Exit });

        rt.Cursor.BeginFrame(ctx.GetDeltaTime(), static_cast<int>(items.size()), Vector2(mouseX, mouseY));
        const Vector2 m = rt.Cursor.Mouse();

        const float btnW = 420.0f;
        const float btnH = 68.0f;
        const float btnX = menuCenterX - btnW * 0.5f;
        const float startY = isStageSelectAvail ? 520.0f : 556.0f;
        const float gap = 78.0f;

        int activated = -1;
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            const float y = startY + i * gap;
            rt.Cursor.Item(i, btnX, y, btnW, btnH);
            if (ui.DrawButton(items[i].Normal->FilePath, items[i].Hover->FilePath, items[i].Pressed->FilePath,
                btnX, y, btnW, btnH, m.x, m.y, isMouseDown, isMouseClick, nullptr,
                rt.Cursor.Is(i), rt.Cursor.Activated(i))) {
                activated = i;
            }
        }

        if (activated >= 0) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            switch (items[activated].Act) {
            case Action::Start: {
                const int total = LuminousStageCatalog::Count();
                const int nextStage = ProfileManager::Get().GetNextUncompletedStageIndex(total);
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(
                    LuminousStageCatalog::Load(nextStage - 1), false, nextStage));
                return;
            }
            case Action::StageSelect:
                rt.View = LuminousTitleView::StageSelect;
                rt.Cursor.Reset();
                return;
            case Action::Editor:
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousEditorScene>());
                return;
            case Action::Exit:
                PostQuitMessage(0);
                return;
            }
        }
        if (rt.Cursor.EndFrame()) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }

        // 操作説明 (操作中のデバイスに合わせたボタン画像)
        const float hintH = 38.0f;
        const float total = MeasureControlHint(LuminousButtonGlyph::Move, "CHOOSE", hintH)
                          + MeasureControlHint(LuminousButtonGlyph::Confirm, "DECIDE", hintH);
        float hx = menuCenterX - total * 0.5f;
        const float hy = screenH - 70.0f;
        hx += DrawControlHint(hx, hy, LuminousButtonGlyph::Move, "CHOOSE", hintH);
        DrawControlHint(hx, hy, LuminousButtonGlyph::Confirm, "DECIDE", hintH);
    }

    inline void LuminousTitleRenderStageSelect(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto& ui = Engine::Graphics::UIRenderer::Get();
        const float centerX = screenW * 0.5f;

        // 背景を少し沈め、3D の部屋はうっすら見せる
        ui.DrawPanel(0.0f, 0.0f, screenW, screenH, { 0.02f, 0.04f, 0.08f, 0.62f });

        // ヘッダー (Luvia ロゴと同じ細線・六角形の意匠)
        const auto& header = LuminousUIConfig::Image_StageSelect_Header;
        ui.DrawImage(header.FilePath, centerX - header.DefaultWidth * 0.5f, 56.0f, header.DefaultWidth, header.DefaultHeight);

        const int stageCount = LuminousStageCatalog::Count();
        const bool hasCustomStages = ProfileManager::Get().HasCustomStages();
        const int extraIndex = hasCustomStages ? stageCount : -1;
        const int backIndex = stageCount + (hasCustomStages ? 1 : 0);

        rt.Cursor.BeginFrame(ctx.GetDeltaTime(), backIndex + 1, Vector2(mouseX, mouseY));
        const Vector2 m = rt.Cursor.Mouse();

        const float gridTop = 215.0f;
        const float gridW = 1100.0f;
        const float gridBottom = 600.0f;

        int cols = (stageCount <= 2) ? (stageCount > 0 ? stageCount : 1) : 3;
        if (stageCount > 6) cols = 4;
        const int rows = (stageCount + cols - 1) / cols;

        const float gapX = 48.0f;
        const float gapY = 32.0f;
        float cardW = (std::min)(460.0f, (gridW - gapX * (cols - 1)) / static_cast<float>(cols));
        float cardH = cardW * (260.0f / 400.0f);

        const float maxGridH = gridBottom - gridTop;
        const float neededH = cardH * rows + gapY * (rows - 1);
        if (neededH > maxGridH && neededH > 1.0f) {
            const float shrink = maxGridH / neededH;
            cardW *= shrink;
            cardH *= shrink;
        }

        const float usedW = cardW * cols + gapX * (cols - 1);
        const float originX = centerX - usedW * 0.5f;
        const float pulse = 0.70f + 0.30f * std::sin(rt.Timer * 4.0f);

        const float extraY = 650.0f;
        const float backY = hasCustomStages ? 736.0f : 660.0f;

        // 先に全ボタンの矩形を登録しておく (方向キーの移動先探しに使う)
        for (int i = 0; i < stageCount; ++i) {
            rt.Cursor.Item(i, originX + (i % cols) * (cardW + gapX), gridTop + (i / cols) * (cardH + gapY), cardW, cardH);
        }
        if (hasCustomStages) rt.Cursor.Item(extraIndex, centerX - 210.0f, extraY, 420.0f, 68.0f);
        rt.Cursor.Item(backIndex, centerX - 210.0f, backY, 420.0f, 68.0f);

        for (int i = 0; i < stageCount; ++i) {
            const auto* entry = LuminousStageCatalog::Get(i);
            if (entry == nullptr) continue;

            const int stageNumber = i + 1;
            const float x = originX + (i % cols) * (cardW + gapX);
            const float y = gridTop + (i / cols) * (cardH + gapY);

            const bool unlocked = ProfileManager::Get().IsStageUnlocked(stageNumber);
            std::string img = unlocked
                ? entry->ButtonImage
                : (entry->LockedImage.empty() ? LuminousUIConfig::Image_Card_Locked.FilePath : entry->LockedImage);

            const bool clicked = ui.DrawButton(img, img, img, x, y, cardW, cardH,
                m.x, m.y, isMouseDown, isMouseClick, nullptr, rt.Cursor.Is(i), rt.Cursor.Activated(i));
            if (rt.Cursor.Is(i)) {
                DrawUIFrame(x - 7.0f, y - 7.0f, cardW + 14.0f, cardH + 14.0f, 2.0f, { 0.86f, 0.96f, 0.99f, pulse });
            }

            if (clicked) {
                if (unlocked) {
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(
                        LuminousStageCatalog::Load(i), false, stageNumber));
                    return;
                }
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            }

            if (entry->ButtonImage.empty() && !entry->DisplayName.empty()) {
                ui.DrawString(entry->DisplayName, x + 16.0f, y + cardH * 0.5f - 9.0f, 16.0f,
                    unlocked ? DirectX::XMFLOAT4{ 0.95f, 0.97f, 1.0f, 0.95f } : DirectX::XMFLOAT4{ 0.55f, 0.60f, 0.68f, 0.80f });
            }
        }

        if (hasCustomStages) {
            if (ui.DrawButton(
                LuminousUIConfig::Image_Btn_Extra_Normal.FilePath,
                LuminousUIConfig::Image_Btn_Extra_Hover.FilePath,
                LuminousUIConfig::Image_Btn_Extra_Hover.FilePath,
                centerX - 210.0f, extraY, 420.0f, 68.0f, m.x, m.y, isMouseDown, isMouseClick, nullptr,
                rt.Cursor.Is(extraIndex), rt.Cursor.Activated(extraIndex)))
            {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                rt.View = LuminousTitleView::CustomStageList;
                rt.Cursor.Reset();
                return;
            }
        }

        if (ui.DrawButton(
            LuminousUIConfig::Image_Btn_Back_Normal.FilePath,
            LuminousUIConfig::Image_Btn_Back_Hover.FilePath,
            LuminousUIConfig::Image_Btn_Back_Hover.FilePath,
            centerX - 210.0f, backY, 420.0f, 68.0f, m.x, m.y, isMouseDown, isMouseClick, nullptr,
            rt.Cursor.Is(backIndex), rt.Cursor.Activated(backIndex))
            || LuminousInput::CancelPressed())
        {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            rt.View = LuminousTitleView::MainMenu;
            rt.Cursor.Reset();
            return;
        }

        if (rt.Cursor.EndFrame()) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }

        const float hintH = 38.0f;
        const float total = MeasureControlHint(LuminousButtonGlyph::Move, "CHOOSE", hintH)
                          + MeasureControlHint(LuminousButtonGlyph::Confirm, "DECIDE", hintH)
                          + MeasureControlHint(LuminousButtonGlyph::Cancel, "BACK", hintH);
        float hx = centerX - total * 0.5f;
        const float hy = screenH - 70.0f;
        hx += DrawControlHint(hx, hy, LuminousButtonGlyph::Move, "CHOOSE", hintH);
        hx += DrawControlHint(hx, hy, LuminousButtonGlyph::Confirm, "DECIDE", hintH);
        DrawControlHint(hx, hy, LuminousButtonGlyph::Cancel, "BACK", hintH);
    }

    inline void LuminousTitleRenderCustomStageList(const Engine::Core::SystemContext& ctx, float screenW, float screenH, float mouseX, float mouseY, bool isMouseDown, bool isMouseClick) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto& ui = Engine::Graphics::UIRenderer::Get();
        const float centerX = screenW * 0.5f;

        ui.DrawPanel(0.0f, 0.0f, screenW, screenH, { 0.02f, 0.04f, 0.08f, 0.70f });

        ui.DrawString("CUSTOM STAGES", centerX - 150.0f, 90.0f, 34.0f, { 0.86f, 0.96f, 0.99f, 1.0f });
        ui.DrawPanel(centerX - 420.0f, 144.0f, 840.0f, 1.0f, { 0.86f, 0.96f, 0.99f, 0.45f });
        ui.DrawString("Stages created in the Stage Editor", centerX - 190.0f, 156.0f, 17.0f, { 0.59f, 0.77f, 0.85f, 0.90f });

        const auto customStages = ProfileManager::Get().GetCustomStages();
        const int rowCount = static_cast<int>((std::min)(customStages.size(), static_cast<size_t>(6)));
        const int backIndex = rowCount;
        rt.Cursor.BeginFrame(ctx.GetDeltaTime(), rowCount + 1, Vector2(mouseX, mouseY));
        const Vector2 m = rt.Cursor.Mouse();

        const float startY = 200.0f;
        const float rowH = 95.0f;
        const float rowX = centerX - 270.0f;
        const float rowW = 690.0f;
        const float btnH = 85.0f;

        for (int i = 0; i < rowCount; ++i) rt.Cursor.Item(i, rowX, startY + i * rowH, rowW, btnH);
        rt.Cursor.Item(backIndex, centerX - 210.0f, 860.0f, 420.0f, 68.0f);

        for (int i = 0; i < rowCount; ++i) {
            const float y = startY + i * rowH;
            ui.DrawImage(LuminousUIConfig::Image_Card_Custom.FilePath, centerX - 420.0f, y, 130.0f, 85.0f);

            const bool isOver = (m.x >= rowX && m.x <= rowX + rowW && m.y >= y && m.y <= y + btnH);
            const bool selected = rt.Cursor.Is(i);

            ui.DrawPanel(rowX, y, rowW, btnH, selected ? DirectX::XMFLOAT4{ 0.09f, 0.29f, 0.43f, 0.85f } : DirectX::XMFLOAT4{ 0.03f, 0.06f, 0.10f, 0.85f });
            DrawUIFrame(rowX, y, rowW, btnH, 1.0f, { 0.86f, 0.96f, 0.99f, selected ? 0.95f : 0.35f });

            if ((isOver && isMouseClick) || rt.Cursor.Activated(i)) {
                LuminousStage customStage;
                std::string err;
                if (customStage.LoadFromFile(customStages[i].second, err)) {
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(customStage, false));
                    return;
                }
            }

            ui.DrawString(customStages[i].first, rowX + 25.0f, y + 20.0f, 22.0f,
                selected ? DirectX::XMFLOAT4{ 1.0f, 1.0f, 1.0f, 1.0f } : DirectX::XMFLOAT4{ 0.86f, 0.96f, 0.99f, 1.0f });
            ui.DrawString("Path: " + customStages[i].second, rowX + 25.0f, y + 52.0f, 15.0f, { 0.59f, 0.70f, 0.78f, 0.85f });
        }

        if (ui.DrawButton(
            LuminousUIConfig::Image_Btn_Back_Normal.FilePath,
            LuminousUIConfig::Image_Btn_Back_Hover.FilePath,
            LuminousUIConfig::Image_Btn_Back_Hover.FilePath,
            centerX - 210.0f, 860.0f, 420.0f, 68.0f, m.x, m.y, isMouseDown, isMouseClick, nullptr,
            rt.Cursor.Is(backIndex), rt.Cursor.Activated(backIndex))
            || LuminousInput::CancelPressed())
        {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            rt.View = LuminousTitleView::StageSelect;
            rt.Cursor.Reset();
            return;
        }

        if (rt.Cursor.EndFrame()) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }

        const float hintH = 38.0f;
        const float total = MeasureControlHint(LuminousButtonGlyph::Move, "CHOOSE", hintH)
                          + MeasureControlHint(LuminousButtonGlyph::Confirm, "DECIDE", hintH)
                          + MeasureControlHint(LuminousButtonGlyph::Cancel, "BACK", hintH);
        float hx = centerX - total * 0.5f;
        const float hy = screenH - 70.0f;
        hx += DrawControlHint(hx, hy, LuminousButtonGlyph::Move, "CHOOSE", hintH);
        hx += DrawControlHint(hx, hy, LuminousButtonGlyph::Confirm, "DECIDE", hintH);
        DrawControlHint(hx, hy, LuminousButtonGlyph::Cancel, "BACK", hintH);
    }

    // ========================================================================
    // LuminousPlayScene 実装
    // ========================================================================
    inline void LuminousPlayScene::OnSetup(Engine::Core::SceneContext& ctx) {
        // 本編の持ち物をレジストリに作る (シーンが終わるときに一緒に片付く)
        runtimeEntity_ = ctx.CreateEntity();
        // 走っている間だけの持ち物。.scene.json には書き出さない
        ctx.GetRegistry().emplace<Engine::Core::GeneratedTag>(runtimeEntity_);
        world_ = &ctx.GetRegistry();
        auto& rt = ctx.GetRegistry().emplace<LuminousPlayRuntime>(runtimeEntity_);
        rt.Stage = std::move(pendingStage_);
        rt.ReturnToEditorOnExit = pendingReturnToEditor_;
        rt.StageNumber = pendingStageNumber_;
        auto& registry = ctx.GetRegistry();

        rt.FootstepToggle = false;
        rt.LastFootstepIndex = 0;
        rt.FootstepPhaseValid = false;
        rt.PrevHoldingOrb = false;
        rt.PrevGliding = false;
        rt.ClearFanfarePlayed = false;

        // 1〜3. 遊ぶのに欠かせないもの (カメラ・環境光・手持ちの宝玉)。
        //   データだけのシーンから起こすときと同じ道を通す
        LuminousPlayEnsureCoreEntities(registry);

        // 4. ステージオブジェクトのスポーン
        LuminousPlaySpawnStageObjects(registry);

        // 5〜9. ステージから、遊びに要るものを組み立てる
        LuminousPlayBuildRuntime(registry);


        // 10. 毎フレーム更新システムを、このシーンで回す。
        //   名前で引けるようにする登録は **ゲームモジュール側** (RegisterLuminousSystems)。
        //   シーンより先に済ませておかないと、.scene.json から起動したときに
        //   「そんなシステムは無い」と言われる
        ctx.RegisterSystem(Engine::Core::SystemPhase::Update, "LuminousPlayUpdateSystem",
                           &LuminousPlayUpdate);
    }

    inline void LuminousPlayScene::OnTeardown(Engine::Core::SceneContext& ctx) {
        auto& rt = PlayRuntime(ctx.GetRegistry());
        Input::SetCursorLocked(false);

        auto& registry = ctx.GetRegistry();
        auto DestroyWithSubMeshes = [&](entt::entity e) {
            if (!registry.valid(e)) return;
            if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(e)) {
                entt::entity sub = rootComp->FirstSubMesh;
                while (sub != entt::null && registry.valid(sub)) {
                    entt::entity nextSub = registry.get<Engine::Graphics::SubMeshComponent>(sub).NextSubMesh;
                    Engine::Core::TransformAPI::DetachParent(registry, sub);
                    registry.destroy(sub);
                    sub = nextSub;
                }
                rootComp->FirstSubMesh = entt::null;
            }
            registry.destroy(e);
        };

        if (rt.PlayerCamera != entt::null) DestroyWithSubMeshes(rt.PlayerCamera);
        if (rt.HeldOrbMesh != entt::null) DestroyWithSubMeshes(rt.HeldOrbMesh);
        if (rt.HeldOrbLight != entt::null) DestroyWithSubMeshes(rt.HeldOrbLight);
        if (rt.GoalChestBase != entt::null) DestroyWithSubMeshes(rt.GoalChestBase);
        if (rt.GoalChestLid != entt::null) DestroyWithSubMeshes(rt.GoalChestLid);
        if (rt.GoalChestLight != entt::null) DestroyWithSubMeshes(rt.GoalChestLight);

        rt.DebugTool.Shutdown(registry);
        rt.GuidanceStream.Reset();
        App::Graphics::ParticleDrawList::Get(registry).Clear();
        for (auto& pair : rt.PedestalOrbEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        for (auto& pair : rt.PedestalLightEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        for (auto lEnt : rt.StageLightEntities) {
            if (lEnt != entt::null) DestroyWithSubMeshes(lEnt);
        }
        rt.StageLightEntities.clear();
        for (auto fEnt : rt.StageFlameEntities) {
            if (fEnt != entt::null) DestroyWithSubMeshes(fEnt);
        }
        rt.StageFlameEntities.clear();
        rt.StageFlameParticles.clear();
        for (auto& pair : rt.ObjectEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.ObjectEntities.clear();
        rt.PedestalOrbEntities.clear();
        rt.PedestalLightEntities.clear();
    }

    inline void LuminousPlaySpawnStageObjects(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
                auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        auto* meshMgr = Engine::Core::GetService<Engine::Graphics::MeshManager>(registry);

        for (const auto& obj : rt.Stage.Objects) {
            auto ent = CreateGeneratedEntity(registry);
            auto& trans = registry.emplace<Engine::Core::TransformComponent>(ent);
            trans.LocalPosition = obj.Position;
            XMStoreFloat4(&trans.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(obj.Rotation.y), 0.0f));
            trans.LocalScale = obj.Scale;

            std::string modelPath = "Assets/Models/Dungeon/" + obj.AssetId + ".obj";
            auto mesh = meshMgr->GetOrLoadMesh(modelPath);

            if (obj.Phase == MaterialPhase::Phase) {
                PhaseWallMaterialCB initWall;
                PhaseFloorMaterialCB initFloor;
                auto phasePrefabs = GetAssetPhasePrefabs(obj.AssetId, obj.Category, materialMgr, initWall, initFloor);
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, phasePrefabs);
            } else {
                if (obj.AssetId == "Pedestal") {
                    auto subPrefabs = GetAssetSubmeshPrefabs(obj.AssetId, materialMgr);
                    ApplyOrbOccluderTags(obj.AssetId, obj.Category, obj.Phase, subPrefabs);
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, subPrefabs);

                    // ソケットの浮遊宝玉とライト
                    auto pOrbMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");
                    auto pOrbMat  = CreateLuminousOrbPrefab(materialMgr);

                    auto orbEnt = CreateGeneratedEntity(registry);
                    registry.emplace<Engine::Core::TransformComponent>(orbEnt, Engine::Core::TransformComponent{
                        .LocalPosition = { obj.Position.x, obj.Position.y + PEDESTAL_SOCKET_HEIGHT, obj.Position.z }
                    });
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, orbEnt, pOrbMesh, pOrbMat);
                    rt.PedestalOrbEntities[obj.InstanceId] = orbEnt;

                    auto pLightEnt = CreateGeneratedEntity(registry);
                    registry.emplace<Engine::Core::TransformComponent>(pLightEnt, Engine::Core::TransformComponent{
                        .LocalPosition = { obj.Position.x, obj.Position.y + PEDESTAL_SOCKET_HEIGHT, obj.Position.z }
                    });
                    registry.emplace<Engine::Graphics::PointLightComponent>(pLightEnt, Engine::Graphics::PointLightComponent{
                        .Color = { 1.0f, 0.88f, 0.28f },
                        .Intensity = obj.HasInitialOrb ? g_LuminousConfig.OrbLightIntensity : 0.0f,
                        .Radius = g_LuminousConfig.OrbInfluenceRadius,
                        .CastShadows = true,
                        .ShadowNearZ = g_LuminousConfig.OrbLightNearClip
                    });
                    rt.PedestalLightEntities[obj.InstanceId] = pLightEnt;

                } else if (obj.AssetId == "GoalChest") {
                    rt.GoalChestPos = obj.Position;
                    rt.GoalChestYaw = XMConvertToRadians(obj.Rotation.y);
                    rt.GoalChestBase = ent;

                    auto baseMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/GoalChest_Base.obj");
                    auto basePrefabs = GetAssetSubmeshPrefabs("GoalChest_Base", materialMgr);
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, baseMesh, basePrefabs);

                    // 蓋エンティティ: 同一の原点 (0, 0, 0) で同期配置
                    auto lidMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/GoalChest_Lid.obj");
                    auto lidPrefabs = GetAssetSubmeshPrefabs("GoalChest_Lid", materialMgr);
                    rt.GoalChestLid = CreateGeneratedEntity(registry);
                    auto& lidTrans = registry.emplace<Engine::Core::TransformComponent>(rt.GoalChestLid);
                    lidTrans.LocalPosition = obj.Position;
                    lidTrans.LocalRotation = registry.get<Engine::Core::TransformComponent>(ent).LocalRotation;
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, rt.GoalChestLid, lidMesh, lidPrefabs);

                    // 内部黄金ライト
                    rt.GoalChestLight = CreateGeneratedEntity(registry);
                    registry.emplace<Engine::Core::TransformComponent>(rt.GoalChestLight, Engine::Core::TransformComponent{
                        .LocalPosition = XMFLOAT3(obj.Position.x, obj.Position.y + 0.40f, obj.Position.z)
                    });
                    registry.emplace<Engine::Graphics::PointLightComponent>(rt.GoalChestLight, Engine::Graphics::PointLightComponent{
                        .Color = { 1.0f, 0.85f, 0.2f },
                        .Intensity = 0.0f,
                        .Radius = 8.0f,
                        .CastShadows = true,
                        .ShadowNearZ = g_LuminousConfig.PropLightNearClip
                    });
                } else {
                    auto subPrefabs = GetAssetSubmeshPrefabs(obj.AssetId, materialMgr);
                    ApplyOrbOccluderTags(obj.AssetId, obj.Category, obj.Phase, subPrefabs);
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, subPrefabs);
                }
            }

            if (obj.HasLight) {
                XMFLOAT3 flamePos = obj.Position;
                XMFLOAT3 flameScale = { 0.45f, 0.72f, 0.45f };

                if (obj.AssetId == "Prop_Torch") {
                    float radY = XMConvertToRadians(obj.Rotation.y);
                    constexpr float tipOffsetY = 0.5512f; // 斜め柄の先端カップ中心高さ
                    constexpr float tipOffsetZ = -0.1101f; // 前方へ傾いたお椀先端
                    flamePos = XMFLOAT3(
                        obj.Position.x + tipOffsetZ * std::sin(radY),
                        obj.Position.y + tipOffsetY,
                        obj.Position.z + tipOffsetZ * std::cos(radY)
                    );
                    flameScale = { 0.45f, 0.72f, 0.45f };
                } else if (obj.AssetId == "Prop_Candle") {
                    flamePos = XMFLOAT3(obj.Position.x, obj.Position.y + 0.24f, obj.Position.z);
                    flameScale = { 0.15f, 0.24f, 0.15f };
                } else {
                    flamePos.y += 0.35f;
                }

                // 1. ポイントライト
                auto lEnt = CreateGeneratedEntity(registry);
                registry.emplace<Engine::Core::TransformComponent>(lEnt, Engine::Core::TransformComponent{
                    .LocalPosition = flamePos
                });
                registry.emplace<Engine::Graphics::PointLightComponent>(lEnt, Engine::Graphics::PointLightComponent{
                    .Color = obj.LightColor,
                    .Intensity = obj.LightIntensity,
                    .Radius = obj.LightRadius,
                    .CastShadows = true,
                    .ShadowNearZ = g_LuminousConfig.PropLightNearClip
                });
                rt.StageLightEntities.push_back(lEnt);

                // 2. 光源エミッシブオブジェクト（3倍サイズ、シャドウキャスティングなし）
                auto flameMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");
                auto flameMat  = CreateFlameEmissivePrefab(materialMgr, obj.LightColor, obj.LightIntensity);
                auto fEnt = CreateGeneratedEntity(registry);
                registry.emplace<Engine::Core::TransformComponent>(fEnt, Engine::Core::TransformComponent{
                    .LocalPosition = flamePos,
                    .LocalScale = flameScale
                });
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, fEnt, flameMesh, flameMat);
                rt.StageFlameEntities.push_back(fEnt);

                // 3. 火の粉パーティクル (各ランプ8個、上昇・螺旋拡散。丸スプライトのビルボードで描画)
                for (int pIdx = 0; pIdx < 8; ++pIdx) {
                    float pScale = (obj.AssetId == "Prop_Candle") ? 0.048f : 0.082f;
                    LuminousPlayRuntime::FlameParticleData fp;
                    fp.Color = obj.LightColor;
                    fp.CurrentPosition = flamePos;
                    fp.Origin = flamePos;
                    fp.Phase = static_cast<float>(pIdx) * (XM_2PI / 8.0f);
                    fp.Speed = 0.14f + static_cast<float>(pIdx % 4) * 0.035f;
                    fp.Radius = (obj.AssetId == "Prop_Candle") ? 0.055f : 0.115f;
                    fp.BaseScale = pScale;
                    rt.StageFlameParticles.push_back(fp);
                }
            }

            // ステージの中身を**そのエンティティに載せる**。
            //   これでインスペクターから種類・相 (Phase)・光源をそのまま直せる。
            //   以前はシーンが抱える std::vector の中だけにあり、見えなかった
            registry.emplace<PlacedObject>(ent, obj);
            rt.ObjectEntities[obj.InstanceId] = ent;
        }
    }

    inline void LuminousPlaySyncPhaseVisuals(Engine::Graphics::MaterialManager* materialMgr, entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        PhaseWallMaterialCB wallCb;
        PhaseFloorMaterialCB floorCb;

        // 反転判定に寄与する宝玉を最大 8 個まで収集する。
        // (台座固定ぶん + 手持ちの 1 個。8 個を超えるぶんは描画に反映されない)
        int orbCount = 0;
        for (const auto& orb : rt.Orbs) {
            if (orbCount >= static_cast<int>(App::Luminous::ORB_OCCLUSION_MAX_ORBS)) break;
            wallCb.OrbPositions[orbCount][0] = orb.WorldPosition.x;
            wallCb.OrbPositions[orbCount][1] = orb.WorldPosition.y;
            wallCb.OrbPositions[orbCount][2] = orb.WorldPosition.z;
            wallCb.OrbPositions[orbCount][3] = orb.Radius;

            floorCb.OrbPositions[orbCount][0] = orb.WorldPosition.x;
            floorCb.OrbPositions[orbCount][1] = orb.WorldPosition.y;
            floorCb.OrbPositions[orbCount][2] = orb.WorldPosition.z;
            floorCb.OrbPositions[orbCount][3] = orb.Radius;
            ++orbCount;
        }
        wallCb.ActiveOrbCount = static_cast<float>(orbCount);
        floorCb.ActiveOrbCount = static_cast<float>(orbCount);

        // ------------------------------------------------------------
        // Phase 2: 宝玉遮蔽アトラスの生成要求と、生成済みアトラスの参照
        // ------------------------------------------------------------
        App::Luminous::OrbOcclusionRequest occRequest;
        occRequest.Count = static_cast<uint32_t>(orbCount);
        occRequest.GeometryRevision = rt.GeometryRevision;
        for (int i = 0; i < static_cast<int>(App::Luminous::ORB_OCCLUSION_MAX_ORBS); ++i) {
            occRequest.Orbs[i] = XMFLOAT4(wallCb.OrbPositions[i][0], wallCb.OrbPositions[i][1],
                                          wallCb.OrbPositions[i][2], wallCb.OrbPositions[i][3]);
        }
        registry.ctx().insert_or_assign(occRequest);

        // アトラスは前フレームの OrbOcclusionPass が生成済みのものを引く。
        // 生成前 (初回フレーム/宝玉なし) は Enabled=0 として遮蔽判定を行わない。
        if (const auto* occInfo = registry.ctx().find<App::Luminous::OrbOcclusionInfo>()) {
            const bool usable = occInfo->Valid && occInfo->Count == occRequest.Count;
            const float idx = static_cast<float>(occInfo->AtlasIndex);
            const float enabled = (usable && !rt.DebugTool.DisableOrbOcclusionAtlas()) ? 1.0f : 0.0f;

            wallCb.OrbOcclusionAtlasIndex = idx;
            wallCb.OrbOcclusionNearZ = occInfo->NearZ;
            wallCb.OrbOcclusionFarZ = occInfo->FarZ;
            wallCb.OrbOcclusionEnabled = enabled;

            floorCb.OrbOcclusionAtlasIndex = idx;
            floorCb.OrbOcclusionNearZ = occInfo->NearZ;
            floorCb.OrbOcclusionFarZ = occInfo->FarZ;
            floorCb.OrbOcclusionEnabled = enabled;
        }

        auto updatedWallMat = CreatePhaseWallPrefab(materialMgr, wallCb);
        auto updatedFloorMat = CreatePhaseFloorPrefab(materialMgr, floorCb);

        // 反転壁・反転床のマテリアル同期 (サブメッシュ毎の色調を保持)
        for (const auto& obj : rt.Stage.Objects) {
            if (obj.Phase != MaterialPhase::Phase) continue;
            auto it = rt.ObjectEntities.find(obj.InstanceId);
            if (it == rt.ObjectEntities.end() || !registry.valid(it->second)) continue;

            auto phasePrefabs = GetAssetPhasePrefabs(obj.AssetId, obj.Category, materialMgr, wallCb, floorCb);
            ApplySubmeshPrefabs(registry, it->second, phasePrefabs);
        }
    }

    inline void LuminousPlaySubmitParticles(entt::registry& registry) {
        auto& rt = PlayRuntime(registry);
        auto& list = App::Graphics::ParticleDrawList::Get(registry);

        // 燭台・松明の火の粉
        for (const auto& fp : rt.StageFlameParticles) {
            constexpr float glow = 3.0f;
            list.Add(fp.CurrentPosition, fp.CurrentScale * 0.25f,
                { fp.Color.x * glow, fp.Color.y * glow, fp.Color.z * glow, 0.9f });
        }

        // ゴール誘導 (放出時に軌道が確定した粒子)
        rt.GuidanceSprites.clear();
        rt.GuidanceStream.Collect(rt.GuidanceSprites);
        for (const auto& s : rt.GuidanceSprites) {
            list.Add(s.Position, s.Radius, s.Color);
        }

        // 宝箱の光の噴水
        if (rt.ChestBurstTriggered) {
            for (const auto& p : rt.ChestBurstParticles) {
                if (p.life >= p.maxLife) continue;
                const float decay = std::clamp(1.0f - (p.life / p.maxLife), 0.0f, 1.0f);
                list.Add(p.pos, p.baseScale * decay * 0.5f, { 2.8f, 2.1f, 0.7f, decay });
            }
        }
    }

    inline void LuminousPlayUpdate(const Engine::Core::SystemContext& ctx) {
        const float realDt = ctx.GetDeltaTime();
        float dt = realDt;

        auto& registry = ctx.GetRegistry();
        // .scene.json から来たときは、ここで持ち物を組み直す (C++ のシーンからなら素通り)
        LuminousPlayRestoreFromEntities(registry);
        auto& rt = PlayRuntime(registry);
        if (rt.Stage.Objects.empty()) return;   // まだ組めていない
        auto& player = PlayerState(registry);
        auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        App::Graphics::ParticleDrawList::Get(registry).Clear();

        // シーン遷移フェードの更新。暗転しきったところで実際の切り替えを発行する
        LuminousInputDevice::Update();
        LuminousTransition::Get().Tick(dt, &registry);
        if (auto pendingScene = LuminousTransition::Get().TakePendingScene()) {
            ctx.RequestSceneLoad(pendingScene);
            LuminousTransition::Get().Draw(1920.0f, 1080.0f);
            return;
        }

        // 暗転中はゲームを進行させない (ロード待ちの間に操作が入らないようにする)
        const bool sceneCovered = LuminousTransition::Get().IsCoveringScreen();
        if (sceneCovered) dt = 0.0f;

        // 明転が始まったらステージ名オーバーレイを開始する
        if (!rt.StageIntroStarted && !sceneCovered) {
            rt.StageIntroStarted = true;

            // ステージ BGM: 導入部をフェードインで 1 回 → ループ部をループ
            Engine::Audio::AudioEngine::Get().PlayBGMWithIntro(
                LuminousAudioConfig::BGM_Stage_Start.FilePath, LuminousAudioConfig::BGM_Stage_Loop.FilePath,
                LuminousAudioConfig::BGM_Stage_Start.DefaultVolume, LuminousAudioConfig::BGM_IntroFadeInSeconds);

            const auto* entry = LuminousStageCatalog::GetByStageNumber(rt.StageNumber);
            if (entry != nullptr) {
                rt.StageIntro.Begin(entry->OverlayImage);
            }
        }
        rt.StageIntro.Tick(dt);

        // ------------------------------------------------------------
        // F12: デバッグツールの開始 / 終了
        // ------------------------------------------------------------
        if (LuminousDebugTools::Enabled && !sceneCovered && Input::GetKeyDown(LuminousDebugTools::ToggleKey)) {
            auto dctx = LuminousPlayDebugContext(registry);
            if (rt.DebugTool.IsActive()) {
                rt.DebugTool.Deactivate(dctx);
            } else {
                rt.Paused = false;
                rt.DebugTool.Activate(dctx);
            }
        }
        const bool debugActive = rt.DebugTool.IsActive();
        if (debugActive) {
            auto dctx = LuminousPlayDebugContext(registry);
            rt.DebugTool.HandleHotkeys(dctx);
            dt = rt.DebugTool.ScaleDeltaTime(dt);
        }
        // デバッグ中も既定ではプレイヤーを操作できる。
        // Ctrl でツール操作モード (プレイヤー停止・デバッグメニュー操作) に切り替わり、
        // スペクテイターカメラ中もプレイヤーは止まる。
        const bool playerInput = !debugActive || rt.DebugTool.PlayerControlEnabled();

        if (!rt.Paused && !player.IsStageCleared) rt.GameTime += dt;

        // START (Space) でポーズ切り替え
        if (!debugActive && LuminousInput::PausePressed() && !player.IsStageCleared) {
            rt.Paused = !rt.Paused;
            rt.Cursor.Reset();
            Input::SetCursorLocked(!rt.Paused);
            if (rt.Paused) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            } else {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            }
        }

        if (!rt.Paused) {
            if (!debugActive && Input::IsWindowActive() && !Input::IsCursorLocked() && !player.IsStageCleared) {
                Input::SetCursorLocked(true);
            }

            // 1. プレイヤー移動とFPSカメラ更新 (デバッグ中はプレイヤーを止め、カメラをツールが操作する)
            if (registry.valid(rt.PlayerCamera)) {
                auto& camTransform = registry.get<Engine::Core::TransformComponent>(rt.PlayerCamera);
                if (playerInput) {
                    PlayerSystem::UpdatePlayer(player, camTransform, rt.Stage.Objects, rt.Orbs, rt.OcclusionWalls, rt.BakedPedestals, dt);
                } else {
                    auto dctx = LuminousPlayDebugContext(registry);
                    rt.DebugTool.UpdateCamera(dctx, camTransform, realDt);
                }

                // 空間音響: リスナー（カメラ）位置・視線向き更新
                XMVECTOR rotQuat = XMLoadFloat4(&camTransform.LocalRotation);
                XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), rotQuat);
                XMVECTOR up = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), rotQuat);
                XMFLOAT3 fwdVec, upVec;
                XMStoreFloat3(&fwdVec, forward);
                XMStoreFloat3(&upVec, up);
                Engine::Audio::AudioEngine::Get().Update3DListener(
                    camTransform.LocalPosition.x, camTransform.LocalPosition.y, camTransform.LocalPosition.z,
                    fwdVec.x, fwdVec.y, fwdVec.z,
                    upVec.x, upVec.y, upVec.z
                );

                // 石畳歩行足音 (ヘッドボブの沈み込みに同期して左右交互)
                float hSpeedSq = player.Velocity.x * player.Velocity.x + player.Velocity.z * player.Velocity.z;
                if (playerInput && player.IsGrounded && hSpeedSq > 0.40f && !player.IsGliding && !player.IsStageCleared) {
                    const float bobFreq = 4.0f * g_LuminousConfig.CameraShakeSpeed;
                    const float phase = player.WalkDistanceAccumulator * XM_PI * bobFreq;
                    const float stepIndexF = std::floor((phase + XM_PIDIV2) / XM_2PI);
                    const int stepIndex = static_cast<int>(stepIndexF);

                    if (!rt.FootstepPhaseValid) {
                        rt.FootstepPhaseValid = true;
                        rt.LastFootstepIndex = stepIndex;
                    } else if (stepIndex != rt.LastFootstepIndex) {
                        rt.LastFootstepIndex = stepIndex;
                        if (rt.FootstepToggle) {
                            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_Footstep_L.FilePath, LuminousAudioConfig::SE_Footstep_L.DefaultVolume);
                        } else {
                            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_Footstep_R.FilePath, LuminousAudioConfig::SE_Footstep_R.DefaultVolume);
                        }
                        rt.FootstepToggle = !rt.FootstepToggle;
                    }
                } else {
                    rt.FootstepPhaseValid = false;
                }
            }

            // 2. 宝玉位置の更新 (手持ち追従 or 台座浮遊)
            XMFLOAT3 eyePos(player.Position.x, player.Position.y + player.EyeHeight, player.Position.z);
            OrbSystem::UpdateOrbs(rt.Orbs, player, eyePos, player.Yaw, player.Pitch, rt.GameTime, dt, rt.OcclusionWalls);

            // 3. インタラクト判定 (台座 / ゴール)
            PlayerSystem::UpdateInteractionRaycast(player, rt.Stage.Objects, rt.Orbs);

            // 4. 調べるボタンでの脱着処理 (デバッグのツール操作モード・スペクテイター中は受け付けない)
            if (playerInput) {
                OrbSystem::HandleInteraction(player, rt.Orbs, rt.Stage.Objects, eyePos, player.Yaw, player.Pitch);
            }

            // 宝玉取得・装填効果音トリガー
            if (!rt.PrevHoldingOrb && player.IsHoldingOrb) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_Orb_Pickup.FilePath, LuminousAudioConfig::SE_Orb_Pickup.DefaultVolume);
            } else if (rt.PrevHoldingOrb && !player.IsHoldingOrb) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_Orb_Insert.FilePath, LuminousAudioConfig::SE_Orb_Insert.DefaultVolume);
            }
            rt.PrevHoldingOrb = player.IsHoldingOrb;

            // 反転境界すり抜けシマー音
            if (!rt.PrevGliding && player.IsGliding) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_Phase_Pass.FilePath, LuminousAudioConfig::SE_Phase_Pass.DefaultVolume);
            }
            rt.PrevGliding = player.IsGliding;

            // 5. 手持ち宝玉状態の反映

            if (registry.valid(rt.HeldOrbMesh) && registry.valid(rt.HeldOrbLight)) {
                auto& mTrans = registry.get<Engine::Core::TransformComponent>(rt.HeldOrbMesh);
                auto& lTrans = registry.get<Engine::Core::TransformComponent>(rt.HeldOrbLight);
                auto& pLight = registry.get<Engine::Graphics::PointLightComponent>(rt.HeldOrbLight);

                bool isHeldOrFlying = false;
                XMFLOAT3 activeHeldPos = { 0.0f, 0.0f, 0.0f };
                for (const auto& orb : rt.Orbs) {
                    if (orb.Carrier == OrbCarrier::Player || orb.IsAnimating) {
                        isHeldOrFlying = true;
                        activeHeldPos = orb.WorldPosition;
                        break;
                    }
                }

                if (isHeldOrFlying) {
                    mTrans.LocalPosition = activeHeldPos;
                    lTrans.LocalPosition = activeHeldPos;
                    XMStoreFloat4(&mTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(
                        std::sin(rt.GameTime * 0.25f) * 0.12f,
                        rt.GameTime * 0.40f,
                        std::cos(rt.GameTime * 0.35f) * 0.10f
                    ));
                    pLight.Intensity = g_LuminousConfig.OrbLightIntensity;
                    pLight.Radius = g_LuminousConfig.OrbInfluenceRadius;
                    pLight.ShadowNearZ = g_LuminousConfig.OrbLightNearClip;
                    mTrans.IsDirty = true;
                    lTrans.IsDirty = true;
                } else {
                    mTrans.LocalPosition = { 0.0f, -100.0f, 0.0f };
                    lTrans.LocalPosition = { 0.0f, -100.0f, 0.0f };
                    pLight.Intensity = 0.0f;
                    mTrans.IsDirty = true;
                    lTrans.IsDirty = true;
                }
            }

            for (const auto& obj : rt.Stage.Objects) {
                if (obj.Category != AssetCategory::Pedestal) continue;

                bool hasOrb = false;
                XMFLOAT3 orbPos = obj.Position;
                for (const auto& orb : rt.Orbs) {
                    if (orb.Carrier == OrbCarrier::Pedestal && orb.PedestalInstanceId == obj.InstanceId && !orb.IsAnimating) {
                        hasOrb = true;
                        orbPos = orb.WorldPosition;
                        break;
                    }
                }


                auto oIt = rt.PedestalOrbEntities.find(obj.InstanceId);
                if (oIt != rt.PedestalOrbEntities.end() && registry.valid(oIt->second)) {
                    auto& oTrans = registry.get<Engine::Core::TransformComponent>(oIt->second);
                    if (hasOrb) {
                        oTrans.LocalPosition = XMFLOAT3(obj.Position.x, obj.Position.y + PEDESTAL_SOCKET_HEIGHT + std::sin(rt.GameTime * 2.2f) * 0.012f, obj.Position.z);
                        XMStoreFloat4(&oTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(
                            std::sin(rt.GameTime * 0.25f) * 0.12f,
                            rt.GameTime * 0.40f,
                            std::cos(rt.GameTime * 0.35f) * 0.10f
                        ));
                    } else {
                        oTrans.LocalPosition = { 0.0f, -100.0f, 0.0f };
                    }
                    oTrans.IsDirty = true;
                }

                auto lIt = rt.PedestalLightEntities.find(obj.InstanceId);
                if (lIt != rt.PedestalLightEntities.end() && registry.valid(lIt->second)) {
                    auto& pt = registry.get<Engine::Graphics::PointLightComponent>(lIt->second);
                    pt.Intensity = hasOrb ? g_LuminousConfig.OrbLightIntensity : 0.0f;
                    pt.Radius = g_LuminousConfig.OrbInfluenceRadius;
                    pt.ShadowNearZ = g_LuminousConfig.OrbLightNearClip;
                }
            }

            // 火の粉 (ふわりと上昇・螺旋拡散・明滅)
            for (auto& fp : rt.StageFlameParticles) {
                fp.Phase += dt * 2.2f;
                fp.LocalY += fp.Speed * dt;
                constexpr float maxH = 0.35f;
                if (fp.LocalY > maxH) {
                    fp.LocalY = 0.0f;
                }
                float normY = fp.LocalY / maxH;
                float alpha = 0.40f + 0.60f * std::sin(normY * XM_PI);
                float curR = fp.Radius * (0.65f + 0.55f * normY);

                fp.CurrentScale = fp.BaseScale * alpha;
                fp.CurrentPosition = XMFLOAT3(
                    fp.Origin.x + std::cos(fp.Phase) * curR,
                    fp.Origin.y + fp.LocalY,
                    fp.Origin.z + std::sin(fp.Phase) * curR
                );
            }

            // 6. 反転壁・反転床のリアルタイム光学判定とマテリアル同期
            LuminousPlaySyncPhaseVisuals(materialMgr, registry);

            // 7. ゴール誘導パーティクル
            //    所持中の宝玉からだけ放出する (取得アニメーション中は所持側として扱う)。
            //    放出済みの粒子は放出時点の軌道のまま寿命まで進む。
            XMFLOAT3 goalPos = { 0.0f, 0.0f, 0.0f };
            bool hasGoal = false;
            for (const auto& obj : rt.Stage.Objects) {
                if (obj.AssetId == "GoalChest") {
                    goalPos = XMFLOAT3(obj.Position.x, obj.Position.y + 0.6f, obj.Position.z);
                    hasGoal = true;
                    break;
                }
            }

            const OrbRuntimeState* heldOrb = nullptr;
            for (const auto& orb : rt.Orbs) {
                const bool isHeld = orb.IsAnimating
                    ? (orb.TargetCarrier == OrbCarrier::Player)
                    : (orb.Carrier == OrbCarrier::Player);
                if (isHeld) {
                    heldOrb = &orb;
                    break;
                }
            }

            const bool emitGuidance = hasGoal && heldOrb != nullptr && !player.IsStageCleared;
            rt.GuidanceStream.Update(dt, emitGuidance, heldOrb ? heldOrb->WorldPosition : goalPos, goalPos);

            // 8. ゴール宝箱開口・黄金光線励起・300フォトン噴水バースト・カメラ注視 (Phase 6)
            if (player.IsStageCleared) {
                rt.ChestAnimTimer += dt;

                if (!rt.ChestBurstTriggered) {
                    rt.ChestBurstTriggered = true;
                    // 宝箱を開けた瞬間にステージ BGM を素早くフェードアウト (クリアのファンファーレを際立たせる)
                    Engine::Audio::AudioEngine::Get().FadeOutBGM(LuminousAudioConfig::BGM_ChestOpenFadeOutSeconds);
                    Engine::Audio::AudioEngine::Get().Play3D(LuminousAudioConfig::SE_Chest_Open.FilePath, rt.GoalChestPos.x, rt.GoalChestPos.y, rt.GoalChestPos.z, 1.5f, 15.0f, LuminousAudioConfig::SE_Chest_Open.DefaultVolume, false);
                    Engine::Audio::AudioEngine::Get().Play3D(LuminousAudioConfig::SE_Chest_Burst.FilePath, rt.GoalChestPos.x, rt.GoalChestPos.y, rt.GoalChestPos.z, 1.5f, 15.0f, LuminousAudioConfig::SE_Chest_Burst.DefaultVolume, false);

                    for (size_t i = 0; i < rt.ChestBurstParticles.size(); ++i) {
                        auto& p = rt.ChestBurstParticles[i];
                        float ox = ((static_cast<float>(rand() % 200) - 100.0f) / 100.0f) * 0.25f;
                        float oz = ((static_cast<float>(rand() % 200) - 100.0f) / 100.0f) * 0.15f;
                        p.pos = XMFLOAT3(rt.GoalChestPos.x + ox, rt.GoalChestPos.y + 0.35f, rt.GoalChestPos.z + oz);
                        float angle = static_cast<float>(rand() % 360) * (XM_PI / 180.0f);
                        float hSpeed = 0.5f + static_cast<float>(rand() % 180) / 100.0f;
                        float vSpeed = 2.8f + static_cast<float>(rand() % 380) / 100.0f;
                        p.vel = XMFLOAT3(hSpeed * std::cos(angle), vSpeed, hSpeed * std::sin(angle));
                        p.life = 0.0f;
                        p.maxLife = 1.2f + static_cast<float>(rand() % 120) / 100.0f;
                        p.baseScale = 0.025f + static_cast<float>(rand() % 40) / 1000.0f;
                    }
                }

                for (auto& p : rt.ChestBurstParticles) {
                    if (p.life >= p.maxLife) continue;
                    p.life += dt;
                    p.pos.x += p.vel.x * dt;
                    p.pos.y += p.vel.y * dt;
                    p.pos.z += p.vel.z * dt;
                    p.vel.y -= 4.2f * dt;
                    p.vel.x *= (1.0f - 0.6f * dt);
                    p.vel.z *= (1.0f - 0.6f * dt);
                }

                // 宝箱の蓋の開口アニメーション (0.8秒 EaseOutCubic: 0度 -> +75度、背面ヒンジ (0, 0.414, +0.477) を軸に後方開口)
                // 注: MeshManager は aiProcess_MakeLeftHanded で読み込むため頂点Zが反転する。
                float tau = std::clamp(rt.ChestAnimTimer / 0.8f, 0.0f, 1.0f);
                float ease = 1.0f - std::pow(1.0f - tau, 3.0f);
                float angle = XMConvertToRadians(75.0f) * ease;
                constexpr float Hy = 0.414f;
                constexpr float Hz = 0.477f;
                float cosA = std::cos(angle);
                float sinA = std::sin(angle);
                float ldy = Hy - (Hy * cosA - Hz * sinA);
                float ldz = Hz - (Hy * sinA + Hz * cosA);

                if (registry.valid(rt.GoalChestLid)) {
                    auto& lidTrans = registry.get<Engine::Core::TransformComponent>(rt.GoalChestLid);
                    lidTrans.LocalPosition = XMFLOAT3(
                        rt.GoalChestPos.x + ldz * std::sin(rt.GoalChestYaw),
                        rt.GoalChestPos.y + ldy,
                        rt.GoalChestPos.z + ldz * std::cos(rt.GoalChestYaw)
                    );
                    XMStoreFloat4(&lidTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(angle, rt.GoalChestYaw, 0.0f));
                    lidTrans.IsDirty = true;
                }

                if (registry.valid(rt.GoalChestLight)) {
                    auto& cLight = registry.get<Engine::Graphics::PointLightComponent>(rt.GoalChestLight);
                    cLight.Intensity = 48.0f * ease;
                }

                if (rt.ChestAnimTimer >= 0.8f && !rt.ClearFanfarePlayed) {
                    rt.ClearFanfarePlayed = true;
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::Jingle_StageClear.FilePath, LuminousAudioConfig::Jingle_StageClear.DefaultVolume);
                    if (rt.StageNumber >= 1) {
                        ProfileManager::Get().RecordStageClear(
                            rt.StageNumber, rt.GameTime, player.OrbPickupCount, player.PedestalInsertCount);
                    }
                    rt.Cursor.Reset();
                }

                // カメラの視線を宝箱開口部へ滑らかに誘導 (デバッグ中はツールのカメラを優先)
                if (playerInput && registry.valid(rt.PlayerCamera)) {
                    auto& camTrans = registry.get<Engine::Core::TransformComponent>(rt.PlayerCamera);
                    float eyeX = player.Position.x;
                    float eyeY = player.Position.y + player.EyeHeight;
                    float eyeZ = player.Position.z;
                    float targetLookX = rt.GoalChestPos.x - eyeX;
                    float targetLookY = (rt.GoalChestPos.y + 0.4f) - eyeY;
                    float targetLookZ = rt.GoalChestPos.z - eyeZ;
                    float horizDist = std::sqrt(targetLookX * targetLookX + targetLookZ * targetLookZ);
                    if (horizDist > 0.01f) {
                        float desiredYaw = std::atan2(targetLookX, targetLookZ);
                        if (desiredYaw < 0.0f) desiredYaw += XM_2PI;
                        float desiredPitch = -std::atan2(targetLookY, horizDist);

                        float yawDelta = desiredYaw - player.Yaw;
                        while (yawDelta > XM_PI)  yawDelta -= XM_2PI;
                        while (yawDelta < -XM_PI) yawDelta += XM_2PI;

                        float camFactor = 1.0f - std::exp(-4.5f * dt);
                        player.Yaw += yawDelta * camFactor;
                        player.Pitch += (desiredPitch - player.Pitch) * camFactor;

                        camTrans.LocalPosition = XMFLOAT3(eyeX, eyeY, eyeZ);
                        XMStoreFloat4(&camTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(player.Pitch, player.Yaw, 0.0f));
                        camTrans.IsDirty = true;
                    }
                }
            }
        }

        // パーティクルはポーズ中も描き続ける
        LuminousPlaySubmitParticles(registry);

        // デバッグツールの ImGui (NewFrame 済みのフレームだけ)
        if (debugActive) {
            if (auto* es = Engine::Core::FindService<Engine::Core::EngineStateData>(registry)) {
                if (es->ImGuiFrameActive) {
                    auto dctx = LuminousPlayDebugContext(registry);
                    rt.DebugTool.DrawUI(dctx, realDt);
                }
            }
        }

        // HUDの画像描画
        LuminousPlayRenderHUD(ctx);
    }

    inline void LuminousPlayRenderHUD(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        auto& rt = PlayRuntime(registry);
        auto& player = PlayerState(registry);
        auto& ui = Engine::Graphics::UIRenderer::Get();
        const float screenW = 1920.0f;
        const float screenH = 1080.0f;
        const float centerX = screenW * 0.5f;
        const float centerY = screenH * 0.5f;
        const float realDt = ctx.GetDeltaTime();

        Vector2 mPos = Input::GetMousePosition();
        bool isMouseDown = Input::GetKeyHold(KeyCode::MOUSE_LEFT);
        bool isMouseClick = Input::GetKeyDown(KeyCode::MOUSE_LEFT);

        const bool debugActive = rt.DebugTool.IsActive();
        const bool showGameplayHUD = !debugActive || rt.DebugTool.ShowGameHUD();
        const bool playing = !rt.Paused && !player.IsStageCleared;

        if (showGameplayHUD && playing) {
            // 1. レティクル
            ui.DrawImage(LuminousUIConfig::Image_HUD_Crosshair.FilePath, centerX - 32.0f, centerY - 32.0f, 64.0f, 64.0f);

            // 2. インタラクト操作説明 (枠 + 操作中デバイスのボタン画像 + 行動名)
            //    置く / 取る / 開ける で行動名の画像を出し分ける。
            if (player.CanInteract && player.InteractKind != LuminousInteractKind::None) {
                const UIImageConfig* label = &LuminousUIConfig::Image_HUD_Label_TakeOrb;
                if (player.InteractKind == LuminousInteractKind::PlaceOrb) label = &LuminousUIConfig::Image_HUD_Label_PlaceOrb;
                else if (player.InteractKind == LuminousInteractKind::OpenChest) label = &LuminousUIConfig::Image_HUD_Label_OpenChest;

                const auto& frame = LuminousUIConfig::Image_HUD_PromptFrame;
                const float fx = centerX - frame.DefaultWidth * 0.5f;
                const float fy = screenH - 196.0f;
                ui.DrawImage(frame.FilePath, fx, fy, frame.DefaultWidth, frame.DefaultHeight);

                const float gh = 56.0f;
                const float gw = gh * LuminousInputDevice::GlyphAspect(LuminousButtonGlyph::Interact);
                const float gx = fx + 26.0f;
                const float gy = fy + (frame.DefaultHeight - gh) * 0.5f;
                ui.DrawImage(LuminousInputDevice::GlyphPath(LuminousButtonGlyph::Interact), gx, gy, gw, gh);
                ui.DrawImage(label->FilePath, gx + gw + 22.0f, fy + (frame.DefaultHeight - label->DefaultHeight) * 0.5f,
                    label->DefaultWidth, label->DefaultHeight);
            }

            // 3. 宝玉所持アイコン (画面右下)
            if (player.IsHoldingOrb) {
                ui.DrawImage(LuminousUIConfig::Image_HUD_OrbIcon.FilePath, screenW - 96.0f, screenH - 96.0f, 64.0f, 64.0f,
                    { 1.0f, 0.90f, 0.45f, 1.0f });
            }

            // 4. 操作説明の案内 (画面左下)
            DrawControlHint(28.0f, screenH - 62.0f, LuminousButtonGlyph::Pause, "PAUSE / CONTROLS", 34.0f);
        }

        // 5. ポーズメニュー
        if (rt.Paused && !player.IsStageCleared && !debugActive) {
            Input::SetCursorLocked(false);
            ui.DrawPanel(0.0f, 0.0f, screenW, screenH, { 0.0f, 0.02f, 0.05f, 0.66f });

            const float panelW = 540.0f;
            const float panelH = 620.0f;
            const float panelX = centerX - panelW * 0.5f - 200.0f;
            const float panelY = centerY - panelH * 0.5f;
            ui.DrawImage(LuminousUIConfig::Image_Pause_Panel.FilePath, panelX, panelY, panelW, panelH);

            struct Item { const UIImageConfig* Normal; const UIImageConfig* Hover; };
            const Item items[] = {
                { &LuminousUIConfig::Image_Btn_Resume_Normal,      &LuminousUIConfig::Image_Btn_Resume_Hover },
                { &LuminousUIConfig::Image_Btn_Restart_Normal,     &LuminousUIConfig::Image_Btn_Restart_Hover },
                { &LuminousUIConfig::Image_Btn_StageSelect_Normal, &LuminousUIConfig::Image_Btn_StageSelect_Hover },
                { &LuminousUIConfig::Image_Btn_Editor_Normal,      &LuminousUIConfig::Image_Btn_Editor_Hover },
                { &LuminousUIConfig::Image_Btn_Title_Normal,       &LuminousUIConfig::Image_Btn_Title_Hover },
            };
            constexpr int itemCount = 5;
            rt.Cursor.BeginFrame(realDt, itemCount, mPos);
            const Vector2 menuMouse = rt.Cursor.Mouse();

            const float btnW = 420.0f;
            const float btnH = 68.0f;
            const float btnX = panelX + (panelW - btnW) * 0.5f;
            const float btnStartY = panelY + 124.0f;
            const float btnGap = 90.0f;

            int activated = -1;
            for (int i = 0; i < itemCount; ++i) {
                const float by = btnStartY + i * btnGap;
                rt.Cursor.Item(i, btnX, by, btnW, btnH);
                if (ui.DrawButton(items[i].Normal->FilePath, items[i].Hover->FilePath, items[i].Hover->FilePath,
                    btnX, by, btnW, btnH, menuMouse.x, menuMouse.y, isMouseDown, isMouseClick, nullptr,
                    rt.Cursor.Is(i), rt.Cursor.Activated(i))) {
                    activated = i;
                }
            }
            if (rt.Cursor.EndFrame()) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            }

            // 戻るボタンでも再開
            if (activated < 0 && LuminousInput::CancelPressed()) activated = 0;

            switch (activated) {
            case 0:
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                rt.Paused = false;
                Input::SetCursorLocked(true);
                return;
            case 1:
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(rt.Stage, rt.ReturnToEditorOnExit, rt.StageNumber));
                return;
            case 2:
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>(LuminousTitleScene::TitleView::StageSelect));
                return;
            case 3:
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousEditorScene>(rt.Stage));
                return;
            case 4:
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>());
                return;
            default:
                break;
            }

            // 操作一覧 (操作中のデバイスに合わせたボタン画像)
            const float cx = panelX + panelW + 32.0f;
            const float cy = panelY;
            const float cw = 420.0f;
            const float ch = panelH;
            ui.DrawPanel(cx, cy, cw, ch, { 0.03f, 0.055f, 0.10f, 0.82f });
            DrawUIFrame(cx, cy, cw, ch, 1.0f, { 0.86f, 0.96f, 0.99f, 0.35f });
            ui.DrawString("CONTROLS", cx + 34.0f, cy + 30.0f, 26.0f, { 0.86f, 0.96f, 0.99f, 1.0f });
            ui.DrawPanel(cx + 34.0f, cy + 72.0f, cw - 68.0f, 1.0f, { 0.86f, 0.96f, 0.99f, 0.45f });
            ui.DrawString(LuminousInputDevice::SetLabel(LuminousInputDevice::Current), cx + 34.0f, cy + 84.0f, 16.0f, { 0.59f, 0.77f, 0.85f, 0.95f });

            struct Row { LuminousButtonGlyph Glyph; const char* Label; };
            const Row rows[] = {
                { LuminousButtonGlyph::Move,     "MOVE" },
                { LuminousButtonGlyph::Look,     "LOOK" },
                { LuminousButtonGlyph::Interact, "TAKE / PLACE ORB" },
                { LuminousButtonGlyph::Pause,    "PAUSE" },
                { LuminousButtonGlyph::Confirm,  "DECIDE (MENU)" },
                { LuminousButtonGlyph::Cancel,   "BACK (MENU)" },
            };
            float ry = cy + 132.0f;
            for (const auto& row : rows) {
                DrawControlHint(cx + 34.0f, ry, row.Glyph, row.Label, 52.0f);
                ry += 76.0f;
            }
        }

        // 6. ステージクリアのリザルト (宝箱開口0.8秒後)
        if (player.IsStageCleared && rt.ChestAnimTimer >= 0.8f && (!debugActive || rt.DebugTool.ShowGameHUD())) {
            if (!debugActive) Input::SetCursorLocked(false);

            ui.DrawPanel(0.0f, 0.0f, screenW, screenH, { 0.0f, 0.02f, 0.05f, 0.72f });

            // バナー (160) + 間隔 (10) + 成績パネル (480) + 間隔 (40) + 操作説明 (38) を画面の縦中央へ
            const float bannerH = 160.0f;
            const float panelW = 640.0f;
            const float panelH = 480.0f;
            const float blockH = bannerH + 10.0f + panelH + 40.0f + 38.0f;
            const float blockTop = (screenH - blockH) * 0.5f;
            ui.DrawImage(LuminousUIConfig::Image_Clear_Banner.FilePath, centerX - 400.0f, blockTop, 800.0f, bannerH);

            const float panelX = centerX - panelW * 0.5f;
            const float panelY = blockTop + bannerH + 10.0f;
            ui.DrawImage(LuminousUIConfig::Image_Clear_StatsPanel.FilePath, panelX, panelY, panelW, panelH);

            ui.DrawString("Stage: " + (rt.Stage.Name.empty() ? "Custom Stage" : rt.Stage.Name),
                panelX + 60.0f, panelY + 95.0f, 22.0f, { 0.47f, 0.84f, 0.98f, 1.0f });

            const int clearMinutes = static_cast<int>(rt.GameTime) / 60;
            const float clearSeconds = std::fmod(rt.GameTime, 60.0f);
            ui.DrawString(std::format("Clear Time:         {:02d}:{:05.2f}", clearMinutes, clearSeconds),
                panelX + 60.0f, panelY + 140.0f, 20.0f, { 0.95f, 0.97f, 1.0f, 1.0f });
            ui.DrawString(std::format("Orb Pickups:        {}", player.OrbPickupCount),
                panelX + 60.0f, panelY + 185.0f, 20.0f, { 0.95f, 0.97f, 1.0f, 1.0f });
            ui.DrawString(std::format("Pedestal Inserts:   {}", player.PedestalInsertCount),
                panelX + 60.0f, panelY + 230.0f, 20.0f, { 0.95f, 0.97f, 1.0f, 1.0f });

            struct Btn { const UIImageConfig* Normal; const UIImageConfig* Hover; float X, Y, W, H; };
            const Btn btns[] = {
                { &LuminousUIConfig::Image_Btn_Next_Normal,        &LuminousUIConfig::Image_Btn_Next_Hover,        panelX + 30.0f,  panelY + 290.0f, 280.0f, 58.0f },
                { &LuminousUIConfig::Image_Btn_Retry_Normal,       &LuminousUIConfig::Image_Btn_Retry_Hover,       panelX + 330.0f, panelY + 290.0f, 280.0f, 58.0f },
                { &LuminousUIConfig::Image_Btn_StageSelect_Normal, &LuminousUIConfig::Image_Btn_StageSelect_Hover, panelX + 30.0f,  panelY + 380.0f, 280.0f, 58.0f },
                { &LuminousUIConfig::Image_Btn_Title_Normal,       &LuminousUIConfig::Image_Btn_Title_Hover,       panelX + 330.0f, panelY + 380.0f, 280.0f, 58.0f },
            };
            constexpr int btnCount = 4;
            rt.Cursor.BeginFrame(realDt, btnCount, mPos);
            const Vector2 menuMouse = rt.Cursor.Mouse();

            // NEXT / RETRY が上段、STAGE SELECT / TITLE が下段の 2x2。
            // 矩形を先に登録しておくと、下を押せば真下のボタンへ移動する。
            for (int i = 0; i < btnCount; ++i) {
                rt.Cursor.Item(i, btns[i].X, btns[i].Y, btns[i].W, btns[i].H);
            }

            int activated = -1;
            for (int i = 0; i < btnCount; ++i) {
                if (ui.DrawButton(btns[i].Normal->FilePath, btns[i].Hover->FilePath, btns[i].Hover->FilePath,
                    btns[i].X, btns[i].Y, btns[i].W, btns[i].H, menuMouse.x, menuMouse.y, isMouseDown, isMouseClick, nullptr,
                    rt.Cursor.Is(i), rt.Cursor.Activated(i))) {
                    activated = i;
                }
            }
            if (rt.Cursor.EndFrame()) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            }

            if (activated >= 0) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            }
            switch (activated) {
            case 0: {
                const int total = LuminousStageCatalog::Count();
                if (rt.StageNumber >= 1 && rt.StageNumber < total) {
                    // 登録ステージの次へ
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(
                        LuminousStageCatalog::Load(rt.StageNumber), rt.ReturnToEditorOnExit, rt.StageNumber + 1));
                } else if (rt.StageNumber >= total && total > 0) {
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>(LuminousTitleScene::TitleView::StageSelect));
                } else if (rt.Stage.Name.find("Stage 1") != std::string::npos) {
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(LuminousStage::CreateMultiFloorSampleStage(), rt.ReturnToEditorOnExit));
                } else {
                    LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(LuminousStage::CreateSampleStage(), rt.ReturnToEditorOnExit));
                }
                return;
            }
            case 1:
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(rt.Stage, rt.ReturnToEditorOnExit, rt.StageNumber));
                return;
            case 2:
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>(LuminousTitleScene::TitleView::StageSelect));
                return;
            case 3:
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>());
                return;
            default:
                break;
            }

            const float hintH = 38.0f;
            const float total = MeasureControlHint(LuminousButtonGlyph::Move, "CHOOSE", hintH)
                              + MeasureControlHint(LuminousButtonGlyph::Confirm, "DECIDE", hintH);
            float hx = centerX - total * 0.5f;
            hx += DrawControlHint(hx, panelY + panelH + 40.0f, LuminousButtonGlyph::Move, "CHOOSE", hintH);
            DrawControlHint(hx, panelY + panelH + 40.0f, LuminousButtonGlyph::Confirm, "DECIDE", hintH);
        }

        // 7. ステージ開始オーバーレイ (画面中央)
        rt.StageIntro.Draw(screenW, screenH);

        // 8. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(screenW, screenH);
    }

    // ========================================================================
    // LuminousEditorScene 実装
    // ========================================================================
    inline void LuminousEditorScene::OnSetup(Engine::Core::SceneContext& ctx) {
        // 持ち物をレジストリに作る (シーンが終わるときに一緒に片付く)
        const entt::entity editorRuntimeEnt = ctx.CreateEntity();
        ctx.GetRegistry().emplace<Engine::Core::GeneratedTag>(editorRuntimeEnt);
        auto& rt = ctx.GetRegistry().emplace<LuminousEditorRuntime>(editorRuntimeEnt);
        rt.ActivePaletteCategory = pendingCategory_;
        if (hasPendingStage_) rt.Editor.Stage = std::move(pendingStage_);
        auto& registry = ctx.GetRegistry();

        // 編集に欠かせないもの (カメラ・環境光・下見のゴースト)。
        //   データだけのシーンから起こすときと同じ道を通す
        LuminousEditorEnsureCoreEntities(registry);

        // ステージエンティティの生成
        LuminousEditorRefreshSceneEntities(registry);

        // 毎フレーム更新
        ctx.RegisterSystem(Engine::Core::SystemPhase::Update, "LuminousEditorUpdateSystem",
                           &LuminousEditorUpdate);
    }

    inline void LuminousEditorScene::OnTeardown(Engine::Core::SceneContext& ctx) {
        auto& rt = EditorRuntime(ctx.GetRegistry());
        Input::SetCursorLocked(false);
        auto& registry = ctx.GetRegistry();
        if (auto* debug = ctx.FindService<Engine::Debug::DebugSettings>()) {
            debug->ShowEngineControlPanel = rt.SavedShowEnginePanel;
        }

        auto DestroyWithSubMeshes = [&](entt::entity e) {
            if (!registry.valid(e)) return;
            if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(e)) {
                entt::entity sub = rootComp->FirstSubMesh;
                while (sub != entt::null && registry.valid(sub)) {
                    entt::entity nextSub = registry.get<Engine::Graphics::SubMeshComponent>(sub).NextSubMesh;
                    registry.destroy(sub);
                    sub = nextSub;
                }
                rootComp->FirstSubMesh = entt::null;
            }
            registry.destroy(e);
        };

        if (rt.PreviewGhost != entt::null && registry.valid(rt.PreviewGhost)) {
            DestroyWithSubMeshes(rt.PreviewGhost);
            rt.PreviewGhost = entt::null;
        }
        for (auto& pair : rt.LightEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.LightEntities.clear();
        for (auto& pair : rt.FlameEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.FlameEntities.clear();
        for (auto& pair : rt.ObjectEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.ObjectEntities.clear();
    }

    // ------------------------------------------------------------------------
    // 編集に欠かせないものを揃える (カメラ・環境光・下見のゴースト)。
    //
    //   **もう在れば何もしない**。コードのシーンから来ても、
    //   .scene.json から起こしても、同じここを通る。
    //   どれもステージから組み直せる派生物なので GeneratedTag を付ける
    // ------------------------------------------------------------------------
    inline void LuminousEditorEnsureCoreEntities(entt::registry& registry) {
        auto& rt = EditorRuntime(registry);
        if (rt.Camera != entt::null && registry.valid(rt.Camera)) return;

        // 暗転した状態で始め、マテリアルのコンパイルが済んでから明転する
        LuminousTransition::Get().BeginSceneEnter();
        Input::SetCursorLocked(false);
        Input::SetCursorHidden(false);   // エディタはマウス前提
        if (auto* debug = Engine::Core::FindService<Engine::Debug::DebugSettings>(registry)) {
            rt.SavedShowEnginePanel = debug->ShowEngineControlPanel;
            debug->ShowEngineControlPanel = false;
        }

        // カメラエンティティの作成
        rt.Camera = CreateGeneratedEntity(registry);
        auto& camTrans = registry.emplace<Engine::Core::TransformComponent>(rt.Camera);
        camTrans.LocalPosition = { rt.Editor.CameraFocus.x, rt.Editor.CameraDistance, rt.Editor.CameraFocus.z };
        XMStoreFloat4(&camTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(89.0f), 0.0f, 0.0f));
        registry.emplace<Engine::Graphics::CameraComponent>(rt.Camera, Engine::Graphics::CameraComponent{
            .FovY = XMConvertToRadians(50.0f),
            .AspectRatio = 1920.0f / 1080.0f,
            .NearZ = 0.2f,
            .FarZ = 400.0f
        });
        Engine::Graphics::CameraAPI::SetActiveCamera(registry, rt.Camera);

        // 環境光ライト
        auto sunEnt = CreateGeneratedEntity(registry);
        auto& sunTrans = registry.emplace<Engine::Core::TransformComponent>(sunEnt);
        sunTrans.LocalPosition = { 10.0f, 40.0f, 10.0f };
        XMStoreFloat4(&sunTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(60.0f), XMConvertToRadians(45.0f), 0.0f));
        registry.emplace<Engine::Graphics::DirectionalLightComponent>(sunEnt, Engine::Graphics::DirectionalLightComponent{
            .Color = { 0.95f, 0.98f, 1.0f },
            .Intensity = 3.5f,
            .CastShadows = false
        });

        // 既存オブジェクトマップの初期化
        rt.ObjectEntities.clear();
        rt.PreviewGhost = entt::null;
        rt.CurrentGhostAsset = "";
        rt.CurrentGhostConflict = false;

        // 3Dプレビュー用ゴーストエンティティ (SceneScopeTag付与)
        rt.PreviewGhost = CreateGeneratedEntity(registry);
        auto& ghostTrans = registry.emplace<Engine::Core::TransformComponent>(rt.PreviewGhost);
        ghostTrans.LocalPosition = { 0.0f, -9999.0f, 0.0f };
        ghostTrans.LocalScale = { 1.0f, 1.0f, 1.0f };
        ghostTrans.IsDirty = true;
    }

    // ------------------------------------------------------------------------
    // ステージエディタを建てる (名前つきのシステム)。
    //
    //   .scene.json の "systems" に並べておけば、シーンの C++ が無くても開く。
    //   本編の LuminousStageBuildSystem と同じ形
    // ------------------------------------------------------------------------
    inline void LuminousEditorBuildSystem(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        auto& rt = EditorRuntime(registry);
        if (rt.Camera != entt::null && registry.valid(rt.Camera)) return;   // もう建っている
        // シーンのファイルが「どの並びを開いて始めるか」を言っていれば、それに従う
        //   (コードのシーンでは作るときの引数だったもの。計画 18 の段 2)
        rt.ActivePaletteCategory = SceneSetting<LuminousEditorSettings>(registry).PaletteCategory;
        LuminousEditorEnsureCoreEntities(registry);
        LuminousEditorRefreshSceneEntities(registry);
    }

    inline void LuminousEditorRefreshSceneEntities(entt::registry& registry) {
        auto& rt = EditorRuntime(registry);
        auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        auto* meshMgr = Engine::Core::GetService<Engine::Graphics::MeshManager>(registry);

        auto DestroyWithSubMeshes = [&](entt::entity e) {
            if (!registry.valid(e)) return;
            if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(e)) {
                entt::entity sub = rootComp->FirstSubMesh;
                while (sub != entt::null && registry.valid(sub)) {
                    entt::entity nextSub = registry.get<Engine::Graphics::SubMeshComponent>(sub).NextSubMesh;
                    registry.destroy(sub);
                    sub = nextSub;
                }
                rootComp->FirstSubMesh = entt::null;
            }
            registry.destroy(e);
        };

        for (auto& pair : rt.LightEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.LightEntities.clear();
        for (auto& pair : rt.FlameEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.FlameEntities.clear();
        for (auto& pair : rt.ObjectEntities) {
            DestroyWithSubMeshes(pair.second);
        }
        rt.ObjectEntities.clear();

        auto normalMat     = CreatePBRMaterialPrefab(materialMgr, { 0.55f, 0.55f, 0.58f }, 0.7f, 0.1f);
        auto phaseMat      = CreatePBRMaterialPrefab(materialMgr, { 0.30f, 0.70f, 1.0f }, 0.2f, 0.1f, { 0.4f, 0.7f, 1.0f }, false); // 反転マテリアルは影を落とさない
        auto pedMat        = CreatePBRMaterialPrefabTwoSided(materialMgr, { 0.75f, 0.65f, 0.30f }, 0.4f, 0.2f, { 0.3f, 0.2f, 0.0f });
        auto selectMat     = CreatePBRMaterialPrefab(materialMgr, { 1.0f, 0.85f, 0.15f }, 0.2f, 0.3f, { 0.8f, 0.6f, 0.1f });
        auto onionSkinMat  = CreateTranslucentMaterialPrefab(materialMgr, { 0.35f, 0.50f, 0.75f, 0.38f });
        auto upperFloorMat = CreateTranslucentMaterialPrefab(materialMgr, { 0.60f, 0.60f, 0.65f, 0.22f });

        for (const auto& obj : rt.Editor.Stage.Objects) {
            const auto* flr = rt.Editor.Stage.FindFloor(obj.FloorIndex);
            if (flr && !flr->Visible) {
                continue;
            }

            if (rt.Editor.CameraMode == EditorCameraMode::TopDown && obj.FloorIndex > rt.Editor.CurrentFloor) {
                continue;
            }

            auto ent = registry.create();
            registry.emplace<Engine::Core::SceneScopeTag>(ent);
            auto& trans = registry.emplace<Engine::Core::TransformComponent>(ent);
            trans.LocalPosition = obj.Position;
            XMStoreFloat4(&trans.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(obj.Rotation.y), 0.0f));
            trans.LocalScale = obj.Scale;

            std::string modelPath = "Assets/Models/Dungeon/" + obj.AssetId + ".obj";
            auto mesh = meshMgr->GetOrLoadMesh(modelPath);

            if (rt.Editor.ToolMode == EditorToolMode::Select && rt.Editor.SelectedInstanceId == obj.InstanceId) {
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, selectMat);
            } else if (obj.FloorIndex < rt.Editor.CurrentFloor) {
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, onionSkinMat);
            } else if (obj.FloorIndex > rt.Editor.CurrentFloor) {
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, upperFloorMat);
            } else {
                if (obj.Category == AssetCategory::Pedestal) {
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, pedMat);
                } else if (obj.Phase == MaterialPhase::Phase) {
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, phaseMat);
                } else {
                    auto subPrefabs = GetAssetSubmeshPrefabs(obj.AssetId, materialMgr);
                    ApplyOrbOccluderTags(obj.AssetId, obj.Category, obj.Phase, subPrefabs);
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, ent, mesh, subPrefabs);
                }
            }

            if (obj.HasLight) {
                XMFLOAT3 flamePos = obj.Position;
                XMFLOAT3 flameScale = { 0.45f, 0.72f, 0.45f };

                if (obj.AssetId == "Prop_Torch") {
                    float radY = XMConvertToRadians(obj.Rotation.y);
                    constexpr float tipOffsetY = 0.5512f;
                    constexpr float tipOffsetZ = -0.1101f;
                    flamePos = XMFLOAT3(
                        obj.Position.x + tipOffsetZ * std::sin(radY),
                        obj.Position.y + tipOffsetY,
                        obj.Position.z + tipOffsetZ * std::cos(radY)
                    );
                    flameScale = { 0.45f, 0.72f, 0.45f };
                } else if (obj.AssetId == "Prop_Candle") {
                    flamePos = XMFLOAT3(obj.Position.x, obj.Position.y + 0.24f, obj.Position.z);
                    flameScale = { 0.15f, 0.24f, 0.15f };
                } else {
                    flamePos.y += 0.35f;
                }

                auto lEnt = registry.create();
                registry.emplace<Engine::Core::SceneScopeTag>(lEnt);
                registry.emplace<Engine::Core::TransformComponent>(lEnt, Engine::Core::TransformComponent{
                    .LocalPosition = flamePos
                });
                registry.emplace<Engine::Graphics::PointLightComponent>(lEnt, Engine::Graphics::PointLightComponent{
                    .Color = obj.LightColor,
                    .Intensity = obj.LightIntensity,
                    .Radius = obj.LightRadius,
                    .CastShadows = true,
                    .ShadowNearZ = g_LuminousConfig.PropLightNearClip
                });
                rt.LightEntities[obj.InstanceId] = lEnt;

                auto flameMesh = meshMgr->GetOrLoadMesh("Assets/Models/Dungeon/LuminousOrb.obj");
                auto flameMat  = CreateFlameEmissivePrefab(materialMgr, obj.LightColor, obj.LightIntensity);
                auto fEnt = registry.create();
                registry.emplace<Engine::Core::SceneScopeTag>(fEnt);
                registry.emplace<Engine::Core::TransformComponent>(fEnt, Engine::Core::TransformComponent{
                    .LocalPosition = flamePos,
                    .LocalScale = flameScale
                });
                Engine::Graphics::MeshAttacher::AttachToEntity(registry, fEnt, flameMesh, flameMat);
                rt.FlameEntities[obj.InstanceId] = fEnt;
            }

            // ステージの中身を**そのエンティティに載せる**。
            //   これでインスペクターから種類・相 (Phase)・光源をそのまま直せる。
            //   以前はシーンが抱える std::vector の中だけにあり、見えなかった
            registry.emplace<PlacedObject>(ent, obj);
            // **ステージの JSON から組み立てたもの**なので、シーンのファイルには
            //   書き出さない (E-05)。正本はステージの方で、ここは組み直せる結果
            registry.emplace<Engine::Core::GeneratedTag>(ent);

            rt.ObjectEntities[obj.InstanceId] = ent;
        }
    }

    inline void LuminousEditorUpdate(const Engine::Core::SystemContext& ctx) {
        auto& rt = EditorRuntime(ctx.GetRegistry());
        float dt = ctx.GetDeltaTime();
        rt.Editor.UpdateStatusTimer(dt);

        auto& registry = ctx.GetRegistry();

        // シーン遷移フェードの更新。暗転しきったところで実際の切り替えを発行する
        // (マテリアルの非同期コンパイル完了待ちも Tick 内で行う)
        LuminousInputDevice::Update();
        LuminousTransition::Get().Tick(dt, &registry);
        if (auto pendingScene = LuminousTransition::Get().TakePendingScene()) {
            ctx.RequestSceneLoad(pendingScene);
            // このフレームは以降の描画を行わないため、暗幕だけはここで描いておく。
            // 描かないと切り替え直前の 1 フレームだけ素通しになり、チラつく。
            LuminousTransition::Get().Draw(1920.0f, 1080.0f);
            return;
        }
        auto* materialMgr = Engine::Core::GetService<Engine::Graphics::MaterialManager>(registry);
        auto* meshMgr = Engine::Core::GetService<Engine::Graphics::MeshManager>(registry);

        // 0. モード・カメラ切り替えショートカット
        if (Input::GetKeyDown(KeyCode::NUM_1)) {
            if (rt.Editor.ToolMode != EditorToolMode::Paint) {
                rt.Editor.ToolMode = EditorToolMode::Paint;
                rt.Editor.SetStatusMessage("Mode: Paint Brush (1)");
                LuminousEditorRefreshSceneEntities(registry);
            }
        }
        if (Input::GetKeyDown(KeyCode::NUM_2)) {
            if (rt.Editor.ToolMode != EditorToolMode::Select) {
                rt.Editor.ToolMode = EditorToolMode::Select;
                rt.Editor.SetStatusMessage("Mode: Object Select (2)");
                LuminousEditorRefreshSceneEntities(registry);
            }
        }
        if (Input::GetKeyDown(KeyCode::TAB)) {
            rt.Editor.ToggleCameraMode();
            if (rt.Editor.CameraMode == EditorCameraMode::TopDown) {
                Input::SetCursorLocked(false);
            }
            LuminousEditorRefreshSceneEntities(registry);
        }

        // フロア階層ショートカット
        if (Input::GetKeyDown(KeyCode::PAGE_UP)) {
            if (rt.Editor.CurrentFloor < rt.Editor.Stage.GetMaxFloor()) {
                rt.Editor.CurrentFloor++;
                LuminousEditorRefreshSceneEntities(registry);
            }
        }
        if (Input::GetKeyDown(KeyCode::PAGE_DOWN)) {
            if (rt.Editor.CurrentFloor > rt.Editor.Stage.GetMinFloor()) {
                rt.Editor.CurrentFloor--;
                LuminousEditorRefreshSceneEntities(registry);
            }
        }
        if (Input::GetKeyHold(KeyCode::CTRL) && Input::GetKeyDown(KeyCode::UP)) {
            rt.Editor.AddFloorAbove();
            LuminousEditorRefreshSceneEntities(registry);
        }
        if (Input::GetKeyHold(KeyCode::CTRL) && Input::GetKeyDown(KeyCode::DOWN)) {
            rt.Editor.AddFloorBelow();
            LuminousEditorRefreshSceneEntities(registry);
        }
        if (Input::GetKeyHold(KeyCode::ALT) && Input::GetKeyDown(KeyCode::UP)) {
            if (rt.Editor.MoveCurrentFloorUp()) {
                LuminousEditorRefreshSceneEntities(registry);
            }
        }
        if (Input::GetKeyHold(KeyCode::ALT) && Input::GetKeyDown(KeyCode::DOWN)) {
            if (rt.Editor.MoveCurrentFloorDown()) {
                LuminousEditorRefreshSceneEntities(registry);
            }
        }

        // 1. カメラ更新 (TopDown / Flycam3D)
        XMMATRIX viewMatrix;
        XMMATRIX projMatrix;
        if (rt.Editor.CameraMode == EditorCameraMode::TopDown) {
            rt.Editor.UpdateCamera(dt);
            if (registry.valid(rt.Camera)) {
                auto& cTrans = registry.get<Engine::Core::TransformComponent>(rt.Camera);
                cTrans.LocalPosition = XMFLOAT3(rt.Editor.CameraFocus.x, rt.Editor.CameraDistance, rt.Editor.CameraFocus.z);
                XMStoreFloat4(&cTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(89.0f), 0.0f, 0.0f));
                cTrans.IsDirty = true;
            }
            viewMatrix = XMMatrixLookToLH(
                XMVectorSet(rt.Editor.CameraFocus.x, rt.Editor.CameraDistance, rt.Editor.CameraFocus.z, 1.0f),
                XMVectorSet(0.0f, -1.0f, 0.0001f, 0.0f),
                XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f)
            );
            projMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(50.0f), 1920.0f / 1080.0f, 0.2f, 400.0f);
        } else {
            rt.Editor.UpdateFlycam(dt);
            if (registry.valid(rt.Camera)) {
                auto& cTrans = registry.get<Engine::Core::TransformComponent>(rt.Camera);
                cTrans.LocalPosition = rt.Editor.FlycamPos;
                XMStoreFloat4(&cTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(rt.Editor.FlycamPitch), XMConvertToRadians(rt.Editor.FlycamYaw), 0.0f));
                cTrans.IsDirty = true;
            }
            float pitchRad = XMConvertToRadians(rt.Editor.FlycamPitch);
            float yawRad = XMConvertToRadians(rt.Editor.FlycamYaw);
            XMVECTOR forward = XMVectorSet(
                cosf(pitchRad) * sinf(yawRad),
                -sinf(pitchRad),
                cosf(pitchRad) * cosf(yawRad),
                0.0f
            );
            XMVECTOR eye = XMVectorSet(rt.Editor.FlycamPos.x, rt.Editor.FlycamPos.y, rt.Editor.FlycamPos.z, 1.0f);
            viewMatrix = XMMatrixLookToLH(eye, forward, XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
            projMatrix = XMMatrixPerspectiveFovLH(XMConvertToRadians(60.0f), 1920.0f / 1080.0f, 0.2f, 400.0f);

            // Flycam3D時の右クリック視点回転ロック制御
            if (Input::GetKeyHold(KeyCode::MOUSE_RIGHT)) {
                Input::SetCursorLocked(true);
            } else {
                Input::SetCursorLocked(false);
            }
        }

#ifndef IMGUI_DISABLE
        auto& io = ImGui::GetIO();
        // ImGui はクライアント領域の実ピクセル座標、3D ピッキングはゲームの仮想解像度で扱う
        // (フルスクリーンで黒帯が付いても両者がずれないようにする)
        Vector2 mousePos = Input::GetMouseClientPosition();
        io.AddMousePosEvent(mousePos.x, mousePos.y);
        io.AddMouseButtonEvent(0, Input::GetKeyHold(KeyCode::MOUSE_LEFT));
        io.AddMouseButtonEvent(1, Input::GetKeyHold(KeyCode::MOUSE_RIGHT));
        bool isHoveringUI = io.WantCaptureMouse || (mousePos.y <= 68.0f) || (mousePos.x <= 365.0f && mousePos.y >= 70.0f) || (mousePos.x >= io.DisplaySize.x - 440.0f);
        const auto& viewMapping = Input::GetViewportMapping();
        int screenW = (std::max)(1, static_cast<int>(viewMapping.VirtualWidth));
        int screenH = (std::max)(1, static_cast<int>(viewMapping.VirtualHeight));
#else
        Vector2 mousePos = Input::GetMousePosition();
        bool isHoveringUI = (mousePos.y <= 68.0f) || (mousePos.x <= 365.0f && mousePos.y >= 70.0f);
        int screenW = 1920;
        int screenH = 1080;
#endif

        // 3. Undo / Redo
        if (Input::GetKeyHold(KeyCode::CTRL)) {
            if (Input::GetKeyDown(KeyCode::Z)) {
                if (rt.Editor.Undo()) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            } else if (Input::GetKeyDown(KeyCode::Y)) {
                if (rt.Editor.Redo()) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }
        }

        // 4. マウス操作・配置・選択・削除
        if (!isHoveringUI) {
            rt.Editor.UpdateHoverState(screenW, screenH, viewMatrix, projMatrix);

            if (rt.Editor.ToolMode == EditorToolMode::Paint) {
                // [R] キーでブラシ回転
                if (Input::GetKeyDown(KeyCode::R)) {
                    rt.Editor.RotateBrush();
                }

                // 連続ドラッグ配置
                if (Input::GetKeyDown(KeyCode::MOUSE_LEFT)) {
                    if (rt.Editor.StartDragPlace()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                } else if (Input::GetKeyHold(KeyCode::MOUSE_LEFT)) {
                    if (rt.Editor.ContinueDragPlace()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
                if (Input::GetKeyUp(KeyCode::MOUSE_LEFT)) {
                    rt.Editor.EndDragPlace();
                }

                // 連続ドラッグ削除 (右ドラッグ、Shift非押下、Flycam視点操作中ではない時)
                bool flycamLooking = (rt.Editor.CameraMode == EditorCameraMode::Flycam3D && Input::GetKeyHold(KeyCode::MOUSE_RIGHT));
                if (!flycamLooking && !Input::GetKeyHold(KeyCode::SHIFT)) {
                    if (Input::GetKeyDown(KeyCode::MOUSE_RIGHT)) {
                        if (rt.Editor.StartDragRemove()) {
                            LuminousEditorRefreshSceneEntities(registry);
                        }
                    } else if (Input::GetKeyHold(KeyCode::MOUSE_RIGHT)) {
                        if (rt.Editor.ContinueDragRemove()) {
                            LuminousEditorRefreshSceneEntities(registry);
                        }
                    }
                    if (Input::GetKeyUp(KeyCode::MOUSE_RIGHT)) {
                        rt.Editor.EndDragRemove();
                    }
                }
            } else if (rt.Editor.ToolMode == EditorToolMode::Select) {
                // 選択モード
                if (Input::GetKeyDown(KeyCode::MOUSE_LEFT)) {
                    uint64_t pickedId = rt.Editor.PickObjectAtScreen(screenW, screenH, viewMatrix, projMatrix);
                    rt.Editor.SelectObject(pickedId);
                    LuminousEditorRefreshSceneEntities(registry);
                }

                // 削除
                if (Input::GetKeyDown(KeyCode::DEL) || Input::GetKeyDown(KeyCode::BACKSPACE)) {
                    if (rt.Editor.DeleteSelectedObject()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }

                // 回転
                if (Input::GetKeyDown(KeyCode::R)) {
                    if (rt.Editor.RotateSelectedObject(90.0f)) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
            }
        }

        // 5. 3Dゴーストプレビューの更新
        if (registry.valid(rt.PreviewGhost)) {
            auto& ghostTrans = registry.get<Engine::Core::TransformComponent>(rt.PreviewGhost);
            if (rt.Editor.ToolMode == EditorToolMode::Paint && rt.Editor.HasFloorHit && !isHoveringUI) {
                DirectX::XMFLOAT3 pos = rt.Editor.GetPlacementPosition();
                bool conflict = rt.Editor.CheckPlacementConflict();

                if (rt.CurrentGhostAsset != rt.Editor.SelectedAssetId || rt.CurrentGhostConflict != conflict) {
                    rt.CurrentGhostAsset = rt.Editor.SelectedAssetId;
                    rt.CurrentGhostConflict = conflict;
                    std::string mPath = "Assets/Models/Dungeon/" + rt.CurrentGhostAsset + ".obj";
                    auto mesh = meshMgr->GetOrLoadMesh(mPath);

                    Engine::Core::Prefab ghostMat;
                    if (conflict) {
                        ghostMat = CreateTranslucentMaterialPrefab(materialMgr, XMFLOAT4(0.95f, 0.15f, 0.15f, 0.60f), XMFLOAT3(0.5f, 0.05f, 0.05f));
                    } else {
                        ghostMat = CreateTranslucentMaterialPrefab(materialMgr, XMFLOAT4(0.15f, 0.90f, 0.45f, 0.60f), XMFLOAT3(0.05f, 0.4f, 0.15f));
                    }
                    Engine::Graphics::MeshAttacher::AttachToEntity(registry, rt.PreviewGhost, mesh, ghostMat);
                }

                ghostTrans.LocalPosition = pos;
                XMStoreFloat4(&ghostTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(0.0f, XMConvertToRadians(rt.Editor.GetPlacementYaw()), 0.0f));
                ghostTrans.LocalScale = { 1.0f, 1.0f, 1.0f };
                ghostTrans.IsDirty = true;
            } else {
                if (auto* rootComp = registry.try_get<Engine::Graphics::MeshRootComponent>(rt.PreviewGhost)) {
                    entt::entity sub = rootComp->FirstSubMesh;
                    while (sub != entt::null && registry.valid(sub)) {
                        entt::entity nextSub = registry.get<Engine::Graphics::SubMeshComponent>(sub).NextSubMesh;
                        Engine::Core::TransformAPI::DetachParent(registry, sub);
                        registry.destroy(sub);
                        sub = nextSub;
                    }
                    rootComp->FirstSubMesh = entt::null;
                }
                rt.CurrentGhostAsset = "";
                ghostTrans.LocalPosition = { 0.0f, -9999.0f, 0.0f };
                ghostTrans.LocalScale = { 1.0f, 1.0f, 1.0f };
                ghostTrans.IsDirty = true;
            }
        }

        // 6. [F5] テストプレイ直接起動
        if (Input::GetKeyDown(KeyCode::F5)) {
            auto val = rt.Editor.Validate();
            if (val.IsValid) {
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(rt.Editor.Stage, true));
            } else {
                rt.Editor.SetStatusMessage("Cannot Test Play: " + (val.Errors.empty() ? "Validation Failed" : val.Errors[0]));
            }
        }

        // 7. エディタUI描画
        LuminousEditorRenderUI(ctx);

        // 8. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(1920.0f, 1080.0f);
    }

    inline void LuminousEditorRenderUI(const Engine::Core::SystemContext& ctx) {
        auto& rt = EditorRuntime(ctx.GetRegistry());
#ifndef IMGUI_DISABLE
        ImGuiIO& io = ImGui::GetIO();
        auto& registry = ctx.GetRegistry();

        // 1. 上部コントロールバー (EDITOR_TOP_BAR)
        ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 56), ImGuiCond_Always);
        ImGui::Begin("EDITOR_TOP_BAR", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // ツールモード切替ボタン (1: Paint, 2: Select)
        bool isPaint = (rt.Editor.ToolMode == EditorToolMode::Paint);
        if (isPaint) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.70f, 0.40f, 1.0f));
        if (ImGui::Button("[1] PAINT BRUSH", ImVec2(130, 36))) {
            rt.Editor.ToolMode = EditorToolMode::Paint;
            LuminousEditorRefreshSceneEntities(registry);
        }
        if (isPaint) ImGui::PopStyleColor();

        ImGui::SameLine();
        bool isSelect = (rt.Editor.ToolMode == EditorToolMode::Select);
        if (isSelect) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.65f, 0.15f, 1.0f));
        if (ImGui::Button("[2] SELECT OBJECT", ImVec2(130, 36))) {
            rt.Editor.ToolMode = EditorToolMode::Select;
            LuminousEditorRefreshSceneEntities(registry);
        }
        if (isSelect) ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // カメラモード切替ボタン (Tab: TopDown / 3D Flycam)
        bool isFlycam = (rt.Editor.CameraMode == EditorCameraMode::Flycam3D);
        if (isFlycam) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.20f, 0.55f, 0.90f, 1.0f));
        const char* camBtnText = isFlycam ? "[TAB] 3D FLYCAM" : "[TAB] TOP-DOWN";
        if (ImGui::Button(camBtnText, ImVec2(130, 36))) {
            rt.Editor.ToggleCameraMode();
            if (rt.Editor.CameraMode == EditorCameraMode::TopDown) {
                Input::SetCursorLocked(false);
            }
            LuminousEditorRefreshSceneEntities(registry);
        }
        if (isFlycam) ImGui::PopStyleColor();

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // フロア切替
        int minF = rt.Editor.Stage.GetMinFloor();
        int maxF = rt.Editor.Stage.GetMaxFloor();
        if (ImGui::Button("< Prev", ImVec2(60, 36)) && rt.Editor.CurrentFloor > minF) {
            rt.Editor.CurrentFloor--;
            LuminousEditorRefreshSceneEntities(registry);
        }
        ImGui::SameLine();
        const auto* curFlrCfg = rt.Editor.Stage.FindFloor(rt.Editor.CurrentFloor);
        std::string flrName = curFlrCfg ? curFlrCfg->Name : ("Floor " + std::to_string(rt.Editor.CurrentFloor + 1));
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), " [ %d F : %s ] ", rt.Editor.CurrentFloor + 1, flrName.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Next >", ImVec2(60, 36)) && rt.Editor.CurrentFloor < maxF) {
            rt.Editor.CurrentFloor++;
            LuminousEditorRefreshSceneEntities(registry);
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();

        // テストプレイ (F5)
        auto valRes = rt.Editor.Validate();
        if (valRes.IsValid) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.75f, 0.35f, 1.0f));
            if (ImGui::Button(" TEST PLAY (F5) ", ImVec2(140, 36))) {
                LuminousTransition::Get().ChangeScene(std::make_shared<LuminousPlayScene>(rt.Editor.Stage, true));
            }
            ImGui::PopStyleColor();
        } else {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.4f, 0.4f, 0.4f, 0.6f));
            if (ImGui::Button(" TEST PLAY (F5) ", ImVec2(140, 36))) {
                rt.Editor.SetStatusMessage("Cannot Test Play: " + valRes.Errors[0]);
            }
            ImGui::PopStyleColor();
        }

        ImGui::SameLine();
        if (ImGui::Button("UNDO (Ctrl+Z)", ImVec2(110, 36))) {
            if (rt.Editor.Undo()) LuminousEditorRefreshSceneEntities(registry);
        }

        ImGui::SameLine();
        if (ImGui::Button("REDO (Ctrl+Y)", ImVec2(110, 36))) {
            if (rt.Editor.Redo()) LuminousEditorRefreshSceneEntities(registry);
        }

        ImGui::SameLine();
        if (ImGui::Button("SAVE JSON", ImVec2(90, 36))) {
            std::string err;
            if (rt.Editor.Stage.SaveToFile(rt.Editor.CurrentFilePath, err)) {
                rt.Editor.SetStatusMessage("Stage Saved Successfully!");
            } else {
                rt.Editor.SetStatusMessage("Save Error: " + err);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("LOAD JSON", ImVec2(90, 36))) {
            std::string err;
            if (rt.Editor.Stage.LoadFromFile(rt.Editor.CurrentFilePath, err)) {
                rt.Editor.SetStatusMessage("Stage Loaded Successfully!");
                LuminousEditorRefreshSceneEntities(registry);
            } else {
                rt.Editor.SetStatusMessage("Load Error: " + err);
            }
        }

        ImGui::SameLine();
        if (ImGui::Button("TITLE", ImVec2(80, 36))) {
            LuminousTransition::Get().ChangeScene(std::make_shared<LuminousTitleScene>());
        }

        ImGui::End();

        // 2. 左側パレットUI (ASSET_PALETTE)
        ImGui::SetNextWindowPos(ImVec2(10, 76), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(350, io.DisplaySize.y - 120), ImGuiCond_Always);
        ImGui::Begin("ASSET_PALETTE", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        if (rt.Editor.ToolMode == EditorToolMode::Paint) {
            // 3D プレビューターンテーブル情報カード
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.07f, 0.09f, 0.14f, 0.85f));
            ImGui::BeginChild("BrushCard", ImVec2(0, 140), true);
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "=== BRUSH SETTINGS ===");
            ImGui::Text("Asset: %s", rt.Editor.SelectedAssetId.c_str());

            bool isEdgeSnap = (rt.Editor.SelectedPlacement == PlacementType::EdgeSnap || rt.Editor.SelectedCategory == AssetCategory::Wall);
            if (isEdgeSnap) {
                ImGui::Text("Snap: EdgeSnap (Auto Align)");
                const char* flipLabel = (rt.Editor.BrushWallFlipYaw > 90.0f) ? "Flip Face 180 deg (R) [180 deg]" : "Flip Face 180 deg (R) [0 deg]";
                if (ImGui::Button(flipLabel, ImVec2(-1, 26))) {
                    rt.Editor.RotateBrush();
                }
            } else {
                ImGui::Text("Snap: %s | Yaw: %.0f deg",
                    (rt.Editor.SelectedPlacement == PlacementType::CellSnap ? "CellSnap" : "FreeAttach"),
                    rt.Editor.BrushYaw);
                if (ImGui::Button("Rotate 90 deg (R)", ImVec2(-1, 26))) {
                    rt.Editor.RotateBrush();
                }
            }

            if (rt.Editor.SelectedCategory == AssetCategory::Floor || rt.Editor.SelectedCategory == AssetCategory::Wall) {
                bool isPhase = (rt.Editor.SelectedPhase == MaterialPhase::Phase);
                if (ImGui::Checkbox("Phase Inverted (Hole/Light Pass)", &isPhase)) {
                    rt.Editor.SelectedPhase = isPhase ? MaterialPhase::Phase : MaterialPhase::Normal;
                }
            }
            if (rt.Editor.SelectedAssetId == "Pedestal") {
                const int32_t orbCount = rt.Editor.CountInitialOrbs();
                const bool orbLimitReached = (orbCount >= MAX_STAGE_ORBS);
                if (orbLimitReached) {
                    rt.Editor.BrushInitialOrb = false;
                    ImGui::BeginDisabled();
                }
                ImGui::Checkbox("Has Initial Orb", &rt.Editor.BrushInitialOrb);
                if (orbLimitReached) ImGui::EndDisabled();
                ImGui::TextColored(
                    orbLimitReached ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.65f, 0.75f, 0.85f, 1.0f),
                    "Orbs: %d / %d%s", orbCount, MAX_STAGE_ORBS,
                    orbLimitReached ? "  (limit reached)" : "");
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // カテゴリタブ
            const char* categories[] = { "Floors", "Walls", "Stairs", "Pedestals", "Specials", "Props" };
            for (int i = 0; i < 6; ++i) {
                bool isCurrent = (rt.ActivePaletteCategory == i);
                if (isCurrent) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.6f, 0.9f, 1.0f));
                if (ImGui::Button(categories[i], ImVec2(52, 26))) {
                    rt.ActivePaletteCategory = i;
                }
                if (isCurrent) ImGui::PopStyleColor();
                if (i % 3 != 2 && i < 5) ImGui::SameLine();
            }
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            // アセットカードグリッド
            auto DrawAssetCard = [&](const char* label, const std::string& assetId, AssetCategory cat, PlacementType place) {
                bool isSelected = (rt.Editor.SelectedAssetId == assetId);
                if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.65f, 0.85f, 1.0f));
                if (ImGui::Button(label, ImVec2(-1, 30))) {
                    rt.Editor.SelectAsset(assetId, cat, place);
                }
                if (isSelected) ImGui::PopStyleColor();
            };

            if (rt.ActivePaletteCategory == 0) { // Floors
                DrawAssetCard("Floor_1x1 (Standard Stone)", "Floor_1x1", AssetCategory::Floor, PlacementType::CellSnap);
                DrawAssetCard("Floor_PatternB (Patterned Cobble)", "Floor_PatternB", AssetCategory::Floor, PlacementType::CellSnap);
                DrawAssetCard("Floor_SawBlade (Hazard Trap)", "Floor_SawBlade", AssetCategory::Floor, PlacementType::CellSnap);
                DrawAssetCard("Floor_Spikes (Needle Trap)", "Floor_Spikes", AssetCategory::Floor, PlacementType::CellSnap);
                DrawAssetCard("Floor_Tile (Decorative Tile)", "Floor_Tile", AssetCategory::Floor, PlacementType::FreeAttach);
            } else if (rt.ActivePaletteCategory == 1) { // Walls
                DrawAssetCard("Wall_Brick (Solid Stone)", "Wall_Brick", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Smooth (Smooth Stone)", "Wall_Smooth", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Grate (Iron Fence - Pass Light)", "Wall_Grate", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Window (Arch Window)", "Wall_Window", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Railing (Low Railing)", "Wall_Railing", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_DoorArc (Archway Opening)", "Wall_DoorArc", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Door (Heavy Door)", "Wall_Door", AssetCategory::Wall, PlacementType::EdgeSnap);
                DrawAssetCard("Wall_Ruined (Broken Wall)", "Wall_Ruined", AssetCategory::Wall, PlacementType::EdgeSnap);
            } else if (rt.ActivePaletteCategory == 2) { // Stairs
                DrawAssetCard("Stairs_Straight (1x2 Rise 3m)", "Stairs_Straight", AssetCategory::Stairs, PlacementType::CellSnap);
            } else if (rt.ActivePaletteCategory == 3) { // Pedestals
                DrawAssetCard("Pedestal (Socket Pillar)", "Pedestal", AssetCategory::Pedestal, PlacementType::FreeAttach);
            } else if (rt.ActivePaletteCategory == 4) { // Specials
                DrawAssetCard("StartDais (Player Spawn Dais)", "StartDais", AssetCategory::Special, PlacementType::CellSnap);
                DrawAssetCard("GoalChest (Treasure Goal)", "GoalChest", AssetCategory::Special, PlacementType::FreeAttach);
            } else if (rt.ActivePaletteCategory == 5) { // Props
                DrawAssetCard("Wall Lamp (Prop_Torch)", "Prop_Torch", AssetCategory::Prop, PlacementType::EdgeSnap);
                DrawAssetCard("Floor Lamp (Prop_Candle)", "Prop_Candle", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Pillar", "Prop_Pillar", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Barrel", "Prop_Barrel", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_WoodenBox", "Prop_WoodenBox", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Brick (Single Brick)", "Prop_Brick", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_BrickPile (Rubble Pile)", "Prop_BrickPile", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Rock", "Prop_Rock", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Debris", "Prop_Debris", AssetCategory::Prop, PlacementType::FreeAttach);
                DrawAssetCard("Prop_Skull", "Prop_Skull", AssetCategory::Prop, PlacementType::FreeAttach);
            }
        } else {
            // 選択モード時のガイド
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.10f, 0.04f, 0.85f));
            ImGui::BeginChild("SelectGuideCard", ImVec2(0, 160), true);
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "=== SELECTION MODE ===");
            ImGui::Spacing();
            ImGui::TextWrapped("Click any object in the 3D viewport or Top-down view to select it.");
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "[L-Click] Pick Object");
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "[Del / Backspace] Delete");
            ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "[R] Rotate 90 deg");
            ImGui::TextWrapped("Inspect & edit properties on the right panel.");
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        ImGui::End();

        // 3. 右側パネル (STAGE_STUDIO: レイヤー管理・インスペクタ・バリデーション)
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 560, 76), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(360, io.DisplaySize.y - 120), ImGuiCond_Always);
        ImGui::Begin("STAGE_STUDIO", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);

        // セクション 0: ステージ設定・保存 (STAGE SETTINGS & SAVE)
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "=== STAGE SETTINGS ===");
        char nameBuf[128] = {};
        strncpy_s(nameBuf, rt.Editor.Stage.Name.c_str(), sizeof(nameBuf) - 1);
        if (ImGui::InputText("Name##stage_name", nameBuf, sizeof(nameBuf))) {
            rt.Editor.Stage.Name = nameBuf;
        }
        char pathBuf[256] = {};
        strncpy_s(pathBuf, rt.Editor.CurrentFilePath.c_str(), sizeof(pathBuf) - 1);
        if (ImGui::InputText("Path##file_path", pathBuf, sizeof(pathBuf))) {
            rt.Editor.CurrentFilePath = pathBuf;
        }
        if (ImGui::Button("SAVE STAGE", ImVec2(165, 26))) {
            std::string err;
            if (rt.Editor.Stage.SaveToFile(rt.Editor.CurrentFilePath, err)) {
                rt.Editor.SetStatusMessage("Stage saved: " + rt.Editor.CurrentFilePath);
            } else {
                rt.Editor.SetStatusMessage("Save Error: " + err);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("LOAD STAGE", ImVec2(165, 26))) {
            std::string err;
            if (rt.Editor.Stage.LoadFromFile(rt.Editor.CurrentFilePath, err)) {
                rt.Editor.SetStatusMessage("Stage loaded: " + rt.Editor.CurrentFilePath);
                LuminousEditorRefreshSceneEntities(registry);
            } else {
                rt.Editor.SetStatusMessage("Load Error: " + err);
            }
        }
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // セクション 1: 動的フロアレイヤー (DYNAMIC FLOOR LAYERS)
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "=== DYNAMIC FLOOR LAYERS ===");
        ImGui::Spacing();

        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.05f, 0.07f, 0.10f, 0.85f));
        ImGui::BeginChild("LayersList", ImVec2(0, 135), true);
        auto& floors = rt.Editor.Stage.Floors;
        // 上の階層から順に表示 (逆順)
        for (int i = static_cast<int>(floors.size()) - 1; i >= 0; --i) {
            auto& flr = floors[i];
            ImGui::PushID(flr.FloorIndex);

            bool isCur = (rt.Editor.CurrentFloor == flr.FloorIndex);
            if (ImGui::Checkbox("##vis", &flr.Visible)) {
                LuminousEditorRefreshSceneEntities(registry);
            }
            ImGui::SameLine();

            if (isCur) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.85f, 0.2f, 1.0f));
            std::string flrLabel = std::format("{}F: {}", flr.FloorIndex + 1, flr.Name);
            if (ImGui::Selectable(flrLabel.c_str(), isCur, 0, ImVec2(140, 20))) {
                rt.Editor.CurrentFloor = flr.FloorIndex;
                LuminousEditorRefreshSceneEntities(registry);
            }
            if (isCur) ImGui::PopStyleColor();

            ImGui::SameLine();
            if (ImGui::Button("^##up", ImVec2(22, 20))) {
                rt.Editor.CurrentFloor = flr.FloorIndex;
                if (rt.Editor.MoveCurrentFloorUp()) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("v##dn", ImVec2(22, 20))) {
                rt.Editor.CurrentFloor = flr.FloorIndex;
                if (rt.Editor.MoveCurrentFloorDown()) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }
            ImGui::SameLine();
            if (floors.size() > 1) {
                if (ImGui::Button("X##del", ImVec2(22, 20))) {
                    rt.Editor.CurrentFloor = flr.FloorIndex;
                    if (rt.Editor.DeleteCurrentFloor()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
            }

            ImGui::PopID();
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        // フロア追加ボタン
        if (ImGui::Button("+ Add Floor Above", ImVec2(165, 26))) {
            rt.Editor.AddFloorAbove();
            LuminousEditorRefreshSceneEntities(registry);
        }
        ImGui::SameLine();
        if (ImGui::Button("+ Add Floor Below", ImVec2(165, 26))) {
            rt.Editor.AddFloorBelow();
            LuminousEditorRefreshSceneEntities(registry);
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // セクション 2: オブジェクトインスペクタ (OBJECT INSPECTOR)
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "=== OBJECT INSPECTOR ===");
        auto* selectedObj = rt.Editor.GetSelectedObjectMut();
        if (selectedObj) {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.05f, 0.85f));
            ImGui::BeginChild("InspectorCard", ImVec2(0, 310), true);

            ImGui::Text("Asset: %s", selectedObj->AssetId.c_str());
            ImGui::Text("Instance ID: #%llu", selectedObj->InstanceId);
            ImGui::Text("Floor: %d F", selectedObj->FloorIndex + 1);
            if (selectedObj->FloorIndex != rt.Editor.CurrentFloor) {
                if (ImGui::Button(std::format("Move to Active Floor ({}F)", rt.Editor.CurrentFloor + 1).c_str(), ImVec2(-1, 24))) {
                    if (rt.Editor.MoveSelectedObjectToFloor(rt.Editor.CurrentFloor)) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
            }

            ImGui::Text("Pos: (%.2f, %.2f, %.2f)", selectedObj->Position.x, selectedObj->Position.y, selectedObj->Position.z);
            ImGui::Text("Yaw: %.0f deg", selectedObj->Rotation.y);
            if (ImGui::Button("Rotate +90 deg (R)", ImVec2(-1, 24))) {
                if (rt.Editor.RotateSelectedObject(90.0f)) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }

            if (selectedObj->Category == AssetCategory::Floor || selectedObj->Category == AssetCategory::Wall) {
                bool isPhase = (selectedObj->Phase == MaterialPhase::Phase);
                if (ImGui::Checkbox("Phase Inverted", &isPhase)) {
                    if (rt.Editor.ToggleSelectedPhase()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
            }
            if (selectedObj->AssetId == "StartDais") {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(0.45f, 0.9f, 1.0f, 1.0f), "Player Spawn Facing:");
                const char* facingItems[] = { "North (+Z)", "East (+X)", "South (-Z)", "West (-X)" };
                int facingIdx = static_cast<int>(selectedObj->Facing);
                if (ImGui::Combo("Facing", &facingIdx, facingItems, IM_ARRAYSIZE(facingItems))) {
                    selectedObj->Facing = static_cast<SpawnFacing>(facingIdx);
                }
                ImGui::TextColored(ImVec4(0.65f, 0.75f, 0.85f, 1.0f),
                    "Yaw at spawn: %.0f deg", SpawnFacingToYawDegrees(selectedObj->Facing));
            }
            if (selectedObj->AssetId == "Pedestal") {
                bool hasOrb = selectedObj->HasInitialOrb;
                const int32_t selOrbCount = rt.Editor.CountInitialOrbs();
                const bool selOrbLocked = (!hasOrb && selOrbCount >= MAX_STAGE_ORBS);
                if (selOrbLocked) ImGui::BeginDisabled();
                if (ImGui::Checkbox("Has Initial Orb", &hasOrb)) {
                    if (rt.Editor.ToggleSelectedInitialOrb()) {
                        LuminousEditorRefreshSceneEntities(registry);
                    }
                }
                if (selOrbLocked) ImGui::EndDisabled();
                ImGui::TextColored(
                    (selOrbCount >= MAX_STAGE_ORBS) ? ImVec4(1.0f, 0.45f, 0.35f, 1.0f) : ImVec4(0.65f, 0.75f, 0.85f, 1.0f),
                    "Stage Orbs: %d / %d", selOrbCount, MAX_STAGE_ORBS);
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Light Settings:");
            if (ImGui::Checkbox("Enable Light", &selectedObj->HasLight)) {
                LuminousEditorRefreshSceneEntities(registry);
            }
            if (selectedObj->HasLight) {
                float col[3] = { selectedObj->LightColor.x, selectedObj->LightColor.y, selectedObj->LightColor.z };
                if (ImGui::ColorEdit3("Light Color", col)) {
                    selectedObj->LightColor = { col[0], col[1], col[2] };
                    LuminousEditorRefreshSceneEntities(registry);
                }
                if (ImGui::DragFloat("Intensity", &selectedObj->LightIntensity, 0.5f, 0.0f, 100.0f, "%.1f")) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
                if (ImGui::DragFloat("Radius", &selectedObj->LightRadius, 0.5f, 1.0f, 50.0f, "%.1f")) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }

            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.85f, 0.2f, 0.2f, 1.0f));
            if (ImGui::Button("DELETE OBJECT (Del)", ImVec2(-1, 28))) {
                if (rt.Editor.DeleteSelectedObject()) {
                    LuminousEditorRefreshSceneEntities(registry);
                }
            }
            ImGui::PopStyleColor();

            ImGui::EndChild();
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("No object selected.");
            ImGui::TextDisabled("Click an object in the viewport or press [2] to enter Select Mode.");
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // セクション 3: ステージバリデーション (STAGE VALIDATION)
        ImGui::TextColored(ImVec4(0.4f, 0.85f, 1.0f, 1.0f), "=== STAGE VALIDATION ===");
        ImGui::Text("Start Points: %d / 1", valRes.StartPointCount);
        ImGui::Text("Goal Points:  %d / 1", valRes.GoalPointCount);
        ImGui::Text("Initial Orbs: %d", valRes.InitialOrbCount);
        ImGui::Text("Total Objects: %zu", rt.Editor.Stage.Objects.size());
        ImGui::Spacing();

        if (valRes.IsValid) {
            ImGui::TextColored(ImVec4(0.2f, 0.9f, 0.3f, 1.0f), "Status: VALID (Ready to Play)");
        } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.2f, 1.0f), "Status: INVALID");
            for (const auto& err : valRes.Errors) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "- %s", err.c_str());
            }
        }

        ImGui::End();

        // 4. 下部ステータスバー (STATUS_BAR)
        ImGui::SetNextWindowPos(ImVec2(10, io.DisplaySize.y - 34), ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(io.DisplaySize.x - 20, 28), ImGuiCond_Always);
        ImGui::Begin("STATUS_BAR", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        const char* modeStr = (rt.Editor.ToolMode == EditorToolMode::Paint ? "PAINT" : "SELECT");
        const char* camStr = (rt.Editor.CameraMode == EditorCameraMode::TopDown ? "TOP-DOWN" : "3D FLYCAM");
        if (rt.Editor.HasFloorHit) {
            ImGui::Text("[%s | %s] Cursor: (%d, %d, %dF) | Edge: %s | %s",
                modeStr, camStr,
                rt.Editor.HoverCell.X, rt.Editor.HoverCell.Z, rt.Editor.HoverCell.Floor + 1,
                (rt.Editor.HoverEdge == CellEdge::North ? "N" : (rt.Editor.HoverEdge == CellEdge::South ? "S" : (rt.Editor.HoverEdge == CellEdge::East ? "E" : "W"))),
                rt.Editor.StatusMessage.c_str());
        } else {
            ImGui::Text("[%s | %s] Cursor: (Out of bounds) | %s", modeStr, camStr, rt.Editor.StatusMessage.c_str());
        }
        ImGui::End();
#endif
    }
}
