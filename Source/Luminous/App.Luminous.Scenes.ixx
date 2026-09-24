module;

#define NOMINMAX
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <memory>
#include <vector>
#include <string>
#include <optional>
#include <cstdio>
#include <cstdlib>
#include <unordered_map>
#include <cmath>
#include <algorithm>
#include <format>
#include <DirectXMath.h>
#include <entt/entt.hpp>
#include <EngineDebug.h>
#include <span>

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
import App.Luminous.StageCatalog;
import App.Luminous.Transition;
import App.Luminous.InputConfig;
import App.Luminous.Profile;
import App.Luminous.DebugTools;
import App.Luminous.UIScreens;
import Engine.UI.Types;
import Engine.UI.System;
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


    // ========================================================================
    // タイトル画面 (完全 ImGui フリー、360 度パノラマ回転背景)
    // ========================================================================
    // タイトル画面の処理 (レジストリだけを見る自由関数)
    void LuminousTitleRenderMenu(const Engine::Core::SystemContext& ctx);
    void LuminousTitleRenderStageSelect(const Engine::Core::SystemContext& ctx, float screenW);
    void LuminousTitleRenderCustomStageList(const Engine::Core::SystemContext& ctx);
    void LuminousTitleUpdate(const Engine::Core::SystemContext& ctx);

    // ========================================================================
    // 本編 (FPS。完全 ImGui フリーの画像 UI と空間音響)
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
    // ダンジョンの環境光。本編と、エディタで見せるとき (AU-24) の両方で置く
    inline void LuminousPlaySpawnAmbientLight(entt::registry& registry) {
        auto ambientEnt = CreateGeneratedEntity(registry);
        auto& ambTrans = registry.emplace<Engine::Core::TransformComponent>(ambientEnt);
        ambTrans.LocalPosition = { 10.0f, 30.0f, 10.0f };
        XMStoreFloat4(&ambTrans.LocalRotation, XMQuaternionRotationRollPitchYaw(XMConvertToRadians(70.0f), XMConvertToRadians(30.0f), 0.0f));
        registry.emplace<Engine::Graphics::DirectionalLightComponent>(ambientEnt, Engine::Graphics::DirectionalLightComponent{
            .Color = { 0.85f, 0.90f, 1.0f },
            .Intensity = g_LuminousConfig.AmbientLightIntensity,
            .CastShadows = false
        });
    }

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
        LuminousPlaySpawnAmbientLight(registry);

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
    // 遊ぶステージを決めて本編のシーンへ移る (計画 18 の段 3)。
    //
    //   コードのシーンを作って渡していた頃は `LuminousPlayScene(stage, …)` と
    //   書けたが、いま行き先は**ファイル**なので、その場で決まる値は
    //   LuminousSession (シーンをまたいで残る持ち物) へ預ける
    inline void LuminousGoToStage(entt::registry& registry, std::string stagePath,
                                  int stageNumber = 0, bool returnToEditor = false) {
        LuminousSession& session = Session(registry);
        session.StagePath = std::move(stagePath);
        session.StageNumber = stageNumber;
        session.ReturnToEditor = returnToEditor;
        LuminousTransition::Get().ChangeScene(LuminousScenes::Play);
    }

    // ステージ選択へ移る。**シーンはタイトルと同じ** (同じ部屋。画面だけが違う)。
    //   以前はステージ選択のために別のシーンがあり、中身はタイトルと同じ部屋だった
    //   (部屋を直すとき 2 か所を直す必要があった)。どの画面から始めるかは持ち物へ預ける
    inline void LuminousGoToStageSelect(entt::registry& registry) {
        Session(registry).TitleStartsAtStageSelect = true;
        LuminousTransition::Get().ChangeScene(LuminousScenes::Title);
    }

    // 登録ステージ (1 始まりの番号) へ移る。ファイルが無ければ何もしない
    inline void LuminousGoToBaseStage(entt::registry& registry, int stageNumber,
                                      bool returnToEditor = false) {
        if (const auto* entry = LuminousStageCatalog::GetByStageNumber(stageNumber)) {
            LuminousGoToStage(registry, entry->StageFilePath, stageNumber, returnToEditor);
        }
    }

    // ------------------------------------------------------------------------
    // 開発用の起動オプション (19 の AU-24)。
    //
    //   以前は変種ごとにシーンの殻があった (Luminous.Play.Shadow など)。
    //   殻をやめて、起動オプションが言うステージ / 並びのタブをここで返す。
    //   **最初の 1 回だけ**使う (殻を直に開いたときと同じ。あとでタイトルから
    //   本編やエディタへ入り直したときには効かない)
    // ------------------------------------------------------------------------
    namespace LuminousDevLaunch {
        inline std::string TakeStagePath() {
            static bool taken = false;
            if (taken) return {};
            taken = true;
            struct Option { const char* Flag; const char* StagePath; };
            static constexpr Option kOptions[] = {
                { "play-shadow-edge", "Assets/Data/TestStages/shadow_edge.json" },
                { "play-shadow-open", "Assets/Data/TestStages/shadow_open.json" },
                { "play-shadow",      "Assets/Data/TestStages/shadow.json" },
                { "play2",            "Assets/Data/TestStages/multi_floor.json" },
            };
            for (const Option& option : kOptions) {
                if (Engine::Common::CommandLine::Get().Has(option.Flag)) return option.StagePath;
            }
            return {};
        }

        // --stageselect: タイトルのシーンをステージ選択の画面から始める (以前は専用のシーンの殻)
        inline bool TakeStageSelect() {
            static bool taken = false;
            if (taken) return false;
            taken = true;
            return Engine::Common::CommandLine::Get().Has("stageselect");
        }

        inline std::optional<int> TakePaletteCategory() {
            static bool taken = false;
            if (taken) return std::nullopt;
            taken = true;
            struct Option { const char* Flag; int Category; };
            static constexpr Option kOptions[] = {
                { "editor-walls", 1 }, { "editor-specials", 4 }, { "editor-props", 5 },
            };
            for (const Option& option : kOptions) {
                if (Engine::Common::CommandLine::Get().Has(option.Flag)) return option.Category;
            }
            return std::nullopt;
        }
    }

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
        // 2b. 開発用の起動オプション --play-shadow など (以前は変種のシーンの殻が言っていた)
        if (path.empty()) path = LuminousDevLaunch::TakeStagePath();
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
        // 「もう一度」で建て直すときに要る (計画 18 の段 3)
        rt.StagePath = path;
        // 預かったものは**1 度きり**。次のシーンへ持ち越さない
        //   (持ち越すと、あとでステージエディタを開いたときに
        //    前に遊んだステージが勝手に開く)
        Session(registry).StagePath.clear();

        LuminousPlayEnsureCoreEntities(registry);
        LuminousPlaySpawnStageObjects(registry);
        LuminousPlayBuildRuntime(registry);
        ENGINE_LOG_INFO("Luminous", "ステージを建てました '{}' ({} 個)",
                        path, static_cast<int>(rt.Stage.Objects.size()));
    }

    // ------------------------------------------------------------------------
    // エディタで止めている間に、ステージの見た目だけを建てる (19 の AU-24)。
    //
    //   本編のシーンは「設定 1 体 + 組み立てシステム」なので、エディタで開くと空だった。
    //   シーンのファイルが言うステージ (無ければ基本ステージ 1) の床・壁・置き物と
    //   環境光だけを建てる。カメラ・プレイヤー・暗転・カーソルの固定には触らない。
    //   作ったもの (遊ぶための持ち物 PlayRuntime も含む) はエディタが印を付け、
    //   プレイを始める前にまとめて片付ける。プレイではこれまでどおり
    //   LuminousStageBuildSystem が本物を建てる
    // ------------------------------------------------------------------------
    inline void LuminousStagePreview(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        const LuminousPlaySettings settings = SceneSetting<LuminousPlaySettings>(registry);
        const std::string path = settings.StagePath.empty()
            ? std::string("Assets/Data/BaseStages/stage_01.json") : settings.StagePath;
        auto& rt = PlayRuntime(registry);
        std::string error;
        if (!rt.Stage.LoadFromFile(Engine::Common::Paths::ResolveString(path), error)) {
            ENGINE_LOG_WARN("Luminous", "エディタで見せるステージを読めませんでした '{}': {}", path, error);
            return;
        }
        LuminousPlaySpawnStageObjects(registry);
        LuminousPlaySpawnAmbientLight(registry);
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
    // 片付け (Teardown フェーズ。計画 18 の段 3)
    void LuminousTitleTeardown(const Engine::Core::SystemContext& ctx);
    void LuminousPlayTeardown(const Engine::Core::SystemContext& ctx);
    void LuminousEditorTeardown(const Engine::Core::SystemContext& ctx);

    // ゲームの立ち上げで 1 度だけ呼ぶ。
    //   ここで名前を通しておくと、.scene.json の "systems" から引けるようになる
    inline void RegisterLuminousSystems() {
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousStageBuildSystem", Engine::Core::SystemPhase::Update, &LuminousStageBuildSystem);
        // エディタで止めている間は、ステージの見た目だけを見せる (19 の AU-24)
        Engine::Core::SystemRegistry::Get().RegisterEditorPreview(
            "LuminousStageBuildSystem", &LuminousStagePreview);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousPlayUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousPlayUpdate);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousTitleUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousTitleUpdate);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousEditorBuildSystem", Engine::Core::SystemPhase::Update, &LuminousEditorBuildSystem);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousEditorUpdateSystem", Engine::Core::SystemPhase::Update, &LuminousEditorUpdate);
        // 片付け。**Teardown フェーズ**で登録するので、シーンのファイルには
        //   名前を並べるだけでよい ("phase" は読むときに使われない)
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousTitleTeardownSystem", Engine::Core::SystemPhase::Teardown, &LuminousTitleTeardown);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousPlayTeardownSystem", Engine::Core::SystemPhase::Teardown, &LuminousPlayTeardown);
        Engine::Core::SystemRegistry::Get().Register(
            "LuminousEditorTeardownSystem", Engine::Core::SystemPhase::Teardown, &LuminousEditorTeardown);
    }

    // ========================================================================
    // ステージエディタ (画面は UI の仕組み。StageEditor.ui.json。21 の U9)
    // ========================================================================
    // ステージエディタの処理 (レジストリだけを見る自由関数)
    void LuminousEditorRefreshSceneEntities(entt::registry& registry);
    // 編集に欠かせないもの (カメラ・環境光・下見のゴースト) を揃える
    void LuminousEditorEnsureCoreEntities(entt::registry& registry);

    void LuminousEditorRenderUI(const Engine::Core::SystemContext& ctx);
    void LuminousEditorUpdate(const Engine::Core::SystemContext& ctx);

    // ========================================================================
    // タイトル画面の実装
    // ========================================================================

    // ------------------------------------------------------------------------
    // 片付け (計画 18 の段 3)。
    //
    //   コードのシーンの OnTeardown だったもの。データのシーンには C++ の
    //   シーンが無いので、**Teardown フェーズのシステム**にした。
    //   `.scene.json` の "systems" に名前を並べれば、同じところで走る
    // ------------------------------------------------------------------------
    inline void LuminousTitleTeardown(const Engine::Core::SystemContext& ctx) {
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
        if (auto* ui = ctx.FindService<Engine::UI::UISystem>()) {
            ui->Close(rt.MenuUi);
            ui->Close(rt.StageUi);
            ui->Close(rt.ListUi);
        }
        rt.MenuUi = {};
        rt.StageUi = {};
        rt.ListUi = {};
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
        // 本編から「ステージ選択へ」と言われていれば、そこから始める (1 度きり)
        if (LuminousSession& session = Session(registry); session.TitleStartsAtStageSelect) {
            session.TitleStartsAtStageSelect = false;
            rt.View = LuminousTitleView::StageSelect;
        } else if (LuminousDevLaunch::TakeStageSelect()) {
            rt.View = LuminousTitleView::StageSelect;   // 開発用の起動オプション --stageselect
        }

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
            LuminousTransition::Get().Draw(ctx.GetRegistry());
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

        // 4. 画面 (UI の仕組みがマウス・キーボード・コントローラーを受ける)
        const float screenW = 1920.0f;

        // 画面の UI は、今の画面のものだけを開いておく (21 の U8)
        if (auto* ui = ctx.FindService<Engine::UI::UISystem>()) {
            if (rt.View != LuminousTitleView::MainMenu && ui->IsOpen(rt.MenuUi)) {
                ui->Close(rt.MenuUi);
                rt.MenuUi = {};
            }
            if (rt.View != LuminousTitleView::StageSelect && ui->IsOpen(rt.StageUi)) {
                ui->Close(rt.StageUi);
                rt.StageUi = {};
            }
            if (rt.View != LuminousTitleView::CustomStageList && ui->IsOpen(rt.ListUi)) {
                ui->Close(rt.ListUi);
                rt.ListUi = {};
            }
        }

        if (rt.View == LuminousTitleView::MainMenu) {
            LuminousTitleRenderMenu(ctx);
        } else if (rt.View == LuminousTitleView::StageSelect) {
            LuminousTitleRenderStageSelect(ctx, screenW);
        } else if (rt.View == LuminousTitleView::CustomStageList) {
            LuminousTitleRenderCustomStageList(ctx);
        }

        // 5. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(ctx.GetRegistry());
    }

    inline void LuminousTitleRenderMenu(const Engine::Core::SystemContext& ctx) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto* menu = ctx.FindService<Engine::UI::UISystem>();
        if (!menu) return;

        // ステージセレクトの出現判定: 1ステージ以上クリア済み OR エディタ自作ステージ作成済み
        const bool isStageSelectAvail = ProfileManager::Get().IsStageSelectAvailable();
        const char* const kButtons[] = { "Start", "StageSelect", "Editor", "Exit" };

        // UI の仕組み (Title.ui.json。21 の U8 の 4)
        if (!menu->IsOpen(rt.MenuUi)) {
            rt.MenuUi = menu->Open(LuminousScreenAssets::Title, { .SortOrder = LuminousScreenOrder::Menu });
            // STAGE SELECT が無いときは畳み、並びを前と同じ高さ (556) から始める
            menu->SetVisibility(menu->Find(rt.MenuUi, "StageSelect"),
                                isStageSelectAvail ? Engine::UI::UIVisibility::Visible : Engine::UI::UIVisibility::Collapsed);
            menu->SetOffset(menu->Find(rt.MenuUi, "Menu"), { 290.0f, isStageSelectAvail ? 520.0f : 556.0f });
            // ロゴの呼吸。前の 0.92 + 0.08 sin(1.1 t) と同じ形 (sine のつなぎ方で 0.84 → 1 → 0.84 の 1 周) を、
            //   同じ位相 (タイトルに入ってからの時間 rt.Timer) から始める
            constexpr float kBreathPeriod = 2.0f * DirectX::XM_PI / 1.1f;
            menu->Play(rt.MenuUi, "Breathe", { 1.0f, std::fmod(rt.Timer + kBreathPeriod * 0.25f, kBreathPeriod) });
            rt.MenuGlyphSet = -1;
            rt.MenuLastFocus = menu->Find(rt.MenuUi, kButtons[0]);
            menu->SetFocus(rt.MenuLastFocus);
        }
        if (rt.MenuGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
            rt.MenuGlyphSet = static_cast<int>(LuminousInputDevice::Current);
            LuminousUIScreens::ApplyHint(*menu, rt.MenuUi, "HintChoose", { LuminousButtonGlyph::Move, "CHOOSE" }, 38.0f);
            LuminousUIScreens::ApplyHint(*menu, rt.MenuUi, "HintDecide", { LuminousButtonGlyph::Confirm, "DECIDE" }, 38.0f);
        }

        // 選んでいるボタンが変わったら音 (マウスが乗っても、キーで動かしても)
        const Engine::UI::UIElement focus = menu->GetFocus();
        if (focus.Instance == rt.MenuUi && !(focus == rt.MenuLastFocus)) {
            rt.MenuLastFocus = focus;
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }
        // 押した (クリック・決定。前の UI の Update で起きたこと)
        int activated = -1;
        for (const Engine::UI::UIEventRecord& e : menu->GetEvents()) {
            if (e.Type != Engine::UI::UIEventType::Clicked || !(e.Element.Instance == rt.MenuUi)) continue;
            for (int i = 0; i < 4; ++i) {
                if (e.Element == menu->Find(rt.MenuUi, kButtons[i])) activated = i;
            }
        }

        if (activated >= 0) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            switch (activated) {
            case 0: {
                const int total = LuminousStageCatalog::Count();
                const int nextStage = ProfileManager::Get().GetNextUncompletedStageIndex(total);
                LuminousGoToBaseStage(ctx.GetRegistry(), nextStage);
                return;
            }
            case 1:
                rt.View = LuminousTitleView::StageSelect;
                return;
            case 2:
                LuminousTransition::Get().ChangeScene(LuminousScenes::Editor);
                return;
            case 3:
                // エンジンに頼む (ゲーム単体では終わり、エディタの Game ビューではプレイを止める)。
                //   PostQuitMessage を直に呼ぶと、エディタで遊んでいるときエディタごと閉じる
                ctx.RequestQuitGame();
                return;
            default:
                break;
            }
        }
    }

    // ステージ選択のカードの並び (前の描き方と同じ式。登録ステージの数で列の数・カードの大きさが決まる)
    struct LuminousStageGrid {
        static constexpr float Top = 215.0f;
        static constexpr float GapX = 48.0f;
        static constexpr float GapY = 32.0f;
        int Cols = 1;
        float CardW = 0.0f;
        float CardH = 0.0f;
        float OriginX = 0.0f;

        [[nodiscard]] static LuminousStageGrid Compute(int stageCount, float centerX) {
            constexpr float gridW = 1100.0f;
            constexpr float gridBottom = 600.0f;
            LuminousStageGrid g;
            g.Cols = (stageCount <= 2) ? (stageCount > 0 ? stageCount : 1) : 3;
            if (stageCount > 6) g.Cols = 4;
            const int rows = (stageCount + g.Cols - 1) / g.Cols;
            g.CardW = (std::min)(460.0f, (gridW - GapX * (g.Cols - 1)) / static_cast<float>(g.Cols));
            g.CardH = g.CardW * (260.0f / 400.0f);
            const float maxGridH = gridBottom - Top;
            const float neededH = g.CardH * rows + GapY * (rows - 1);
            if (neededH > maxGridH && neededH > 1.0f) {
                const float shrink = maxGridH / neededH;
                g.CardW *= shrink;
                g.CardH *= shrink;
            }
            const float usedW = g.CardW * g.Cols + GapX * (g.Cols - 1);
            g.OriginX = centerX - usedW * 0.5f;
            return g;
        }
        [[nodiscard]] float X(int i) const { return OriginX + (i % Cols) * (CardW + GapX); }
        [[nodiscard]] float Y(int i) const { return Top + (i / Cols) * (CardH + GapY); }
    };

    // StageSelect.ui.json のカードの枠の数 (Card0〜Card11)
    inline constexpr int kStageCardSlots = 12;

    inline void LuminousTitleRenderStageSelect(const Engine::Core::SystemContext& ctx, float screenW) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto* menu = ctx.FindService<Engine::UI::UISystem>();
        if (!menu) return;
        using Engine::UI::UIElement;
        using Engine::UI::UIVisibility;
        const float centerX = screenW * 0.5f;

        const int stageCount = LuminousStageCatalog::Count();
        const int cardCount = (std::min)(stageCount, kStageCardSlots);
        const bool hasCustomStages = ProfileManager::Get().HasCustomStages();
        const auto cardName = [](int i) { return "Card" + std::to_string(i); };

        // UI の仕組み (StageSelect.ui.json。21 の U8 の 5)
        if (!menu->IsOpen(rt.StageUi)) {
            rt.StageUi = menu->Open(LuminousScreenAssets::StageSelect, { .SortOrder = LuminousScreenOrder::Menu });
            if (stageCount > kStageCardSlots) {
                ENGINE_LOG_WARN("Luminous", "ステージが {} 個あるが、ステージ選択のカードの枠は {} 枚 (StageSelect.ui.json に枠を足す)",
                                stageCount, kStageCardSlots);
            }
            // カード: 登録ステージの数だけ出し、前と同じ式で並べる。絵は開いているかで替える
            const LuminousStageGrid grid = LuminousStageGrid::Compute(stageCount, centerX);
            // 選んだカードの枠の点滅。前の 0.70 + 0.30 sin(4 t) と同じ形・同じ位相から
            constexpr float kPulsePeriod = DirectX::XM_PI * 0.5f;
            const float pulseStart = std::fmod(rt.Timer + kPulsePeriod * 0.25f, kPulsePeriod);
            for (int i = 0; i < cardCount; ++i) {
                const auto* entry = LuminousStageCatalog::Get(i);
                const UIElement card = menu->Find(rt.StageUi, cardName(i));
                if (entry == nullptr || !card.IsValid()) continue;
                menu->SetVisibility(card, UIVisibility::Visible);
                menu->SetOffset(card, { grid.X(i), grid.Y(i) });
                menu->SetSize(card, { grid.CardW, grid.CardH });
                const bool unlocked = ProfileManager::Get().IsStageUnlocked(i + 1);
                menu->SetImage(card, unlocked
                    ? entry->ButtonImage
                    : entry->LockedImage);   // 空なら設計図の絵 (共通の鍵の絵)
                // 絵の無いステージは名前を出す (今の登録ステージは全部に絵がある)
                if (entry->ButtonImage.empty() && !entry->DisplayName.empty()) {
                    const UIElement name = menu->FindChild(card, "Name");
                    menu->SetText(name, entry->DisplayName);
                    menu->SetVisibility(name, UIVisibility::Visible);
                    if (!unlocked) menu->SetColor(name, { 0.55f / 0.95f, 0.60f / 0.97f, 0.68f, 0.80f / 0.95f });
                }
                menu->Play(rt.StageUi, cardName(i) + ".Pulse", { 1.0f, pulseStart });
            }
            // EXTRA (自作ステージがあるときだけ) と BACK
            menu->SetVisibility(menu->Find(rt.StageUi, "Extra"), hasCustomStages ? UIVisibility::Visible : UIVisibility::Collapsed);
            menu->SetOffset(menu->Find(rt.StageUi, "Back"), { centerX - 210.0f, hasCustomStages ? 736.0f : 660.0f });
            rt.StageGlyphSet = -1;
            rt.StageLastFocus = menu->Find(rt.StageUi, cardCount > 0 ? cardName(0) : std::string("Back"));
            menu->SetFocus(rt.StageLastFocus);
        }
        if (rt.StageGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
            rt.StageGlyphSet = static_cast<int>(LuminousInputDevice::Current);
            LuminousUIScreens::ApplyHint(*menu, rt.StageUi, "HintChoose", { LuminousButtonGlyph::Move, "CHOOSE" }, 38.0f);
            LuminousUIScreens::ApplyHint(*menu, rt.StageUi, "HintDecide", { LuminousButtonGlyph::Confirm, "DECIDE" }, 38.0f);
            LuminousUIScreens::ApplyHint(*menu, rt.StageUi, "HintBack", { LuminousButtonGlyph::Cancel, "BACK" }, 38.0f);
        }

        // 選んでいるものが変わったら音 (マウスが乗っても、キーで動かしても)
        const UIElement focus = menu->GetFocus();
        if (focus.Instance == rt.StageUi && !(focus == rt.StageLastFocus)) {
            rt.StageLastFocus = focus;
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }
        // 押したもの (クリック・決定。前の UI の Update で起きたこと)。カードは 0 から、EXTRA は -2、BACK は -3
        const UIElement extra = menu->Find(rt.StageUi, "Extra");
        const UIElement back = menu->Find(rt.StageUi, "Back");
        int activated = -1;
        for (const Engine::UI::UIEventRecord& e : menu->GetEvents()) {
            if (e.Type != Engine::UI::UIEventType::Clicked || !(e.Element.Instance == rt.StageUi)) continue;
            if (e.Element == extra) activated = -2;
            else if (e.Element == back) activated = -3;
            for (int i = 0; i < cardCount; ++i) {
                if (e.Element == menu->Find(rt.StageUi, cardName(i))) activated = i;
            }
        }

        if (activated >= 0) {
            if (ProfileManager::Get().IsStageUnlocked(activated + 1)) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousGoToBaseStage(ctx.GetRegistry(), activated + 1);
                return;
            }
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        } else if (activated == -2) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            rt.View = LuminousTitleView::CustomStageList;
            return;
        }
        // BACK か戻るボタン
        if (activated == -3 || LuminousInput::CancelPressed()) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            rt.View = LuminousTitleView::MainMenu;
            return;
        }
    }

    inline void LuminousTitleRenderCustomStageList(const Engine::Core::SystemContext& ctx) {
        auto& rt = TitleRuntime(ctx.GetRegistry());
        auto* menu = ctx.FindService<Engine::UI::UISystem>();
        if (!menu) return;
        using Engine::UI::UIElement;

        // UI の仕組み (CustomStages.ui.json。21 の U8 の 7)。行は一覧 (CustomStageRow.ui.json を見えている行だけに使い回す)。
        //   前は 6 行までしか出さなかったが、一覧なので 7 行目からは流して見られる
        if (!menu->IsOpen(rt.ListUi)) {
            rt.ListUi = menu->Open(LuminousScreenAssets::CustomStages, { .SortOrder = LuminousScreenOrder::Menu });
            rt.CustomStages = ProfileManager::Get().GetCustomStages();
            const UIElement list = menu->Find(rt.ListUi, "Stages");
            menu->SetListCount(list, static_cast<uint32_t>(rt.CustomStages.size()));
            menu->OnListItem(list, [menu, stages = rt.CustomStages](const Engine::UI::UIListItem& item) {
                if (item.Index >= stages.size()) return;
                // 項目の中の名前は項目の根から引く ("Stages.0.Name"。ボタンの子でも "Stages.0.Entry.Name" ではない)
                menu->SetText(menu->FindChild(item.Root, "Name"), stages[item.Index].first);
                menu->SetText(menu->FindChild(item.Root, "NameLit"), stages[item.Index].first);
                menu->SetText(menu->FindChild(item.Root, "Path"), "Path: " + stages[item.Index].second);
            });
            rt.ListGlyphSet = -1;
            rt.ListLastFocus = menu->Find(rt.ListUi, rt.CustomStages.empty() ? "Back" : "Stages.0.Entry");
            menu->SetFocus(rt.ListLastFocus);
        }
        if (rt.ListGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
            rt.ListGlyphSet = static_cast<int>(LuminousInputDevice::Current);
            LuminousUIScreens::ApplyHint(*menu, rt.ListUi, "HintChoose", { LuminousButtonGlyph::Move, "CHOOSE" }, 38.0f);
            LuminousUIScreens::ApplyHint(*menu, rt.ListUi, "HintDecide", { LuminousButtonGlyph::Confirm, "DECIDE" }, 38.0f);
            LuminousUIScreens::ApplyHint(*menu, rt.ListUi, "HintBack", { LuminousButtonGlyph::Cancel, "BACK" }, 38.0f);
        }

        // 選んでいるものが変わったら音 (マウスが乗っても、キーで動かしても)
        const UIElement focus = menu->GetFocus();
        if (focus.Instance == rt.ListUi && !(focus == rt.ListLastFocus)) {
            rt.ListLastFocus = focus;
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
        }
        // 押したもの (クリック・決定。前の UI の Update で起きたこと)。行は項目の番号、BACK は -3
        const UIElement back = menu->Find(rt.ListUi, "Back");
        int activated = -1;
        for (const Engine::UI::UIEventRecord& e : menu->GetEvents()) {
            if (e.Type != Engine::UI::UIEventType::Clicked || !(e.Element.Instance == rt.ListUi)) continue;
            if (e.Element == back) { activated = -3; continue; }
            const uint32_t index = menu->GetListIndex(e.Element);
            if (index < rt.CustomStages.size()) activated = static_cast<int>(index);
        }

        if (activated >= 0) {
            LuminousStage customStage;
            std::string err;
            // 読めるかどうかだけ先に確かめ、**行き先にはパスを渡す**
            const std::string& path = rt.CustomStages[static_cast<size_t>(activated)].second;
            if (customStage.LoadFromFile(Engine::Common::Paths::ResolveString(path), err)) {
                Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                LuminousGoToStage(ctx.GetRegistry(), path);
                return;
            }
        }
        // BACK か戻るボタン
        if (activated == -3 || LuminousInput::CancelPressed()) {
            Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Cancel.FilePath, LuminousAudioConfig::SE_UI_Cancel.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
            rt.View = LuminousTitleView::StageSelect;
            return;
        }
    }

    // ========================================================================
    // 本編の実装
    // ========================================================================

    // ------------------------------------------------------------------------
    // 片付け (計画 18 の段 3)。
    //
    //   コードのシーンの OnTeardown だったもの。データのシーンには C++ の
    //   シーンが無いので、**Teardown フェーズのシステム**にした。
    //   `.scene.json` の "systems" に名前を並べれば、同じところで走る
    // ------------------------------------------------------------------------
    inline void LuminousPlayTeardown(const Engine::Core::SystemContext& ctx) {
        auto& rt = PlayRuntime(ctx.GetRegistry());
        Input::SetCursorLocked(false);
        if (auto* ui = ctx.FindService<Engine::UI::UISystem>()) {
            ui->Close(rt.HudUi);
            ui->Close(rt.PauseUi);
            ui->Close(rt.ClearUi);
            ui->Close(rt.IntroUi);
        }
        rt.HudUi = {};
        rt.PauseUi = {};
        rt.ClearUi = {};
        rt.IntroUi = {};

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
            LuminousTransition::Get().Draw(ctx.GetRegistry());
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
        const bool debugActive = rt.DebugTool.IsActive();
        const bool showGameplayHUD = !debugActive || rt.DebugTool.ShowGameHUD();
        const bool playing = !rt.Paused && !player.IsStageCleared;

        // 1〜4. HUD (UI の仕組み。Hud.ui.json。21 の U8 の 1・6):
        //   レティクル・調べる案内 (枠 + 操作中の機器のボタンの絵 + 行動名)・宝玉を持っている印 (右下)・操作説明の案内 (左下)
        if (auto* hud = ctx.FindService<Engine::UI::UISystem>()) {
            using Engine::UI::UIVisibility;
            if (!hud->IsOpen(rt.HudUi)) {
                rt.HudUi = hud->Open(LuminousScreenAssets::Hud, { .SortOrder = LuminousScreenOrder::Hud });
                rt.HudGlyphSet = -1;
            }
            const char* const kLabels[] = { "LabelTakeOrb", "LabelPlaceOrb", "LabelOpenChest" };
            if (rt.HudGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
                rt.HudGlyphSet = static_cast<int>(LuminousInputDevice::Current);
                LuminousUIScreens::ApplyHint(*hud, rt.HudUi, "PauseHint", { LuminousButtonGlyph::Pause, "PAUSE / CONTROLS" }, 34.0f);
                // 調べるボタンの絵 (機器で横の長さが変わる)。行動名はその右
                const float gh = 56.0f;
                const float gw = gh * LuminousInputDevice::GlyphAspect(LuminousButtonGlyph::Interact);
                const Engine::UI::UIElement glyph = hud->Find(rt.HudUi, "Glyph");
                hud->SetImage(glyph, LuminousInputDevice::GlyphPath(LuminousButtonGlyph::Interact));
                hud->SetSize(glyph, { gw, gh });
                for (const char* label : kLabels) hud->SetOffset(hud->Find(rt.HudUi, label), { 26.0f + gw + 22.0f, 10.0f });
            }
            const bool showHud = showGameplayHUD && playing;
            const bool showPrompt = showHud && player.CanInteract && player.InteractKind != LuminousInteractKind::None;
            const auto show = [&](const char* name, bool visible) {
                hud->SetVisibility(hud->Find(rt.HudUi, name), visible ? UIVisibility::Visible : UIVisibility::Collapsed);
            };
            show("Crosshair", showHud);
            show("Prompt", showPrompt);
            // 置く / 取る / 開ける で行動名の絵を出し分ける
            show(kLabels[0], player.InteractKind != LuminousInteractKind::PlaceOrb && player.InteractKind != LuminousInteractKind::OpenChest);
            show(kLabels[1], player.InteractKind == LuminousInteractKind::PlaceOrb);
            show(kLabels[2], player.InteractKind == LuminousInteractKind::OpenChest);
            show("OrbIcon", showHud && player.IsHoldingOrb);
            show("PauseHint", showHud);
        }

        // 5. ポーズメニュー (UI の仕組み。Pause.ui.json。21 の U8 の 2)
        if (auto* menu = ctx.FindService<Engine::UI::UISystem>()) {
            const bool pauseShown = rt.Paused && !player.IsStageCleared && !debugActive;
            if (!pauseShown && menu->IsOpen(rt.PauseUi)) {
                menu->Close(rt.PauseUi);
                rt.PauseUi = {};
            }
            if (pauseShown) {
                Input::SetCursorLocked(false);
                const char* const kButtons[] = { "Resume", "Restart", "StageSelect", "Editor", "Title" };
                if (!menu->IsOpen(rt.PauseUi)) {
                    rt.PauseUi = menu->Open(LuminousScreenAssets::Pause, { .SortOrder = LuminousScreenOrder::Menu });
                    rt.PauseGlyphSet = -1;
                    rt.PauseLastFocus = menu->Find(rt.PauseUi, kButtons[0]);
                    menu->SetFocus(rt.PauseLastFocus);
                }
                if (rt.PauseGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
                    rt.PauseGlyphSet = static_cast<int>(LuminousInputDevice::Current);
                    menu->SetText(menu->Find(rt.PauseUi, "DeviceName"), LuminousInputDevice::SetLabel(LuminousInputDevice::Current));
                    struct HintRow { const char* Name; LuminousHint Hint; };
                    const HintRow hints[] = {
                        { "HintMove",     { LuminousButtonGlyph::Move,     "MOVE" } },
                        { "HintLook",     { LuminousButtonGlyph::Look,     "LOOK" } },
                        { "HintInteract", { LuminousButtonGlyph::Interact, "TAKE / PLACE ORB" } },
                        { "HintPause",    { LuminousButtonGlyph::Pause,    "PAUSE" } },
                        { "HintConfirm",  { LuminousButtonGlyph::Confirm,  "DECIDE (MENU)" } },
                        { "HintCancel",   { LuminousButtonGlyph::Cancel,   "BACK (MENU)" } },
                    };
                    for (const HintRow& row : hints) LuminousUIScreens::ApplyHint(*menu, rt.PauseUi, row.Name, row.Hint, 52.0f);
                }

                // 選んでいるボタンが変わったら音 (マウスが乗っても、キーで動かしても)
                const Engine::UI::UIElement focus = menu->GetFocus();
                if (focus.Instance == rt.PauseUi && !(focus == rt.PauseLastFocus)) {
                    rt.PauseLastFocus = focus;
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                }
                // 押した (クリック・決定。前の UI の Update で起きたこと)
                int activated = -1;
                for (const Engine::UI::UIEventRecord& e : menu->GetEvents()) {
                    if (e.Type != Engine::UI::UIEventType::Clicked || !(e.Element.Instance == rt.PauseUi)) continue;
                    for (int i = 0; i < 5; ++i) {
                        if (e.Element == menu->Find(rt.PauseUi, kButtons[i])) activated = i;
                    }
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
                    LuminousGoToStage(registry, rt.StagePath, rt.StageNumber, rt.ReturnToEditorOnExit);
                    return;
                case 2:
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                    LuminousGoToStageSelect(registry);
                    return;
                case 3:
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                    // エディタは「いま遊んでいるステージ」を開く。
                    //   行き先はファイルなので、パスを預けて渡す (計画 18 の段 3)
                    Session(registry).StagePath = rt.StagePath;
                    LuminousTransition::Get().ChangeScene(LuminousScenes::Editor);
                    return;
                case 4:
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                    LuminousTransition::Get().ChangeScene(LuminousScenes::Title);
                    return;
                default:
                    break;
                }
            }
        }

        // 6. ステージクリアのリザルト (宝箱開口0.8秒後。UI の仕組み。Clear.ui.json。21 の U8 の 3)
        if (auto* menu = ctx.FindService<Engine::UI::UISystem>()) {
            const bool clearShown = player.IsStageCleared && rt.ChestAnimTimer >= 0.8f && (!debugActive || rt.DebugTool.ShowGameHUD());
            if (!clearShown && menu->IsOpen(rt.ClearUi)) {
                menu->Close(rt.ClearUi);
                rt.ClearUi = {};
            }
            if (clearShown) {
                if (!debugActive) Input::SetCursorLocked(false);
                const char* const kButtons[] = { "Next", "Retry", "StageSelect", "Title" };
                if (!rt.ClearModel) rt.ClearModel = std::make_unique<LuminousClearModel>();
                LuminousClearModel& model = *rt.ClearModel;
                model.StageName = rt.Stage.Name.empty() ? std::string("Custom Stage") : rt.Stage.Name;
                model.Minutes = static_cast<int>(rt.GameTime) / 60;
                model.Seconds = std::fmod(rt.GameTime, 60.0f);
                model.OrbPickups = player.OrbPickupCount;
                model.PedestalInserts = player.PedestalInsertCount;
                if (!menu->IsOpen(rt.ClearUi)) {
                    rt.ClearUi = menu->Open(LuminousScreenAssets::Clear, { .SortOrder = LuminousScreenOrder::Menu });
                    menu->Bind(rt.ClearUi, model);
                    rt.ClearGlyphSet = -1;
                    rt.ClearLastFocus = menu->Find(rt.ClearUi, kButtons[0]);
                    menu->SetFocus(rt.ClearLastFocus);
                }
                if (rt.ClearGlyphSet != static_cast<int>(LuminousInputDevice::Current)) {
                    rt.ClearGlyphSet = static_cast<int>(LuminousInputDevice::Current);
                    LuminousUIScreens::ApplyHint(*menu, rt.ClearUi, "HintChoose", { LuminousButtonGlyph::Move, "CHOOSE" }, 38.0f);
                    LuminousUIScreens::ApplyHint(*menu, rt.ClearUi, "HintDecide", { LuminousButtonGlyph::Confirm, "DECIDE" }, 38.0f);
                }

                // 選んでいるボタンが変わったら音 (マウスが乗っても、キーで動かしても)
                const Engine::UI::UIElement focus = menu->GetFocus();
                if (focus.Instance == rt.ClearUi && !(focus == rt.ClearLastFocus)) {
                    rt.ClearLastFocus = focus;
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Hover.FilePath, LuminousAudioConfig::SE_UI_Hover.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                }
                // 押した (クリック・決定。前の UI の Update で起きたこと)
                int activated = -1;
                for (const Engine::UI::UIEventRecord& e : menu->GetEvents()) {
                    if (e.Type != Engine::UI::UIEventType::Clicked || !(e.Element.Instance == rt.ClearUi)) continue;
                    for (int i = 0; i < 4; ++i) {
                        if (e.Element == menu->Find(rt.ClearUi, kButtons[i])) activated = i;
                    }
                }

                if (activated >= 0) {
                    Engine::Audio::AudioEngine::Get().Play2D(LuminousAudioConfig::SE_UI_Click.FilePath, LuminousAudioConfig::SE_UI_Click.DefaultVolume, 1.0f, Engine::Audio::SoundBus::UI);
                }
                switch (activated) {
                case 0: {
                    const int total = LuminousStageCatalog::Count();
                    if (rt.StageNumber >= 1 && rt.StageNumber < total) {
                        // 登録ステージの次へ
                        LuminousGoToBaseStage(registry, rt.StageNumber + 1, rt.ReturnToEditorOnExit);
                    } else if (rt.StageNumber >= total && total > 0) {
                        LuminousGoToStageSelect(registry);
                    } else if (rt.Stage.Name.find("Stage 1") != std::string::npos) {
                        // 確認用のステージも**ファイルになっている** (計画 18 の段 2)
                        LuminousGoToStage(registry, "Assets/Data/TestStages/multi_floor.json",
                                          0, rt.ReturnToEditorOnExit);
                    } else {
                        LuminousGoToStage(registry, "Assets/Data/TestStages/sample.json",
                                          0, rt.ReturnToEditorOnExit);
                    }
                    return;
                }
                case 1:
                    LuminousGoToStage(registry, rt.StagePath, rt.StageNumber, rt.ReturnToEditorOnExit);
                    return;
                case 2:
                    LuminousGoToStageSelect(registry);
                    return;
                case 3:
                    LuminousTransition::Get().ChangeScene(LuminousScenes::Title);
                    return;
                default:
                    break;
                }
            }
        }

        // 7. ステージ開始オーバーレイ (画面中央。UI の仕組み。StageIntro.ui.json。21 の U8 の 6)。
        //    時間の進め方は前のまま (StageIntroOverlay)。ポーズ・クリアより手前に出す
        if (auto* intro = ctx.FindService<Engine::UI::UISystem>()) {
            if (!intro->IsOpen(rt.IntroUi)) {
                rt.IntroUi = intro->Open(LuminousScreenAssets::StageIntro, { .SortOrder = LuminousScreenOrder::Intro });
            }
            const Engine::UI::UIElement title = intro->Find(rt.IntroUi, "Title");
            const float alpha = rt.StageIntro.Alpha();
            if (alpha > 0.002f) {
                intro->SetImage(title, rt.StageIntro.ImagePath());
                intro->SetColor(title, { 1.0f, 1.0f, 1.0f, alpha });
            }
            intro->SetVisibility(title, alpha > 0.002f ? Engine::UI::UIVisibility::Visible : Engine::UI::UIVisibility::Collapsed);
        }

        // 8. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(ctx.GetRegistry());
    }

    // ========================================================================
    // ステージエディタの実装
    // ========================================================================

    // ------------------------------------------------------------------------
    // 片付け (計画 18 の段 3)。
    //
    //   コードのシーンの OnTeardown だったもの。データのシーンには C++ の
    //   シーンが無いので、**Teardown フェーズのシステム**にした。
    //   `.scene.json` の "systems" に名前を並べれば、同じところで走る
    // ------------------------------------------------------------------------
    inline void LuminousEditorTeardown(const Engine::Core::SystemContext& ctx) {
        auto& rt = EditorRuntime(ctx.GetRegistry());
        Input::SetCursorLocked(false);
        if (auto* ui = ctx.FindService<Engine::UI::UISystem>()) ui->Close(rt.EditorUi);
        rt.EditorUi = {};
        auto& registry = ctx.GetRegistry();
#ifndef IMGUI_DISABLE
        // エンジンの ImGui の操作パネルを戻す (Shipping には ImGui もパネルも無い)
        if (auto* debug = Engine::Core::FindService<Engine::Debug::DebugSettings>(registry)) {
            debug->ShowEngineControlPanel = rt.SavedShowEnginePanel;
        }
#endif

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
#ifndef IMGUI_DISABLE
        // エンジンの ImGui の操作パネルは、エディタの間は隠す (Shipping には ImGui もパネルも無い)
        if (auto* debug = Engine::Core::FindService<Engine::Debug::DebugSettings>(registry)) {
            rt.SavedShowEnginePanel = debug->ShowEngineControlPanel;
            debug->ShowEngineControlPanel = false;
        }
#endif

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
    // ステージエディタからのテストプレイ (計画 18 の段 3)。
    //
    //   編集中のステージは**まだファイルになっていない**ので、そのままでは
    //   行き先 (データのシーン) に渡せない。一時ファイルへ書いてからパスを預ける。
    //   ファイルに残るので、落ちたときに何を試していたかも後から見られる
    inline void LuminousEditorTestPlay(entt::registry& registry) {
        auto& rt = EditorRuntime(registry);
        const std::string scratch = "Saved/EditorTestPlay.stage.json";
        std::string error;
        if (!rt.Editor.Stage.SaveToFile(Engine::Common::Paths::ResolveString(scratch), error)) {
            rt.Editor.SetStatusMessage("Cannot Test Play: " + error);
            ENGINE_LOG_ERROR("Luminous", "テストプレイ用の書き出しに失敗 '{}': {}", scratch, error);
            return;
        }
        LuminousGoToStage(registry, scratch, 0, /*returnToEditor*/ true);
    }

    inline void LuminousEditorBuildSystem(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        auto& rt = EditorRuntime(registry);
        if (rt.Camera != entt::null && registry.valid(rt.Camera)) return;   // もう建っている
        // シーンのファイルが「どの並びを開いて始めるか」を言っていれば、それに従う
        //   (コードのシーンでは作るときの引数だったもの。計画 18 の段 2)
        rt.ActivePaletteCategory = SceneSetting<LuminousEditorSettings>(registry).PaletteCategory;
        // 開発用の起動オプション --editor-walls など (以前は変種のシーンの殻が言っていた。AU-24)
        if (const std::optional<int> category = LuminousDevLaunch::TakePaletteCategory()) {
            rt.ActivePaletteCategory = *category;
        }

        // 本編から「このステージを開いて」と言われていれば、それを読む (計画 18 の段 3)。
        //   コードのシーンへ LuminousStage を丸ごと渡していた頃の代わり
        LuminousSession& session = Session(registry);
        if (!session.StagePath.empty()) {
            std::string error;
            if (rt.Editor.Stage.LoadFromFile(
                    Engine::Common::Paths::ResolveString(session.StagePath), error)) {
                ENGINE_LOG_INFO("Luminous", "ステージエディタで開きました '{}'", session.StagePath);
            } else {
                ENGINE_LOG_WARN("Luminous", "ステージエディタで開けません '{}': {}",
                                session.StagePath, error);
            }
            // **1 度きり**。次に開いたときまで持ち越さない
            session.StagePath.clear();
        }

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
            LuminousTransition::Get().Draw(ctx.GetRegistry());
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

        // マウスが画面の UI (StageEditor.ui.json の帯・パネル) の上なら、置く・選ぶをしない。
        //   UI の仕組みの当たりで見る (パネルは当たるので、下の 3D へ通さない)。3D のピッキングはゲームの仮想解像度
        auto* editorUi = ctx.FindService<Engine::UI::UISystem>();
        const bool isHoveringUI = editorUi && editorUi->WantsPointer();
        const auto& viewMapping = Input::GetViewportMapping();
        int screenW = (std::max)(1, static_cast<int>(viewMapping.VirtualWidth));
        int screenH = (std::max)(1, static_cast<int>(viewMapping.VirtualHeight));

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
                LuminousEditorTestPlay(registry);
            } else {
                rt.Editor.SetStatusMessage("Cannot Test Play: " + (val.Errors.empty() ? "Validation Failed" : val.Errors[0]));
            }
        }

        // 7. エディタUI描画
        LuminousEditorRenderUI(ctx);

        // 8. 画面全体のフェード (最前面)
        LuminousTransition::Get().Draw(ctx.GetRegistry());
    }

    // ------------------------------------------------------------------------
    // STAGE EDITOR の画面 (UI の仕組み。StageEditor.ui.json。21 の U9。前は ImGui)
    //
    //   上の帯 (道具・カメラ・階・テストプレイ・元に戻す・保存)、左 (ブラシとパレット / 選ぶときの案内)、
    //   右 (ステージ・階の一覧・選んだものを調べる・確かめる)、下の帯 (知らせ)。
    //   ボタン・欄はマウスで使う (フォーカスを取らない)。W/A/S/D・L はカメラなどに使うので、UI のナビゲーションにしない。
    //   毎フレーム、前の UI の Update で起きたこと (押した・値を変えた・文字を変えた) を先に行い、
    //   それからエディタの今の状態を画面へ写す (変わったところだけが描き直される)
    // ------------------------------------------------------------------------

    // パレット (カテゴリごとの置けるもの)
    struct LuminousPaletteItem {
        const char* Label;
        const char* AssetId;
        AssetCategory Category;
        PlacementType Placement;
    };
    inline const LuminousPaletteItem kPaletteFloors[] = {
        { "Floor_1x1 (Standard Stone)", "Floor_1x1", AssetCategory::Floor, PlacementType::CellSnap },
        { "Floor_PatternB (Patterned Cobble)", "Floor_PatternB", AssetCategory::Floor, PlacementType::CellSnap },
        { "Floor_SawBlade (Hazard Trap)", "Floor_SawBlade", AssetCategory::Floor, PlacementType::CellSnap },
        { "Floor_Spikes (Needle Trap)", "Floor_Spikes", AssetCategory::Floor, PlacementType::CellSnap },
        { "Floor_Tile (Decorative Tile)", "Floor_Tile", AssetCategory::Floor, PlacementType::FreeAttach },
    };
    inline const LuminousPaletteItem kPaletteWalls[] = {
        { "Wall_Brick (Solid Stone)", "Wall_Brick", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Smooth (Smooth Stone)", "Wall_Smooth", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Grate (Iron Fence - Pass Light)", "Wall_Grate", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Window (Arch Window)", "Wall_Window", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Railing (Low Railing)", "Wall_Railing", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_DoorArc (Archway Opening)", "Wall_DoorArc", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Door (Heavy Door)", "Wall_Door", AssetCategory::Wall, PlacementType::EdgeSnap },
        { "Wall_Ruined (Broken Wall)", "Wall_Ruined", AssetCategory::Wall, PlacementType::EdgeSnap },
    };
    inline const LuminousPaletteItem kPaletteStairs[] = {
        { "Stairs_Straight (1x2 Rise 3m)", "Stairs_Straight", AssetCategory::Stairs, PlacementType::CellSnap },
    };
    inline const LuminousPaletteItem kPalettePedestals[] = {
        { "Pedestal (Socket Pillar)", "Pedestal", AssetCategory::Pedestal, PlacementType::FreeAttach },
    };
    inline const LuminousPaletteItem kPaletteSpecials[] = {
        { "StartDais (Player Spawn Dais)", "StartDais", AssetCategory::Special, PlacementType::CellSnap },
        { "GoalChest (Treasure Goal)", "GoalChest", AssetCategory::Special, PlacementType::FreeAttach },
    };
    inline const LuminousPaletteItem kPaletteProps[] = {
        { "Wall Lamp (Prop_Torch)", "Prop_Torch", AssetCategory::Prop, PlacementType::EdgeSnap },
        { "Floor Lamp (Prop_Candle)", "Prop_Candle", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Pillar", "Prop_Pillar", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Barrel", "Prop_Barrel", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_WoodenBox", "Prop_WoodenBox", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Brick (Single Brick)", "Prop_Brick", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_BrickPile (Rubble Pile)", "Prop_BrickPile", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Rock", "Prop_Rock", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Debris", "Prop_Debris", AssetCategory::Prop, PlacementType::FreeAttach },
        { "Prop_Skull", "Prop_Skull", AssetCategory::Prop, PlacementType::FreeAttach },
    };
    inline std::span<const LuminousPaletteItem> LuminousPalette(int category) {
        switch (category) {
        case 0: return kPaletteFloors;
        case 1: return kPaletteWalls;
        case 2: return kPaletteStairs;
        case 3: return kPalettePedestals;
        case 4: return kPaletteSpecials;
        case 5: return kPaletteProps;
        default: return {};
        }
    }

    // 押したもの (名前で見る。一覧の中は「一覧.項目の枠の番号.名前」)
    inline void LuminousEditorOnClicked(const Engine::Core::SystemContext& ctx, Engine::UI::UISystem& ui,
                                        const std::string& name, Engine::UI::UIElement element) {
        auto& registry = ctx.GetRegistry();
        auto& rt = EditorRuntime(registry);
        auto& ed = rt.Editor;
        const auto refresh = [&] { LuminousEditorRefreshSceneEntities(registry); };
        // 書く場所・読む場所はプロジェクト基準 (欄の相対パスのまま開くと、エディタの Game ビューでは
        //   作業フォルダ = エディタの置き場へ書き、プロジェクトのステージを読めなかった)
        const auto save = [&](bool longMessage) {
            std::string err;
            if (ed.Stage.SaveToFile(Engine::Common::Paths::ResolveString(ed.CurrentFilePath), err)) {
                ed.SetStatusMessage(longMessage ? "Stage saved: " + ed.CurrentFilePath : std::string("Stage Saved Successfully!"));
            } else {
                ed.SetStatusMessage("Save Error: " + err);
            }
        };
        const auto load = [&](bool longMessage) {
            std::string err;
            if (ed.Stage.LoadFromFile(Engine::Common::Paths::ResolveString(ed.CurrentFilePath), err)) {
                ed.SetStatusMessage(longMessage ? "Stage loaded: " + ed.CurrentFilePath : std::string("Stage Loaded Successfully!"));
                refresh();
            } else {
                ed.SetStatusMessage("Load Error: " + err);
            }
        };

        if (name == "Paint") { ed.ToolMode = EditorToolMode::Paint; refresh(); }
        else if (name == "Select") { ed.ToolMode = EditorToolMode::Select; refresh(); }
        else if (name == "Camera") {
            ed.ToggleCameraMode();
            if (ed.CameraMode == EditorCameraMode::TopDown) Input::SetCursorLocked(false);
            refresh();
        }
        else if (name == "FloorPrev") { if (ed.CurrentFloor > ed.Stage.GetMinFloor()) { ed.CurrentFloor--; refresh(); } }
        else if (name == "FloorNext") { if (ed.CurrentFloor < ed.Stage.GetMaxFloor()) { ed.CurrentFloor++; refresh(); } }
        else if (name == "TestPlay") {
            const auto val = ed.Validate();
            if (val.IsValid) LuminousEditorTestPlay(registry);
            else ed.SetStatusMessage("Cannot Test Play: " + (val.Errors.empty() ? std::string("Validation Failed") : val.Errors[0]));
        }
        else if (name == "Undo") { if (ed.Undo()) refresh(); }
        else if (name == "Redo") { if (ed.Redo()) refresh(); }
        else if (name == "SaveJson") save(false);
        else if (name == "LoadJson") load(false);
        else if (name == "SaveStage") save(true);
        else if (name == "LoadStage") load(true);
        else if (name == "Title") LuminousTransition::Get().ChangeScene(LuminousScenes::Title);
        else if (name == "BrushRotate") ed.RotateBrush();
        else if (name.size() == 4 && name.starts_with("Cat")) rt.ActivePaletteCategory = name[3] - '0';
        else if (name == "AddAbove") { ed.AddFloorAbove(); refresh(); }
        else if (name == "AddBelow") { ed.AddFloorBelow(); refresh(); }
        else if (name == "InsMoveFloor") { if (ed.MoveSelectedObjectToFloor(ed.CurrentFloor)) refresh(); }
        else if (name == "InsRotate") { if (ed.RotateSelectedObject(90.0f)) refresh(); }
        else if (name == "InsDelete") { if (ed.DeleteSelectedObject()) refresh(); }
        else if (name.size() == 7 && name.starts_with("Facing")) {
            if (auto* obj = ed.GetSelectedObjectMut()) obj->Facing = static_cast<SpawnFacing>(name[6] - '0');
        }
        else if (name.starts_with("Assets.")) {
            const auto palette = LuminousPalette(rt.ActivePaletteCategory);
            const uint32_t index = ui.GetListIndex(element);
            if (index < palette.size()) ed.SelectAsset(palette[index].AssetId, palette[index].Category, palette[index].Placement);
        }
        else if (name.starts_with("Floors.") && !name.ends_with(".Visible")) {
            // 階の一覧は上の階から並べている (見える / 見えないの切り替えは値が変わったところで)
            auto& floors = ed.Stage.Floors;
            const uint32_t index = ui.GetListIndex(element);
            if (index >= floors.size()) return;
            const int floorIndex = floors[floors.size() - 1 - index].FloorIndex;
            ed.CurrentFloor = floorIndex;
            if (name.ends_with(".Up")) { if (ed.MoveCurrentFloorUp()) refresh(); }
            else if (name.ends_with(".Down")) { if (ed.MoveCurrentFloorDown()) refresh(); }
            else if (name.ends_with(".Delete")) { if (floors.size() > 1 && ed.DeleteCurrentFloor()) refresh(); }
            else refresh();
        }
    }

    // 値を変えた (切り替え・数値の欄)
    inline void LuminousEditorOnValueChanged(const Engine::Core::SystemContext& ctx, Engine::UI::UISystem& ui,
                                             const std::string& name, Engine::UI::UIElement element, float value) {
        auto& registry = ctx.GetRegistry();
        auto& rt = EditorRuntime(registry);
        auto& ed = rt.Editor;
        const auto refresh = [&] { LuminousEditorRefreshSceneEntities(registry); };
        const bool on = value != 0.0f;
        auto* obj = ed.GetSelectedObjectMut();

        if (name == "BrushPhase") ed.SelectedPhase = on ? MaterialPhase::Phase : MaterialPhase::Normal;
        else if (name == "BrushOrb") ed.BrushInitialOrb = on;
        else if (name == "InsPhase") { if (obj && (obj->Phase == MaterialPhase::Phase) != on && ed.ToggleSelectedPhase()) refresh(); }
        else if (name == "InsOrb") { if (obj && obj->HasInitialOrb != on && ed.ToggleSelectedInitialOrb()) refresh(); }
        else if (name == "InsLight") { if (obj) { obj->HasLight = on; refresh(); } }
        else if (name == "LightR") { if (obj) { obj->LightColor.x = value; refresh(); } }
        else if (name == "LightG") { if (obj) { obj->LightColor.y = value; refresh(); } }
        else if (name == "LightB") { if (obj) { obj->LightColor.z = value; refresh(); } }
        else if (name == "LightIntensity") { if (obj) { obj->LightIntensity = value; refresh(); } }
        else if (name == "LightRadius") { if (obj) { obj->LightRadius = value; refresh(); } }
        else if (name.starts_with("Floors.") && name.ends_with(".Visible")) {
            auto& floors = ed.Stage.Floors;
            const uint32_t index = ui.GetListIndex(element);
            if (index < floors.size()) {
                floors[floors.size() - 1 - index].Visible = on;
                refresh();
            }
        }
    }

    inline void LuminousEditorRenderUI(const Engine::Core::SystemContext& ctx) {
        auto& registry = ctx.GetRegistry();
        auto& rt = EditorRuntime(registry);
        auto* ui = ctx.FindService<Engine::UI::UISystem>();
        if (!ui) return;
        using Engine::UI::UIElement;
        using Engine::UI::UIColor;
        using Engine::UI::UIVisibility;
        auto& ed = rt.Editor;

        if (!ui->IsOpen(rt.EditorUi)) {
            rt.EditorUi = ui->Open(LuminousScreenAssets::StageEditor, { .SortOrder = LuminousScreenOrder::Hud });
            rt.PaletteShown = -1;
            rt.PaletteSelectedShown.clear();
            rt.FloorsShown.clear();
            // 一覧の中身 (見えている行だけ。中の名前は項目の根から引く)
            entt::registry* reg = &registry;
            ui->OnListItem(ui->Find(rt.EditorUi, "Assets"), [ui, reg](const Engine::UI::UIListItem& item) {
                auto& r = EditorRuntime(*reg);
                const auto palette = LuminousPalette(r.ActivePaletteCategory);
                if (item.Index >= palette.size()) return;
                ui->SetText(ui->FindChild(item.Root, "Label"), palette[item.Index].Label);
                ui->SetValue(item.Root, r.Editor.SelectedAssetId == palette[item.Index].AssetId ? 1.0f : 0.0f);
            });
            ui->OnListItem(ui->Find(rt.EditorUi, "Floors"), [ui, reg](const Engine::UI::UIListItem& item) {
                auto& r = EditorRuntime(*reg);
                const auto& floors = r.Editor.Stage.Floors;
                if (item.Index >= floors.size()) return;
                const auto& flr = floors[floors.size() - 1 - item.Index];   // 上の階から
                const bool current = r.Editor.CurrentFloor == flr.FloorIndex;
                ui->SetValue(ui->FindChild(item.Root, "Visible"), flr.Visible ? 1.0f : 0.0f);
                ui->SetValue(ui->FindChild(item.Root, "Pick"), current ? 1.0f : 0.0f);
                const UIElement label = ui->FindChild(item.Root, "PickLabel");
                ui->SetText(label, std::format("{}F: {}", flr.FloorIndex + 1, flr.Name));
                // 今の階は黄色 (文字の色 0.92, 0.94, 0.97 に掛けて 1, 0.85, 0.2)
                ui->SetColor(label, current ? UIColor{ 1.0f / 0.92f, 0.85f / 0.94f, 0.2f / 0.97f, 1.0f } : UIColor{ 1.0f, 1.0f, 1.0f, 1.0f });
                ui->SetVisibility(ui->FindChild(item.Root, "Delete"), floors.size() > 1 ? UIVisibility::Visible : UIVisibility::Hidden);
            });
        }
        const auto el = [&](const char* name) { return ui->Find(rt.EditorUi, name); };
        const auto show = [&](const char* name, bool visible) {
            ui->SetVisibility(el(name), visible ? UIVisibility::Visible : UIVisibility::Collapsed);
        };
        const auto setText = [&](const char* name, const std::string& text) { ui->SetText(el(name), text); };
        const auto setOn = [&](const char* name, bool on) {
            const UIElement e = el(name);
            if ((ui->GetValue(e) != 0.0f) != on) ui->SetValue(e, on ? 1.0f : 0.0f);
        };
        const auto setNumber = [&](const char* name, float value) {
            const UIElement e = el(name);
            if (!(ui->GetEditing() == e) && std::abs(ui->GetValue(e) - value) > 1.0e-5f) ui->SetValue(e, value);
        };
        // 文字の色 (EdText の 0.92, 0.94, 0.97 に掛けて、その色にする)
        const auto textColor = [&](const char* name, float r, float g, float b) {
            ui->SetColor(el(name), { r / 0.92f, g / 0.94f, b / 0.97f, 1.0f });
        };

        // 1. 前の UI の Update で起きたこと
        if (const Engine::UI::UIDocument* doc = ui->GetDocument(rt.EditorUi)) {
            for (const Engine::UI::UIEventRecord& e : ui->GetEvents()) {
                if (!(e.Element.Instance == rt.EditorUi)) continue;
                const std::string& name = doc->Name[e.Element.Row];
                switch (e.Type) {
                case Engine::UI::UIEventType::Clicked:
                    LuminousEditorOnClicked(ctx, *ui, name, e.Element);
                    break;
                case Engine::UI::UIEventType::ValueChanged:
                    LuminousEditorOnValueChanged(ctx, *ui, name, e.Element, e.Value);
                    break;
                case Engine::UI::UIEventType::TextChanged:
                case Engine::UI::UIEventType::TextCommitted:
                    if (name == "StageName") ed.Stage.Name = std::string(ui->GetText(e.Element));
                    else if (name == "StagePath") ed.CurrentFilePath = std::string(ui->GetText(e.Element));
                    break;
                default:
                    break;
                }
            }
        }
        // 打ち終えた欄のフォーカスは外す (W/A/S/D・L が UI の操作にならないように)
        if (!ui->IsEditingText() && ui->GetFocus().Instance == rt.EditorUi) ui->ClearFocus();

        // 2. 上の帯
        const bool isPaint = ed.ToolMode == EditorToolMode::Paint;
        const bool isFlycam = ed.CameraMode == EditorCameraMode::Flycam3D;
        setOn("Paint", isPaint);
        setOn("Select", ed.ToolMode == EditorToolMode::Select);
        setOn("Camera", isFlycam);
        setText("CameraLabel", isFlycam ? "[TAB] 3D FLYCAM" : "[TAB] TOP-DOWN");
        const auto* curFlrCfg = ed.Stage.FindFloor(ed.CurrentFloor);
        const std::string flrName = curFlrCfg ? curFlrCfg->Name : ("Floor " + std::to_string(ed.CurrentFloor + 1));
        setText("FloorLabel", std::format(" [ {} F : {} ] ", ed.CurrentFloor + 1, flrName));
        const auto valRes = ed.Validate();
        setOn("TestPlay", valRes.IsValid);   // 出せるときは緑

        // 3. 左 (ブラシとパレット / 選ぶときの案内)
        show("PaintPanel", isPaint);
        show("SelectPanel", !isPaint);
        if (isPaint) {
            setText("BrushAsset", "Asset: " + ed.SelectedAssetId);
            const bool isEdgeSnap = ed.SelectedPlacement == PlacementType::EdgeSnap || ed.SelectedCategory == AssetCategory::Wall;
            if (isEdgeSnap) {
                setText("BrushSnap", "Snap: EdgeSnap (Auto Align)");
                setText("BrushRotateLabel", ed.BrushWallFlipYaw > 90.0f ? "Flip Face 180 deg (R) [180 deg]" : "Flip Face 180 deg (R) [0 deg]");
            } else {
                setText("BrushSnap", std::format("Snap: {} | Yaw: {:.0f} deg",
                    ed.SelectedPlacement == PlacementType::CellSnap ? "CellSnap" : "FreeAttach", ed.BrushYaw));
                setText("BrushRotateLabel", "Rotate 90 deg (R)");
            }
            const bool floorOrWall = ed.SelectedCategory == AssetCategory::Floor || ed.SelectedCategory == AssetCategory::Wall;
            show("BrushPhase", floorOrWall);
            setOn("BrushPhase", ed.SelectedPhase == MaterialPhase::Phase);
            const bool isPedestal = ed.SelectedAssetId == "Pedestal";
            show("BrushOrb", isPedestal);
            show("BrushOrbCount", isPedestal);
            if (isPedestal) {
                const int32_t orbCount = ed.CountInitialOrbs();
                const bool orbLimitReached = orbCount >= MAX_STAGE_ORBS;
                if (orbLimitReached) ed.BrushInitialOrb = false;
                ui->SetEnabled(el("BrushOrb"), !orbLimitReached);
                setOn("BrushOrb", ed.BrushInitialOrb);
                setText("BrushOrbCount", std::format("Orbs: {} / {}{}", orbCount, MAX_STAGE_ORBS, orbLimitReached ? "  (limit reached)" : ""));
                if (orbLimitReached) textColor("BrushOrbCount", 1.0f, 0.45f, 0.35f);
                else textColor("BrushOrbCount", 0.65f, 0.75f, 0.85f);
            }
            for (int i = 0; i < 6; ++i) setOn(std::format("Cat{}", i).c_str(), rt.ActivePaletteCategory == i);
            const UIElement assets = el("Assets");
            if (rt.PaletteShown != rt.ActivePaletteCategory) {
                rt.PaletteShown = rt.ActivePaletteCategory;
                ui->SetListCount(assets, static_cast<uint32_t>(LuminousPalette(rt.ActivePaletteCategory).size()));
                ui->ScrollListTo(assets, 0);
                ui->RefreshList(assets);
                rt.PaletteSelectedShown = ed.SelectedAssetId;
            } else if (rt.PaletteSelectedShown != ed.SelectedAssetId) {
                rt.PaletteSelectedShown = ed.SelectedAssetId;
                ui->RefreshList(assets);
            }
        }

        // 4. 右: ステージ (打っている欄はそのまま。読み込みなどで変わったら入れ直す)
        const UIElement nameField = el("StageName");
        if (!(ui->GetEditing() == nameField) && ui->GetText(nameField) != ed.Stage.Name) ui->SetText(nameField, ed.Stage.Name);
        const UIElement pathField = el("StagePath");
        if (!(ui->GetEditing() == pathField) && ui->GetText(pathField) != ed.CurrentFilePath) ui->SetText(pathField, ed.CurrentFilePath);

        // 右: 階の一覧 (中身が変わったときだけ入れ直す)
        std::string floorsKey = std::to_string(ed.CurrentFloor);
        for (const auto& flr : ed.Stage.Floors) floorsKey += std::format("|{}:{}:{}", flr.FloorIndex, flr.Name, flr.Visible ? 1 : 0);
        if (floorsKey != rt.FloorsShown) {
            rt.FloorsShown = floorsKey;
            const UIElement floorsList = el("Floors");
            ui->SetListCount(floorsList, static_cast<uint32_t>(ed.Stage.Floors.size()));
            ui->RefreshList(floorsList);
        }

        // 右: 選んだものを調べる
        auto* selectedObj = ed.GetSelectedObjectMut();
        show("Inspector", selectedObj != nullptr);
        show("InspectorNone", selectedObj == nullptr);
        if (selectedObj) {
            setText("InsAsset", "Asset: " + selectedObj->AssetId);
            setText("InsId", std::format("Instance ID: #{}", selectedObj->InstanceId));
            setText("InsFloor", std::format("Floor: {} F", selectedObj->FloorIndex + 1));
            show("InsMoveFloor", selectedObj->FloorIndex != ed.CurrentFloor);
            setText("InsMoveFloorLabel", std::format("Move to Active Floor ({}F)", ed.CurrentFloor + 1));
            setText("InsPos", std::format("Pos: ({:.2f}, {:.2f}, {:.2f})", selectedObj->Position.x, selectedObj->Position.y, selectedObj->Position.z));
            setText("InsYaw", std::format("Yaw: {:.0f} deg", selectedObj->Rotation.y));
            show("InsPhase", selectedObj->Category == AssetCategory::Floor || selectedObj->Category == AssetCategory::Wall);
            setOn("InsPhase", selectedObj->Phase == MaterialPhase::Phase);
            const bool isDais = selectedObj->AssetId == "StartDais";
            show("InsFacingBox", isDais);
            if (isDais) {
                for (int i = 0; i < 4; ++i) setOn(std::format("Facing{}", i).c_str(), static_cast<int>(selectedObj->Facing) == i);
                setText("InsFacingYaw", std::format("Yaw at spawn: {:.0f} deg", SpawnFacingToYawDegrees(selectedObj->Facing)));
            }
            const bool isPedestal = selectedObj->AssetId == "Pedestal";
            show("InsOrbBox", isPedestal);
            if (isPedestal) {
                const int32_t selOrbCount = ed.CountInitialOrbs();
                ui->SetEnabled(el("InsOrb"), selectedObj->HasInitialOrb || selOrbCount < MAX_STAGE_ORBS);
                setOn("InsOrb", selectedObj->HasInitialOrb);
                setText("InsOrbCount", std::format("Stage Orbs: {} / {}", selOrbCount, MAX_STAGE_ORBS));
                if (selOrbCount >= MAX_STAGE_ORBS) textColor("InsOrbCount", 1.0f, 0.45f, 0.35f);
                else textColor("InsOrbCount", 0.65f, 0.75f, 0.85f);
            }
            setOn("InsLight", selectedObj->HasLight);
            show("InsLightBox", selectedObj->HasLight);
            if (selectedObj->HasLight) {
                setNumber("LightR", selectedObj->LightColor.x);
                setNumber("LightG", selectedObj->LightColor.y);
                setNumber("LightB", selectedObj->LightColor.z);
                ui->SetColor(el("LightSwatch"), { selectedObj->LightColor.x, selectedObj->LightColor.y, selectedObj->LightColor.z, 1.0f });
                setNumber("LightIntensity", selectedObj->LightIntensity);
                setNumber("LightRadius", selectedObj->LightRadius);
            }
        }

        // 右: 確かめる
        setText("ValStart", std::format("Start Points: {} / 1", valRes.StartPointCount));
        setText("ValGoal", std::format("Goal Points:  {} / 1", valRes.GoalPointCount));
        setText("ValOrbs", std::format("Initial Orbs: {}", valRes.InitialOrbCount));
        setText("ValTotal", std::format("Total Objects: {}", ed.Stage.Objects.size()));
        if (valRes.IsValid) {
            setText("ValStatus", "Status: VALID (Ready to Play)");
            textColor("ValStatus", 0.2f, 0.9f, 0.3f);
        } else {
            setText("ValStatus", "Status: INVALID");
            textColor("ValStatus", 1.0f, 0.3f, 0.2f);
        }
        std::string errors;
        for (const auto& err : valRes.Errors) {
            if (!errors.empty()) errors += '\n';
            errors += "- " + err;
        }
        setText("ValErrors", errors);
        show("ValErrors", !errors.empty());

        // 5. 下の帯
        const char* modeStr = isPaint ? "PAINT" : "SELECT";
        const char* camStr = isFlycam ? "3D FLYCAM" : "TOP-DOWN";
        if (ed.HasFloorHit) {
            const char* edge = ed.HoverEdge == CellEdge::North ? "N" : ed.HoverEdge == CellEdge::South ? "S" : ed.HoverEdge == CellEdge::East ? "E" : "W";
            setText("Status", std::format("[{} | {}] Cursor: ({}, {}, {}F) | Edge: {} | {}", modeStr, camStr,
                ed.HoverCell.X, ed.HoverCell.Z, ed.HoverCell.Floor + 1, edge, ed.StatusMessage));
        } else {
            setText("Status", std::format("[{} | {}] Cursor: (Out of bounds) | {}", modeStr, camStr, ed.StatusMessage));
        }
    }
}
