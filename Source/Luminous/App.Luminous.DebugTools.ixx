module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <array>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <entt/entt.hpp>

#ifndef IMGUI_DISABLE
#include <imGui/imgui.h>
#endif

export module App.Luminous.DebugTools;

import Engine.Core.Components.Transform;
import Engine.Core.EngineState;
import Engine.Graphics.Components.Camera;
import Engine.Graphics.Components.Light;
import Engine.Debug.Core;
import Engine.Input;
import Engine.Math;
import App.Graphics.ParticleData;
import App.Graphics.RenderSettings;
import App.Luminous.Types;
import App.Luminous.Optics;
import App.Luminous.TerrainSystem;
import App.Luminous.PlayerSystem;
import App.Luminous.StageData;
import App.Luminous.InputConfig;
import Engine.Core.Services;

export namespace App::Luminous {

    using namespace DirectX;
    using namespace Engine::Input;

    // ========================================================================
    // デバッグツールがプレイシーンの状態へアクセスするための参照束
    // ========================================================================
    struct LuminousDebugContext {
        entt::registry& Registry;
        LuminousStage& Stage;
        LuminousPlayerComponent& Player;
        std::vector<OrbRuntimeState>& Orbs;
        const std::vector<OcclusionGeometry>& Occluders;
        const std::vector<PedestalColliderBake>& Bakes;
        entt::entity CameraEntity;
        float GameTime;
        size_t GuidanceParticleCount;
    };

#ifndef IMGUI_DISABLE
    // ========================================================================
    // 3D ワイヤーフレームを ImGui の背景描画リストへ投影して描く (深度テストなし)
    // ========================================================================
    class DebugOverlayDrawer {
    public:
        void Begin(const XMMATRIX& viewProj, const XMFLOAT3& cameraPos, float maxDistance) {
            viewProj_ = viewProj;
            cameraPos_ = cameraPos;
            maxDistance_ = maxDistance;
            mapping_ = Input::GetViewportMapping();
            list_ = ImGui::GetBackgroundDrawList();
        }

        bool InRange(const XMFLOAT3& p, float margin = 0.0f) const {
            const float dx = p.x - cameraPos_.x, dy = p.y - cameraPos_.y, dz = p.z - cameraPos_.z;
            const float r = maxDistance_ + margin;
            return dx * dx + dy * dy + dz * dz <= r * r;
        }

        void Line(const XMFLOAT3& a, const XMFLOAT3& b, ImU32 color, float thickness = 1.5f) {
            if (!list_) return;
            XMVECTOR ca = XMVector4Transform(XMVectorSet(a.x, a.y, a.z, 1.0f), viewProj_);
            XMVECTOR cb = XMVector4Transform(XMVectorSet(b.x, b.y, b.z, 1.0f), viewProj_);
            // 近クリップ面 (z >= 0) で切る
            const float za = XMVectorGetZ(ca), zb = XMVectorGetZ(cb);
            if (za < 0.0f && zb < 0.0f) return;
            if (za < 0.0f) ca = XMVectorLerp(ca, cb, za / (za - zb));
            else if (zb < 0.0f) cb = XMVectorLerp(cb, ca, zb / (zb - za));
            ImVec2 pa, pb;
            if (!ToScreen(ca, pa) || !ToScreen(cb, pb)) return;
            list_->AddLine(pa, pb, color, thickness);
        }

        void Box(float minX, float minY, float minZ, float maxX, float maxY, float maxZ, ImU32 color, float thickness = 1.5f) {
            const XMFLOAT3 c[8] = {
                { minX, minY, minZ }, { maxX, minY, minZ }, { maxX, minY, maxZ }, { minX, minY, maxZ },
                { minX, maxY, minZ }, { maxX, maxY, minZ }, { maxX, maxY, maxZ }, { minX, maxY, maxZ } };
            for (int i = 0; i < 4; ++i) {
                Line(c[i], c[(i + 1) % 4], color, thickness);
                Line(c[i + 4], c[(i + 1) % 4 + 4], color, thickness);
                Line(c[i], c[i + 4], color, thickness);
            }
        }

        // ローカル箱 (XZ 平面で Yaw 回転) を描く
        void OrientedBox(const XMFLOAT3& origin, float yawRad, float minLX, float maxLX, float minY, float maxY, float minLZ, float maxLZ, ImU32 color) {
            const float c = std::cos(yawRad), s = std::sin(yawRad);
            auto w = [&](float lx, float y, float lz) {
                return XMFLOAT3(origin.x + lx * c + lz * s, origin.y + y, origin.z - lx * s + lz * c);
            };
            const XMFLOAT3 p[8] = {
                w(minLX, minY, minLZ), w(maxLX, minY, minLZ), w(maxLX, minY, maxLZ), w(minLX, minY, maxLZ),
                w(minLX, maxY, minLZ), w(maxLX, maxY, minLZ), w(maxLX, maxY, maxLZ), w(minLX, maxY, maxLZ) };
            for (int i = 0; i < 4; ++i) {
                Line(p[i], p[(i + 1) % 4], color);
                Line(p[i + 4], p[(i + 1) % 4 + 4], color);
                Line(p[i], p[i + 4], color);
            }
        }

        void CircleY(const XMFLOAT3& center, float radius, ImU32 color, int segments = 32, float thickness = 1.5f) {
            XMFLOAT3 prev(center.x + radius, center.y, center.z);
            for (int i = 1; i <= segments; ++i) {
                const float a = XM_2PI * static_cast<float>(i) / segments;
                XMFLOAT3 cur(center.x + std::cos(a) * radius, center.y, center.z + std::sin(a) * radius);
                Line(prev, cur, color, thickness);
                prev = cur;
            }
        }

        void Sphere(const XMFLOAT3& center, float radius, ImU32 color, int segments = 40) {
            CircleY(center, radius, color, segments);
            for (int plane = 0; plane < 2; ++plane) {
                XMFLOAT3 prev = plane == 0 ? XMFLOAT3(center.x + radius, center.y, center.z) : XMFLOAT3(center.x, center.y, center.z + radius);
                for (int i = 1; i <= segments; ++i) {
                    const float a = XM_2PI * static_cast<float>(i) / segments;
                    XMFLOAT3 cur = plane == 0
                        ? XMFLOAT3(center.x + std::cos(a) * radius, center.y + std::sin(a) * radius, center.z)
                        : XMFLOAT3(center.x, center.y + std::sin(a) * radius, center.z + std::cos(a) * radius);
                    Line(prev, cur, color);
                    prev = cur;
                }
            }
        }

        void Cylinder(const XMFLOAT3& base, float radius, float height, ImU32 color) {
            CircleY(base, radius, color, 20);
            CircleY(XMFLOAT3(base.x, base.y + height, base.z), radius, color, 20);
            for (int i = 0; i < 4; ++i) {
                const float a = XM_PIDIV2 * i;
                XMFLOAT3 p(base.x + std::cos(a) * radius, base.y, base.z + std::sin(a) * radius);
                Line(p, XMFLOAT3(p.x, p.y + height, p.z), color);
            }
        }

        void Point(const XMFLOAT3& p, ImU32 color, float size = 3.0f) {
            ImVec2 s;
            if (Project(p, s)) list_->AddCircleFilled(s, size, color, 8);
        }

        void Text(const XMFLOAT3& p, const char* text, ImU32 color) {
            ImVec2 s;
            if (Project(p, s)) list_->AddText(s, color, text);
        }

        bool Project(const XMFLOAT3& p, ImVec2& out) const {
            if (!list_) return false;
            XMVECTOR c = XMVector4Transform(XMVectorSet(p.x, p.y, p.z, 1.0f), viewProj_);
            if (XMVectorGetZ(c) < 0.0f) return false;
            return ToScreen(c, out);
        }

    private:
        bool ToScreen(FXMVECTOR clip, ImVec2& out) const {
            const float w = XMVectorGetW(clip);
            if (w <= 1e-5f) return false;
            const float nx = XMVectorGetX(clip) / w;
            const float ny = XMVectorGetY(clip) / w;
            const Engine::Math::Vector2 v((nx * 0.5f + 0.5f) * mapping_.VirtualWidth, (0.5f - ny * 0.5f) * mapping_.VirtualHeight);
            const Engine::Math::Vector2 c = mapping_.VirtualToClient(v);
            out = ImVec2(c.x, c.y);
            return true;
        }

        XMMATRIX viewProj_ = XMMatrixIdentity();
        XMFLOAT3 cameraPos_ = { 0, 0, 0 };
        float maxDistance_ = 30.0f;
        ViewportMapping mapping_;
        ImDrawList* list_ = nullptr;
    };
#endif

    // ========================================================================
    // LuminousDebugTool (プレイ中 F12)
    // ------------------------------------------------------------------------
    //   - コリジョン / 光遮蔽形状 / 反転マテリアルの判定状態の可視化
    //   - 宝玉の影響範囲 (球・遮蔽体・レイ・サンプル点・床の穴円) の詳細表示
    //   - スペクテイターカメラ、その位置へのプレイヤーテレポート
    //     (テレポートせずに F12 で抜けると、プレイヤーは元の場所のまま)
    //   - 時間の停止 / スロー / コマ送り
    //   - ブルーム・モーションブラー等のグラフィック設定とゲームプレイ調整値
    // ========================================================================
    class LuminousDebugTool {
    public:
        bool IsActive() const { return active_; }
        bool ShowGameHUD() const { return showGameHUD_; }
        bool DisableOrbOcclusionAtlas() const { return disableOrbOcclusion_; }
        bool IsSpectating() const { return spectator_; }
        // デバッグ中にプレイヤーを操作できるか (既定 ON。Ctrl でツール操作モードへ、スペクテイター中は OFF)
        bool PlayerControlEnabled() const { return active_ && playerControl_ && !spectator_; }

        // 時間操作を適用した dt
        float ScaleDeltaTime(float dt) {
            if (stepFrames_ > 0) {
                --stepFrames_;
                return 1.0f / 60.0f;
            }
            if (freezeWorld_) return 0.0f;
            return dt * timeScale_;
        }

        void Activate(LuminousDebugContext& ctx) {
            active_ = true;
            spectator_ = false;
            looking_ = false;
            const auto& p = ctx.Player;
            spectatorPos_ = XMFLOAT3(p.Position.x, p.Position.y + p.EyeHeight, p.Position.z);
            spectatorYaw_ = p.Yaw;
            spectatorPitch_ = p.Pitch;
            if (auto* s = Engine::Core::FindService<Engine::Debug::DebugSettings>(ctx.Registry)) {
                savedShowEnginePanel_ = s->ShowEngineControlPanel;
                s->ShowEngineControlPanel = false;
            }
            playerControl_ = true;
            Input::SetCursorLocked(true);
        }

        void Deactivate(LuminousDebugContext& ctx) {
            Shutdown(ctx.Registry);
        }

        // シーン終了時にも呼ぶ (エンジン設定を元へ戻す)
        void Shutdown(entt::registry& registry) {
            if (!active_) return;
            active_ = false;
            spectator_ = false;
            looking_ = false;
            if (auto* s = Engine::Core::FindService<Engine::Debug::DebugSettings>(registry)) {
                s->ShowEngineControlPanel = savedShowEnginePanel_;
            }
#ifndef IMGUI_DISABLE
            if (ImGui::GetCurrentContext() != nullptr) {
                ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
            }
#endif
        }

        // デバッグ中のショートカット (毎フレーム呼ぶ)
        //   Ctrl : プレイヤー操作モード <-> ツール操作モード (プレイヤー停止・デバッグメニューを操作)
        //   C    : スペクテイターカメラ切り替え
        //   T    : スペクテイターの位置へプレイヤーをテレポート
        void HandleHotkeys(LuminousDebugContext& ctx) {
            if (playerControl_ || !ImGuiWantsKeyboard()) {
                if (Input::GetKeyDown(KeyCode::CTRL)) {
                    playerControl_ = !playerControl_;
                    looking_ = false;
                }
                if (Input::GetKeyDown(KeyCode::C)) SetSpectator(ctx, !spectator_);
                if (Input::GetKeyDown(KeyCode::T) && spectator_) TeleportPlayerToSpectator(ctx);
            }
            ApplyCursorLock();
        }

        // プレイヤーを操作しないフレーム (ツール操作モード / スペクテイター) のカメラ制御
        void UpdateCamera(LuminousDebugContext& ctx, Engine::Core::TransformComponent& cam, float realDt) {
            const bool wantKeyboard = !playerControl_ && ImGuiWantsKeyboard();

            if (!spectator_) {
                const auto& p = ctx.Player;
                cam.LocalPosition = XMFLOAT3(p.Position.x, p.Position.y + p.EyeHeight, p.Position.z);
                XMStoreFloat4(&cam.LocalRotation, XMQuaternionRotationRollPitchYaw(p.Pitch, p.Yaw, 0.0f));
                cam.IsDirty = true;
                looking_ = false;
                ApplyCursorLock();
                return;
            }

            // --- 視点: プレイヤー操作モードはマウス移動そのまま、ツール操作モードは右ドラッグ
            if (playerControl_) {
                looking_ = false;
                const auto d = Input::GetMouseDelta();
                spectatorYaw_ += d.x * lookSensitivity_;
                spectatorPitch_ += d.y * lookSensitivity_;
            } else {
                const bool rmb = Input::GetKeyHold(KeyCode::MOUSE_RIGHT);
                if (rmb && (looking_ || !ImGuiWantsMouse())) {
                    if (looking_) {
                        const auto d = Input::GetMouseDelta();
                        spectatorYaw_ += d.x * lookSensitivity_;
                        spectatorPitch_ += d.y * lookSensitivity_;
                    }
                    looking_ = true;
                } else {
                    looking_ = false;
                }
            }
            const auto rs = Input::GetStick(StickCode::RIGHT, 0.2f, Input::ANY_PAD);
            spectatorYaw_ += rs.x * g_LuminousConfig.PadLookSpeedX * realDt;
            spectatorPitch_ -= rs.y * g_LuminousConfig.PadLookSpeedY * realDt;
            spectatorPitch_ = std::clamp(spectatorPitch_, -XMConvertToRadians(89.0f), XMConvertToRadians(89.0f));

            // --- 速度: ホイール
            if (playerControl_ || looking_ || !ImGuiWantsMouse()) {
                const float wheel = Input::GetMouseWheel();
                if (wheel != 0.0f) spectatorSpeed_ = std::clamp(spectatorSpeed_ * std::pow(1.15f, wheel), 0.25f, 60.0f);
            }

            // --- 移動: WASD + Q/E, 左スティック + LB/RB
            float f = 0.0f, r = 0.0f, u = 0.0f;
            float speed = spectatorSpeed_;
            if (!wantKeyboard) {
                if (Input::GetKeyHold(KeyCode::W)) f += 1.0f;
                if (Input::GetKeyHold(KeyCode::S)) f -= 1.0f;
                if (Input::GetKeyHold(KeyCode::D)) r += 1.0f;
                if (Input::GetKeyHold(KeyCode::A)) r -= 1.0f;
                if (Input::GetKeyHold(KeyCode::E)) u += 1.0f;
                if (Input::GetKeyHold(KeyCode::Q)) u -= 1.0f;
                if (Input::GetKeyHold(KeyCode::SHIFT)) speed *= 4.0f;
                if (Input::GetKeyHold(KeyCode::ALT)) speed *= 0.25f;
            }
            const auto ls = Input::GetStick(StickCode::LEFT, 0.2f, Input::ANY_PAD);
            f += ls.y; r += ls.x;
            if (Input::GetButtonHold(PadCode::RIGHT_SHOULDER, Input::ANY_PAD)) u += 1.0f;
            if (Input::GetButtonHold(PadCode::LEFT_SHOULDER, Input::ANY_PAD)) u -= 1.0f;

            const float sy = std::sin(spectatorYaw_), cy = std::cos(spectatorYaw_);
            const float sp = std::sin(spectatorPitch_), cp = std::cos(spectatorPitch_);
            const XMFLOAT3 fwd(sy * cp, -sp, cy * cp);
            const XMFLOAT3 right(cy, 0.0f, -sy);
            const float step = speed * realDt;
            spectatorPos_.x += (fwd.x * f + right.x * r) * step;
            spectatorPos_.y += (fwd.y * f + u) * step;
            spectatorPos_.z += (fwd.z * f + right.z * r) * step;

            cam.LocalPosition = spectatorPos_;
            XMStoreFloat4(&cam.LocalRotation, XMQuaternionRotationRollPitchYaw(spectatorPitch_, spectatorYaw_, 0.0f));
            cam.IsDirty = true;
            ApplyCursorLock();
        }

        // プレイヤー操作モード中と、ツール操作モードで右ドラッグ中だけカーソルをロックする
        void ApplyCursorLock() {
            Input::SetCursorLocked(playerControl_ || looking_);
        }

        void DrawUI(LuminousDebugContext& ctx, float realDt);

    private:
        static bool ImGuiWantsKeyboard() {
#ifndef IMGUI_DISABLE
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureKeyboard;
#else
            return false;
#endif
        }
        static bool ImGuiWantsMouse() {
#ifndef IMGUI_DISABLE
            return ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().WantCaptureMouse;
#else
            return false;
#endif
        }

        void SetSpectator(LuminousDebugContext& ctx, bool on) {
            if (on == spectator_) return;
            spectator_ = on;
            if (on) {
                const auto& p = ctx.Player;
                spectatorPos_ = XMFLOAT3(p.Position.x, p.Position.y + p.EyeHeight, p.Position.z);
                spectatorYaw_ = p.Yaw;
                spectatorPitch_ = p.Pitch;
            }
        }

        // スペクテイターカメラの位置へプレイヤーを移動し、足元の床へ接地させる
        void TeleportPlayerToSpectator(LuminousDebugContext& ctx) {
            auto& p = ctx.Player;
            XMFLOAT3 feet(spectatorPos_.x, spectatorPos_.y - p.EyeHeight, spectatorPos_.z);
            bool onStairs = false;
            const float ground = PlayerSystem::EvaluateGroundHeight(
                feet, spectatorPos_.y, ctx.Stage.Objects, ctx.Orbs, ctx.Occluders, ctx.Bakes, onStairs);
            if (ground > -50.0f && ground <= spectatorPos_.y) feet.y = ground;
            p.Position = feet;
            p.CurrentFloorHeight = feet.y;
            p.Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
            p.IsGliding = false;
            p.Yaw = spectatorYaw_;
            if (p.Yaw < 0.0f) p.Yaw += XM_2PI * std::ceil(-p.Yaw / XM_2PI);
            p.Yaw = std::fmod(p.Yaw, XM_2PI);
            p.Pitch = std::clamp(spectatorPitch_, -XMConvertToRadians(88.0f), XMConvertToRadians(88.0f));
            lastTeleportMessageTimer_ = 2.5f;
        }

        void DrawOverlay(LuminousDebugContext& ctx);
        void DrawCollisionOverlay(LuminousDebugContext& ctx);
        void DrawPhaseOverlay(LuminousDebugContext& ctx);
        void DrawOrbOverlay(LuminousDebugContext& ctx);

        void TabOverview(LuminousDebugContext& ctx);
        void TabPlayer(LuminousDebugContext& ctx);
        void TabSpectator(LuminousDebugContext& ctx);
        void TabCollision(LuminousDebugContext& ctx);
        void TabPhase(LuminousDebugContext& ctx);
        void TabOrbs(LuminousDebugContext& ctx);
        void TabGraphics(LuminousDebugContext& ctx);
        void TabObjects(LuminousDebugContext& ctx);

        bool active_ = false;
        bool spectator_ = false;
        bool looking_ = false;
        XMFLOAT3 spectatorPos_ = { 0, 0, 0 };
        float spectatorYaw_ = 0.0f;
        float spectatorPitch_ = 0.0f;
        float spectatorSpeed_ = 5.0f;
        float lookSensitivity_ = 0.0025f;
        float lastTeleportMessageTimer_ = 0.0f;

        bool savedShowEnginePanel_ = true;
        bool showGameHUD_ = true;
        bool playerControl_ = true;
        bool disableOrbOcclusion_ = false;

        bool freezeWorld_ = false;
        float timeScale_ = 1.0f;
        int stepFrames_ = 0;

        std::array<float, 240> frameTimes_ = {};
        size_t frameCursor_ = 0;

        // 表示設定
        float overlayDistance_ = 25.0f;
        bool overlayCurrentFloorOnly_ = false;
        bool showWalls_ = true;
        bool showStairs_ = true;
        bool showPedestals_ = true;
        bool showProps_ = false;
        bool showChest_ = true;
        bool showPlayerCollider_ = true;
        bool showFloorTiles_ = false;
        bool showOptics_ = false;
        bool showHoleMasks_ = true;
        bool showWallSpans_ = true;
        bool showPhaseSamples_ = false;
        int phaseSampleDensity_ = 8;
        bool showOrbSpheres_ = true;
        bool showOrbOccluders_ = false;
        bool showOrbRays_ = false;
        int orbRayCount_ = 64;
        bool showOrbSamples_ = false;
        float orbSampleStep_ = 0.8f;
        bool showOrbFloorCircles_ = true;
        int selectedObjectIndex_ = -1;
        char objectFilter_[64] = "";
    };

#ifndef IMGUI_DISABLE
    inline DebugOverlayDrawer g_LuminousDebugOverlay;

    namespace DebugToolDetail {
        inline constexpr ImU32 ColIce = IM_COL32(220, 244, 253, 255);
        inline constexpr ImU32 ColWall = IM_COL32(255, 110, 90, 220);
        inline constexpr ImU32 ColPhase = IM_COL32(230, 90, 255, 230);
        inline constexpr ImU32 ColGrate = IM_COL32(160, 160, 170, 200);
        inline constexpr ImU32 ColOpen = IM_COL32(80, 230, 255, 230);
        inline constexpr ImU32 ColSpan = IM_COL32(120, 255, 140, 230);
        inline constexpr ImU32 ColStairs = IM_COL32(255, 220, 90, 220);
        inline constexpr ImU32 ColPedestal = IM_COL32(255, 160, 60, 220);
        inline constexpr ImU32 ColPlayer = IM_COL32(90, 255, 120, 255);
        inline constexpr ImU32 ColSelect = IM_COL32(255, 255, 255, 255);

        inline const char* CategoryName(AssetCategory c) {
            switch (c) {
            case AssetCategory::Floor: return "Floor";
            case AssetCategory::Wall: return "Wall";
            case AssetCategory::Stairs: return "Stairs";
            case AssetCategory::Pedestal: return "Pedestal";
            case AssetCategory::Special: return "Special";
            case AssetCategory::Prop: return "Prop";
            }
            return "?";
        }

        inline const char* CarrierName(const OrbRuntimeState& o) {
            if (o.IsAnimating) return o.TargetCarrier == OrbCarrier::Player ? "Flying -> Player" : "Flying -> Pedestal";
            switch (o.Carrier) {
            case OrbCarrier::Player: return "Player";
            case OrbCarrier::Pedestal: return "Pedestal";
            default: return "Unbound";
            }
        }

        inline const char* VendorName(ControllerVendor v) {
            switch (v) {
            case ControllerVendor::Xbox: return "Xbox (XInput)";
            case ControllerVendor::Sony_PlayStation: return "PlayStation (DirectInput)";
            case ControllerVendor::Nintendo_Switch: return "Switch Pro (DirectInput)";
            case ControllerVendor::Generic: return "Generic (DirectInput)";
            default: return "Unknown";
            }
        }

        inline const char* InteractName(LuminousInteractKind k) {
            switch (k) {
            case LuminousInteractKind::TakeOrb: return "Take Orb";
            case LuminousInteractKind::PlaceOrb: return "Place Orb";
            case LuminousInteractKind::OpenChest: return "Open Chest";
            default: return "None";
            }
        }

        inline void ToolTip(const char* text) {
            ImGui::SameLine();
            ImGui::TextDisabled("(?)");
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
        }
    }

    inline void LuminousDebugTool::DrawUI(LuminousDebugContext& ctx, float realDt) {
        frameTimes_[frameCursor_] = realDt * 1000.0f;
        {
            // プレイヤー操作モード中は ImGui がマウスを奪わない (視点操作と干渉させない)
            ImGuiIO& flagsIo = ImGui::GetIO();
            if (playerControl_) flagsIo.ConfigFlags |= ImGuiConfigFlags_NoMouse;
            else flagsIo.ConfigFlags &= ~ImGuiConfigFlags_NoMouse;
        }
        frameCursor_ = (frameCursor_ + 1) % frameTimes_.size();
        if (lastTeleportMessageTimer_ > 0.0f) lastTeleportMessageTimer_ -= realDt;

        DrawOverlay(ctx);

        ImGuiIO& io = ImGui::GetIO();
        auto* fg = ImGui::GetForegroundDrawList();
        char banner[256];
        const char* modeText = playerControl_
            ? "PLAY MODE  [Ctrl] tool mode"
            : "TOOL MODE (player stopped, menus active)  [Ctrl] play mode";
        const char* cameraText = spectator_
            ? (playerControl_ ? "SPECTATOR  [C] player view  [T] teleport  [Mouse] look  [WASD/QE] fly"
                              : "SPECTATOR  [C] player view  [T] teleport  [RMB] look  [WASD/QE] fly")
            : "PLAYER VIEW  [C] spectator";
        std::snprintf(banner, sizeof(banner), "DEBUG  [F12] exit  |  %s  |  %s  |  %.1f FPS%s",
            modeText, cameraText, io.Framerate, freezeWorld_ ? "  |  WORLD FROZEN" : "");
        const ImVec2 ts = ImGui::CalcTextSize(banner);
        fg->AddRectFilled(ImVec2(8, 8), ImVec2(8 + ts.x + 20, 36), IM_COL32(8, 18, 32, 215), 4.0f);
        fg->AddRect(ImVec2(8, 8), ImVec2(8 + ts.x + 20, 36), IM_COL32(220, 244, 253, 120), 4.0f);
        fg->AddText(ImVec2(18, 14), DebugToolDetail::ColIce, banner);
        if (lastTeleportMessageTimer_ > 0.0f) {
            fg->AddText(ImVec2(18, 44), IM_COL32(120, 255, 160, 255), "Player teleported to the spectator position. Exit with F12 to play from here.");
        }

        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 490.0f, 50.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(ImVec2(480.0f, io.DisplaySize.y - 70.0f), ImGuiCond_FirstUseEver);
        if (playerControl_) ImGui::SetNextWindowBgAlpha(0.55f);
        if (ImGui::Begin("Luvia Debug Tools")) {
            if (ImGui::BeginTabBar("LuviaDebugTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                if (ImGui::BeginTabItem("Overview"))  { TabOverview(ctx);  ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Player"))    { TabPlayer(ctx);    ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Spectator")) { TabSpectator(ctx); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Collision")) { TabCollision(ctx); ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Phase"))     { TabPhase(ctx);     ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Orbs"))      { TabOrbs(ctx);      ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Graphics"))  { TabGraphics(ctx);  ImGui::EndTabItem(); }
                if (ImGui::BeginTabItem("Objects"))   { TabObjects(ctx);   ImGui::EndTabItem(); }
                ImGui::EndTabBar();
            }
        }
        ImGui::End();
    }

    inline void LuminousDebugTool::TabOverview(LuminousDebugContext& ctx) {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::SeparatorText("Frame");
        float sum = 0.0f, worst = 0.0f;
        for (float v : frameTimes_) { sum += v; worst = (std::max)(worst, v); }
        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "avg %.2f ms / worst %.2f ms", sum / frameTimes_.size(), worst);
        ImGui::PlotLines("##frametimes", frameTimes_.data(), static_cast<int>(frameTimes_.size()),
            static_cast<int>(frameCursor_), overlay, 0.0f, 40.0f, ImVec2(-1.0f, 70.0f));
        ImGui::Text("FPS %.1f   Game time %.2f s", io.Framerate, ctx.GameTime);

        ImGui::SeparatorText("Time");
        ImGui::Checkbox("Freeze world", &freezeWorld_);
        ImGui::SameLine();
        if (ImGui::Button("Step 1 frame")) stepFrames_ = 1;
        ImGui::SliderFloat("Time scale", &timeScale_, 0.0f, 4.0f, "%.2fx");
        ImGui::SameLine();
        if (ImGui::SmallButton("1x")) timeScale_ = 1.0f;
        ImGui::Checkbox("Show game HUD", &showGameHUD_);

        ImGui::SeparatorText("Stage");
        ImGui::Text("Name: %s", ctx.Stage.Name.empty() ? "(unnamed)" : ctx.Stage.Name.c_str());
        int counts[6] = {};
        int phaseCount = 0;
        for (const auto& o : ctx.Stage.Objects) {
            counts[static_cast<int>(o.Category)]++;
            if (o.Phase == MaterialPhase::Phase) ++phaseCount;
        }
        ImGui::Text("Objects %zu  (Floor %d, Wall %d, Stairs %d, Pedestal %d, Special %d, Prop %d)",
            ctx.Stage.Objects.size(), counts[0], counts[1], counts[2], counts[3], counts[4], counts[5]);
        ImGui::Text("Phase objects %d   Orbs %zu   Optics occluders %zu", phaseCount, ctx.Orbs.size(), ctx.Occluders.size());
        size_t particleCount = 0;
        if (const auto* list = ctx.Registry.ctx().find<App::Graphics::ParticleDrawList>()) particleCount = list->Instances.size();
        ImGui::Text("Particles drawn %zu (guidance %zu)   Entities %zu",
            particleCount, ctx.GuidanceParticleCount, ctx.Registry.storage<entt::entity>().size());

        ImGui::SeparatorText("Input");
        ImGui::Text("Active device: %s", LuminousInputDevice::SetLabel(LuminousInputDevice::Current));
        for (int i = 0; i < 4; ++i) {
            if (Input::IsPadConnected(i)) {
                ImGui::Text("Pad %d: %s", i, DebugToolDetail::VendorName(Input::GetPadVendor(i)));
            }
        }
        if (!Input::IsPadConnected(Input::ANY_PAD)) ImGui::TextDisabled("No controller connected");
        const auto mv = Input::GetMousePosition();
        const auto mc = Input::GetMouseClientPosition();
        ImGui::Text("Mouse virtual (%.0f, %.0f)  client (%.0f, %.0f)", mv.x, mv.y, mc.x, mc.y);

        ImGui::SeparatorText("Display");
        if (const auto* s = Engine::Core::FindService<Engine::Core::EngineStateData>(ctx.Registry)) {
            const auto& m = Input::GetViewportMapping();
            ImGui::Text("Render %.0fx%.0f   Backbuffer %.0fx%.0f   %s",
                s->ResolutionX, s->ResolutionY, s->BackBufferWidth, s->BackBufferHeight, s->IsFullscreen ? "Fullscreen" : "Windowed");
            ImGui::Text("Present rect (%.0f, %.0f) %.0fx%.0f   [Alt+Enter] toggle", m.ContentX, m.ContentY, m.ContentWidth, m.ContentHeight);
        }
    }

    inline void LuminousDebugTool::TabPlayer(LuminousDebugContext& ctx) {
        auto& p = ctx.Player;
        ImGui::SeparatorText("Transform");
        ImGui::DragFloat3("Position", &p.Position.x, 0.02f);
        float yawDeg = XMConvertToDegrees(p.Yaw);
        float pitchDeg = XMConvertToDegrees(p.Pitch);
        if (ImGui::SliderFloat("Yaw", &yawDeg, 0.0f, 360.0f, "%.1f deg")) p.Yaw = XMConvertToRadians(yawDeg);
        if (ImGui::SliderFloat("Pitch", &pitchDeg, -88.0f, 88.0f, "%.1f deg")) p.Pitch = XMConvertToRadians(pitchDeg);
        const float speed = std::sqrt(p.Velocity.x * p.Velocity.x + p.Velocity.z * p.Velocity.z);
        ImGui::Text("Velocity (%.2f, %.2f, %.2f)  |%.2f| m/s", p.Velocity.x, p.Velocity.y, p.Velocity.z, speed);
        const GridCoord cell = WorldToGrid(p.Position);
        ImGui::Text("Cell (%d, %d)  Floor %dF  Ground %.3f", cell.X, cell.Z, cell.Floor + 1, p.CurrentFloorHeight);

        ImGui::SeparatorText("State");
        ImGui::Text("Grounded %s  Stairs %s  Gliding %s  Repelled %s",
            p.IsGrounded ? "yes" : "no", p.IsOnStairs ? "yes" : "no", p.IsGliding ? "yes" : "no", p.IsBeingRepelled ? "yes" : "no");
        ImGui::Text("Holding orb %s (id %llu)", p.IsHoldingOrb ? "yes" : "no", static_cast<unsigned long long>(p.HeldOrbId));
        ImGui::Text("Interact: %s  target #%llu", DebugToolDetail::InteractName(p.InteractKind), static_cast<unsigned long long>(p.TargetInstanceId));
        ImGui::Text("Pickups %d  Inserts %d  Cleared %s", p.OrbPickupCount, p.PedestalInsertCount, p.IsStageCleared ? "yes" : "no");

        auto teleport = [&](const XMFLOAT3& feet, float yaw) {
            p.Position = feet;
            p.Velocity = XMFLOAT3(0.0f, 0.0f, 0.0f);
            p.IsGliding = false;
            p.Yaw = yaw;
            p.Pitch = 0.0f;
            if (spectator_) {
                spectatorPos_ = XMFLOAT3(feet.x, feet.y + p.EyeHeight, feet.z);
                spectatorYaw_ = yaw;
                spectatorPitch_ = 0.0f;
            }
        };

        ImGui::SeparatorText("Teleport");
        for (const auto& o : ctx.Stage.Objects) {
            if (o.AssetId == "StartDais") {
                if (ImGui::Button("Start dais")) {
                    teleport(XMFLOAT3(o.Position.x, o.Position.y + 0.1f, o.Position.z), XMConvertToRadians(SpawnFacingToYawDegrees(o.Facing)));
                }
                ImGui::SameLine();
            } else if (o.AssetId == "GoalChest") {
                if (ImGui::Button("Goal chest")) {
                    teleport(XMFLOAT3(o.Position.x - 1.2f, o.Position.y, o.Position.z - 1.2f), XMConvertToRadians(45.0f));
                }
                ImGui::SameLine();
            }
        }
        ImGui::NewLine();
        int pedIndex = 0;
        for (const auto& o : ctx.Stage.Objects) {
            if (o.Category != AssetCategory::Pedestal) continue;
            char label[48];
            std::snprintf(label, sizeof(label), "Pedestal #%llu", static_cast<unsigned long long>(o.InstanceId));
            if (ImGui::Button(label)) teleport(XMFLOAT3(o.Position.x, o.Position.y, o.Position.z - 1.3f), 0.0f);
            if (++pedIndex % 3 != 0) ImGui::SameLine();
        }
        ImGui::NewLine();

        ImGui::SeparatorText("Tuning");
        ImGui::SliderFloat("Move speed", &g_LuminousConfig.MoveSpeed, 0.5f, 15.0f, "%.2f m/s");
        ImGui::SliderFloat("Interact distance", &g_LuminousConfig.InteractionDistance, 0.5f, 6.0f, "%.2f m");
        ImGui::SliderFloat("Mouse sensitivity", &p.LookSpeed, 0.0005f, 0.01f, "%.4f");
        ImGui::SliderFloat("Pad look speed X", &g_LuminousConfig.PadLookSpeedX, 0.5f, 10.0f, "%.2f rad/s");
        ImGui::SliderFloat("Pad look speed Y", &g_LuminousConfig.PadLookSpeedY, 0.5f, 10.0f, "%.2f rad/s");
        ImGui::SliderFloat("Pad look response curve", &g_LuminousConfig.PadLookResponseExponent, 0.5f, 3.0f, "%.2f");
        ImGui::SliderFloat("Head bob intensity", &g_LuminousConfig.CameraShakeIntensity, 0.0f, 3.0f);
        ImGui::SliderFloat("Head bob speed", &g_LuminousConfig.CameraShakeSpeed, 0.0f, 2.0f);
        ImGui::SliderFloat("Eye height", &p.EyeHeight, 0.5f, 2.5f, "%.2f m");
        if (auto* cam = ctx.Registry.try_get<Engine::Graphics::CameraComponent>(ctx.CameraEntity)) {
            float fov = XMConvertToDegrees(cam->FovY);
            if (ImGui::SliderFloat("Field of view", &fov, 30.0f, 120.0f, "%.0f deg")) cam->FovY = XMConvertToRadians(fov);
        }
    }

    inline void LuminousDebugTool::TabSpectator(LuminousDebugContext& ctx) {
        bool on = spectator_;
        if (ImGui::Checkbox("Spectator camera  [C]", &on)) SetSpectator(ctx, on);
        ImGui::TextWrapped("The player stays where it was while you fly. Leaving debug mode with F12 without teleporting returns the view to the original player position.");
        ImGui::BeginDisabled(!spectator_);
        if (ImGui::Button("Teleport player here  [T]", ImVec2(-1.0f, 0.0f))) TeleportPlayerToSpectator(ctx);
        if (ImGui::Button("Snap spectator to player", ImVec2(-1.0f, 0.0f))) {
            spectatorPos_ = XMFLOAT3(ctx.Player.Position.x, ctx.Player.Position.y + ctx.Player.EyeHeight, ctx.Player.Position.z);
            spectatorYaw_ = ctx.Player.Yaw;
            spectatorPitch_ = ctx.Player.Pitch;
        }
        ImGui::DragFloat3("Camera position", &spectatorPos_.x, 0.05f);
        float yawDeg = XMConvertToDegrees(spectatorYaw_);
        float pitchDeg = XMConvertToDegrees(spectatorPitch_);
        if (ImGui::DragFloat("Camera yaw", &yawDeg, 0.5f)) spectatorYaw_ = XMConvertToRadians(yawDeg);
        if (ImGui::SliderFloat("Camera pitch", &pitchDeg, -89.0f, 89.0f)) spectatorPitch_ = XMConvertToRadians(pitchDeg);
        ImGui::EndDisabled();
        ImGui::SliderFloat("Fly speed", &spectatorSpeed_, 0.25f, 60.0f, "%.2f m/s", ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Look sensitivity", &lookSensitivity_, 0.0005f, 0.01f, "%.4f");

        ImGui::SeparatorText("Controls");
        ImGui::BulletText("Right mouse drag / right stick: look");
        ImGui::BulletText("W A S D / left stick: move, E / RB: up, Q / LB: down");
        ImGui::BulletText("Shift: x4 speed, Alt: x0.25 speed, wheel: base speed");
        ImGui::BulletText("Ctrl: switch play mode (mouse look) / tool mode (right drag look, menus)");
    }

    inline void LuminousDebugTool::TabGraphics(LuminousDebugContext& ctx) {
        if (auto* pipelineSettings = App::Graphics::FindDeferredDebugSettings(ctx.Registry)) {
            App::Graphics::DrawDeferredSettingsUI(ctx.Registry, *pipelineSettings);
            ImGui::Checkbox("Disable orb occlusion atlas (phase materials ignore shadows)", &disableOrbOcclusion_);
        }
        if (auto* s = Engine::Core::FindService<Engine::Debug::DebugSettings>(ctx.Registry)) {
            ImGui::Checkbox("Show engine control panel", &s->ShowEngineControlPanel);
        }

        ImGui::SeparatorText("Lighting");
        if (ImGui::SliderFloat("Ambient intensity", &g_LuminousConfig.AmbientLightIntensity, 0.0f, 3.0f)) {
            for (auto [e, light] : ctx.Registry.view<Engine::Graphics::DirectionalLightComponent>().each()) {
                light.Intensity = g_LuminousConfig.AmbientLightIntensity;
            }
        }
        ImGui::SliderFloat("Orb light intensity", &g_LuminousConfig.OrbLightIntensity, 0.0f, 120.0f);
        ImGui::SliderFloat("Orb shadow near clip", &g_LuminousConfig.OrbLightNearClip, 0.01f, 1.0f);
        ImGui::SliderFloat("Lamp shadow near clip", &g_LuminousConfig.PropLightNearClip, 0.01f, 1.0f);
    }

    inline void LuminousDebugTool::TabObjects(LuminousDebugContext& ctx) {
        ImGui::InputTextWithHint("##filter", "filter by asset id", objectFilter_, sizeof(objectFilter_));
        ImGui::SameLine();
        if (ImGui::Button("Clear")) { objectFilter_[0] = '\0'; selectedObjectIndex_ = -1; }

        auto& objects = ctx.Stage.Objects;
        if (ImGui::BeginChild("objlist", ImVec2(0.0f, 300.0f), ImGuiChildFlags_Borders)) {
            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                const auto& o = objects[static_cast<size_t>(i)];
                if (objectFilter_[0] != '\0' && o.AssetId.find(objectFilter_) == std::string::npos) continue;
                char label[160];
                std::snprintf(label, sizeof(label), "#%llu  %s  [%s]  %dF%s##obj%d",
                    static_cast<unsigned long long>(o.InstanceId), o.AssetId.c_str(), DebugToolDetail::CategoryName(o.Category),
                    o.FloorIndex + 1, o.Phase == MaterialPhase::Phase ? "  PHASE" : "", i);
                if (ImGui::Selectable(label, selectedObjectIndex_ == i)) selectedObjectIndex_ = i;
            }
        }
        ImGui::EndChild();

        if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(objects.size())) {
            auto& o = objects[static_cast<size_t>(selectedObjectIndex_)];
            ImGui::SeparatorText("Selected (highlighted in viewport)");
            ImGui::Text("#%llu %s  %s  Floor %dF", static_cast<unsigned long long>(o.InstanceId), o.AssetId.c_str(),
                DebugToolDetail::CategoryName(o.Category), o.FloorIndex + 1);
            ImGui::Text("Position (%.3f, %.3f, %.3f)  Yaw %.0f", o.Position.x, o.Position.y, o.Position.z, o.Rotation.y);
            ImGui::Text("Phase %s  Light %s", o.Phase == MaterialPhase::Phase ? "yes" : "no", o.HasLight ? "yes" : "no");
            if (ImGui::Button("Spectate this object", ImVec2(-1.0f, 0.0f))) {
                SetSpectator(ctx, true);
                spectatorPos_ = XMFLOAT3(o.Position.x - 2.5f, o.Position.y + 2.2f, o.Position.z - 2.5f);
                spectatorYaw_ = XMConvertToRadians(45.0f);
                spectatorPitch_ = XMConvertToRadians(30.0f);
            }
        }
    }

    // ------------------------------------------------------------------------
    // ビューポートオーバーレイ
    // ------------------------------------------------------------------------
    inline void LuminousDebugTool::DrawOverlay(LuminousDebugContext& ctx) {
        auto* trans = ctx.Registry.try_get<Engine::Core::TransformComponent>(ctx.CameraEntity);
        auto* cam = ctx.Registry.try_get<Engine::Graphics::CameraComponent>(ctx.CameraEntity);
        if (!trans || !cam) return;

        const XMMATRIX world = XMMatrixRotationQuaternion(XMLoadFloat4(&trans->LocalRotation)) *
                               XMMatrixTranslation(trans->LocalPosition.x, trans->LocalPosition.y, trans->LocalPosition.z);
        const XMMATRIX view = XMMatrixInverse(nullptr, world);
        const XMMATRIX proj = XMMatrixPerspectiveFovLH(cam->FovY, cam->ResolvedAspectRatio, cam->NearZ, cam->FarZ);
        g_LuminousDebugOverlay.Begin(view * proj, trans->LocalPosition, overlayDistance_);

        DrawCollisionOverlay(ctx);
        DrawPhaseOverlay(ctx);
        DrawOrbOverlay(ctx);

        const auto& objects = ctx.Stage.Objects;
        if (selectedObjectIndex_ >= 0 && selectedObjectIndex_ < static_cast<int>(objects.size())) {
            const auto& o = objects[static_cast<size_t>(selectedObjectIndex_)];
            auto& d = g_LuminousDebugOverlay;
            const float h = (o.Category == AssetCategory::Floor) ? 0.1f : GRID_FLOOR_HEIGHT;
            d.OrientedBox(o.Position, XMConvertToRadians(o.Rotation.y), -1.1f, 1.1f, -0.05f, h, -1.1f, 1.1f, DebugToolDetail::ColSelect);
            d.Text(XMFLOAT3(o.Position.x, o.Position.y + h + 0.2f, o.Position.z), o.AssetId.c_str(), DebugToolDetail::ColSelect);
        }
    }

    inline void LuminousDebugTool::DrawCollisionOverlay(LuminousDebugContext& ctx) {
        using namespace DebugToolDetail;
        auto& d = g_LuminousDebugOverlay;
        const auto& p = ctx.Player;
        const int viewFloor = WorldToGrid(spectator_ ? XMFLOAT3(spectatorPos_.x, spectatorPos_.y - p.EyeHeight, spectatorPos_.z) : p.Position).Floor;

        for (const auto& o : ctx.Stage.Objects) {
            if (overlayCurrentFloorOnly_ && o.FloorIndex != viewFloor) continue;
            if (!d.InRange(o.Position, 4.0f)) continue;
            const float yaw = XMConvertToRadians(o.Rotation.y);

            if (o.Category == AssetCategory::Stairs || o.AssetId == "Stairs_Straight") {
                if (!showStairs_) continue;
                d.OrientedBox(o.Position, yaw, -1.053f, 1.053f, 0.0f, GRID_FLOOR_HEIGHT, -2.106f, 2.106f, ColStairs);
                const float c = std::cos(yaw), s = std::sin(yaw);
                for (float lx : { -1.053f, 1.053f }) {
                    XMFLOAT3 a(o.Position.x + lx * c - 2.106f * s, o.Position.y, o.Position.z - lx * s - 2.106f * c);
                    XMFLOAT3 b(o.Position.x + lx * c + 2.106f * s, o.Position.y + GRID_FLOOR_HEIGHT, o.Position.z - lx * s + 2.106f * c);
                    d.Line(a, b, ColStairs, 2.5f);
                }
            } else if (o.Category == AssetCategory::Pedestal) {
                if (showPedestals_) d.Cylinder(o.Position, 0.45f, 1.25f, ColPedestal);
            } else if (o.AssetId == "GoalChest") {
                if (showChest_) d.OrientedBox(o.Position, yaw, -0.68f, 0.68f, 0.0f, 1.1f, -0.48f, 0.48f, ColPedestal);
            } else if (o.Category == AssetCategory::Special && o.AssetId != "StartDais") {
                if (showChest_) d.Box(o.Position.x - 0.55f, o.Position.y, o.Position.z - 0.45f, o.Position.x + 0.55f, o.Position.y + 1.1f, o.Position.z + 0.45f, ColPedestal);
            } else if (o.Category == AssetCategory::Prop) {
                if (showProps_) d.Cylinder(o.Position, 0.40f, 1.20f, IM_COL32(200, 200, 120, 180));
            } else if (o.Category == AssetCategory::Floor) {
                if (!showFloorTiles_) continue;
                const float half = GRID_CELL_SIZE * 0.5f;
                const float y = o.Position.y + 0.02f;
                const ImU32 col = o.Phase == MaterialPhase::Phase ? ColPhase : IM_COL32(220, 244, 253, 90);
                d.Line(XMFLOAT3(o.Position.x - half, y, o.Position.z - half), XMFLOAT3(o.Position.x + half, y, o.Position.z - half), col);
                d.Line(XMFLOAT3(o.Position.x + half, y, o.Position.z - half), XMFLOAT3(o.Position.x + half, y, o.Position.z + half), col);
                d.Line(XMFLOAT3(o.Position.x + half, y, o.Position.z + half), XMFLOAT3(o.Position.x - half, y, o.Position.z + half), col);
                d.Line(XMFLOAT3(o.Position.x - half, y, o.Position.z + half), XMFLOAT3(o.Position.x - half, y, o.Position.z - half), col);
            } else if (o.Category == AssetCategory::Wall) {
                if (!showWalls_) continue;
                const bool grate = (o.AssetId == "Wall_Grate" || o.AssetId == "Wall_Railing");
                const ImU32 col = o.Phase == MaterialPhase::Phase ? ColPhase : (grate ? ColGrate : ColWall);
                const float floorY = static_cast<float>(o.FloorIndex) * GRID_FLOOR_HEIGHT;
                const float top = floorY + GRID_FLOOR_HEIGHT - 0.25f;
                const XMFLOAT3 base(o.Position.x, floorY, o.Position.z);
                // PlayerSystem::CollideWithWalls と同じ形状
                if (o.AssetId == "Wall_DoorArc") {
                    d.OrientedBox(base, yaw, -1.66f, -1.00f, 0.0f, top - floorY, -0.37f, 0.37f, col);
                    d.OrientedBox(base, yaw, 1.00f, 1.66f, 0.0f, top - floorY, -0.37f, 0.37f, col);
                } else if (o.AssetId == "Wall_Ruined") {
                    d.OrientedBox(base, yaw, -3.16f, -0.95f, 0.0f, top - floorY, -0.25f, 0.25f, col);
                    d.OrientedBox(base, yaw, 0.38f, 3.16f, 0.0f, top - floorY, -0.25f, 0.25f, col);
                } else {
                    const bool rot = (std::abs(std::fmod(o.Rotation.y, 180.0f)) > 45.0f);
                    const float hl = GRID_CELL_SIZE * 0.5f, ht = 0.16f;
                    d.Box(o.Position.x - (rot ? ht : hl), floorY, o.Position.z - (rot ? hl : ht),
                          o.Position.x + (rot ? ht : hl), top, o.Position.z + (rot ? hl : ht), col);
                }

                // 台座宝玉による反転壁の残存区間 (事前ベイク)
                if (showWallSpans_ && o.Phase == MaterialPhase::Phase) {
                    for (const auto& orb : ctx.Orbs) {
                        if (orb.Carrier != OrbCarrier::Pedestal || orb.IsAnimating) continue;
                        for (const auto& bake : ctx.Bakes) {
                            if (bake.PedestalInstanceId != orb.PedestalInstanceId) continue;
                            for (const auto& w : bake.Walls) {
                                if (w.WallInstanceId != o.InstanceId) continue;
                                if (w.IsFullyOpen) {
                                    d.Text(XMFLOAT3(o.Position.x, floorY + 1.6f, o.Position.z), "OPEN", ColOpen);
                                }
                                for (int32_t i = 0; i < w.SolidSpanCount; ++i) {
                                    d.Box(w.SolidMin[i].x, w.SolidMin[i].y + 0.05f, w.SolidMin[i].z,
                                          w.SolidMax[i].x, w.SolidMax[i].y - 0.05f, w.SolidMax[i].z, ColSpan, 2.0f);
                                }
                            }
                        }
                    }
                }
            }
        }

        // 反転床の穴マスク (事前ベイク: 光が届くサブセル = 穴)
        if (showHoleMasks_) {
            for (const auto& orb : ctx.Orbs) {
                if (orb.Carrier != OrbCarrier::Pedestal || orb.IsAnimating) continue;
                for (const auto& bake : ctx.Bakes) {
                    if (bake.PedestalInstanceId != orb.PedestalInstanceId) continue;
                    for (const auto& h : bake.Holes) {
                        const float y = static_cast<float>(h.FloorIndex) * GRID_FLOOR_HEIGHT + 0.03f;
                        const XMFLOAT3 center(h.CellMinXZ.x + h.CellSize * 0.5f, y, h.CellMinXZ.y + h.CellSize * 0.5f);
                        if (!d.InRange(center, 2.0f)) continue;
                        const float sub = h.CellSize / PHASE_MASK_DIM;
                        for (int iz = 0; iz < PHASE_MASK_DIM; ++iz) {
                            for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                                const XMFLOAT3 sp(h.CellMinXZ.x + (ix + 0.5f) * sub, y, h.CellMinXZ.y + (iz + 0.5f) * sub);
                                if (h.IsLitCell(ix, iz)) d.Point(sp, ColOpen, 2.0f);
                            }
                        }
                    }
                }
            }
        }

        if (showPlayerCollider_) {
            d.Cylinder(p.Position, p.Radius, p.EyeHeight + 0.1f, ColPlayer);
            const XMFLOAT3 eye(p.Position.x, p.Position.y + p.EyeHeight, p.Position.z);
            const XMFLOAT3 look(eye.x + std::sin(p.Yaw) * std::cos(p.Pitch), eye.y - std::sin(p.Pitch), eye.z + std::cos(p.Yaw) * std::cos(p.Pitch));
            d.Line(eye, look, ColPlayer, 2.0f);
        }

        if (showOptics_) {
            for (const auto& g : ctx.Occluders) {
                const XMFLOAT3 c((g.MinX + g.MaxX) * 0.5f, (g.MinY + g.MaxY) * 0.5f, (g.MinZ + g.MaxZ) * 0.5f);
                if (!d.InRange(c, 3.0f)) continue;
                d.Box(g.MinX, g.MinY, g.MinZ, g.MaxX, g.MaxY, g.MaxZ,
                      g.BlocksLight() ? IM_COL32(90, 150, 255, 200) : IM_COL32(140, 140, 140, 120), 1.0f);
            }
        }
    }

    inline void LuminousDebugTool::DrawPhaseOverlay(LuminousDebugContext& ctx) {
        if (!showPhaseSamples_) return;
        using namespace DebugToolDetail;
        auto& d = g_LuminousDebugOverlay;
        const int n = std::clamp(phaseSampleDensity_, 2, 24);

        for (const auto& o : ctx.Stage.Objects) {
            if (o.Phase != MaterialPhase::Phase) continue;
            if (!d.InRange(o.Position, 3.0f)) continue;
            const float yaw = XMConvertToRadians(o.Rotation.y);
            const float c = std::cos(yaw), s = std::sin(yaw);

            if (o.Category == AssetCategory::Wall) {
                for (int iy = 0; iy < n; ++iy) {
                    for (int ix = 0; ix < n; ++ix) {
                        const float lx = -1.0f + 2.0f * (ix + 0.5f) / n;
                        const float y = o.Position.y + 0.1f + (GRID_FLOOR_HEIGHT - 0.2f) * (iy + 0.5f) / n;
                        const XMFLOAT3 pt(o.Position.x + lx * c, y, o.Position.z - lx * s);
                        const bool lit = OpticsEngine::EvaluateCompositePhase(pt, ctx.Orbs, ctx.Occluders);
                        d.Point(pt, lit ? ColOpen : ColPhase, lit ? 2.5f : 2.0f);
                    }
                }
            } else if (o.Category == AssetCategory::Floor) {
                const float half = GRID_CELL_SIZE * 0.5f;
                for (int iz = 0; iz < n; ++iz) {
                    for (int ix = 0; ix < n; ++ix) {
                        const XMFLOAT3 pt(o.Position.x - half + GRID_CELL_SIZE * (ix + 0.5f) / n,
                                          o.Position.y + 0.05f,
                                          o.Position.z - half + GRID_CELL_SIZE * (iz + 0.5f) / n);
                        const bool lit = OpticsEngine::EvaluateCompositePhase(pt, ctx.Orbs, ctx.Occluders);
                        d.Point(pt, lit ? ColOpen : ColPhase, lit ? 2.5f : 2.0f);
                    }
                }
            }
        }
    }

    // 宝玉から dir 方向へ半径 R の線分を飛ばし、最初の遮蔽までの割合 (0..1) を返す
    inline float DebugRayFirstHit(const OrbRuntimeState& orb, const XMFLOAT3& end, const std::vector<OcclusionGeometry>& occluders) {
        float best = 1.0f;
        for (const auto& g : occluders) {
            if (!g.BlocksLight()) continue;
            const float t = SegmentAABBEntryT(orb.WorldPosition, end, g.MinX, g.MaxX, g.MinY, g.MaxY, g.MinZ, g.MaxZ);
            if (t >= 0.0f && t < best) best = t;
        }
        return best;
    }

    inline bool DebugSphereTouchesBox(const XMFLOAT3& c, float r, const OcclusionGeometry& g) {
        const float x = std::clamp(c.x, g.MinX, g.MaxX) - c.x;
        const float y = std::clamp(c.y, g.MinY, g.MaxY) - c.y;
        const float z = std::clamp(c.z, g.MinZ, g.MaxZ) - c.z;
        return x * x + y * y + z * z <= r * r;
    }

    inline void LuminousDebugTool::DrawOrbOverlay(LuminousDebugContext& ctx) {
        using namespace DebugToolDetail;
        auto& d = g_LuminousDebugOverlay;

        for (const auto& orb : ctx.Orbs) {
            const XMFLOAT3& c = orb.WorldPosition;
            const float R = orb.Radius;
            if (!d.InRange(c, R + 2.0f)) continue;

            char label[64];
            std::snprintf(label, sizeof(label), "Orb #%llu  %s  R=%.2f", static_cast<unsigned long long>(orb.OrbId), CarrierName(orb), R);
            d.Point(c, IM_COL32(255, 220, 90, 255), 4.0f);
            d.Text(XMFLOAT3(c.x, c.y + 0.25f, c.z), label, IM_COL32(255, 220, 90, 255));

            if (showOrbSpheres_) d.Sphere(c, R, IM_COL32(255, 220, 90, 150));

            if (showOrbFloorCircles_) {
                const int f0 = static_cast<int>(std::floor((c.y - R) / GRID_FLOOR_HEIGHT));
                const int f1 = static_cast<int>(std::floor((c.y + R) / GRID_FLOOR_HEIGHT));
                for (int f = f0; f <= f1; ++f) {
                    const float y = static_cast<float>(f) * GRID_FLOOR_HEIGHT;
                    const float dy = std::abs(c.y - y);
                    if (dy >= R) continue;
                    d.CircleY(XMFLOAT3(c.x, y + 0.04f, c.z), std::sqrt(R * R - dy * dy), ColOpen, 48, 2.0f);
                }
            }

            if (showOrbOccluders_) {
                for (const auto& g : ctx.Occluders) {
                    if (!g.BlocksLight() || !DebugSphereTouchesBox(c, R, g)) continue;
                    d.Box(g.MinX, g.MinY, g.MinZ, g.MaxX, g.MaxY, g.MaxZ, IM_COL32(255, 140, 40, 220), 1.5f);
                }
            }

            if (showOrbRays_) {
                const int n = std::clamp(orbRayCount_, 8, 512);
                const float golden = XM_PI * (3.0f - std::sqrt(5.0f));
                for (int i = 0; i < n; ++i) {
                    const float yv = 1.0f - 2.0f * (i + 0.5f) / n;
                    const float rr = std::sqrt((std::max)(0.0f, 1.0f - yv * yv));
                    const float a = golden * i;
                    const XMFLOAT3 end(c.x + std::cos(a) * rr * R, c.y + yv * R, c.z + std::sin(a) * rr * R);
                    const float t = DebugRayFirstHit(orb, end, ctx.Occluders);
                    const XMFLOAT3 hit(c.x + (end.x - c.x) * t, c.y + (end.y - c.y) * t, c.z + (end.z - c.z) * t);
                    d.Line(c, hit, IM_COL32(255, 230, 120, 170), 1.0f);
                    if (t < 1.0f) {
                        d.Line(hit, end, IM_COL32(200, 60, 60, 90), 1.0f);
                        d.Point(hit, IM_COL32(255, 80, 60, 230), 2.0f);
                    }
                }
            }

            if (showOrbSamples_) {
                const float step = std::clamp(orbSampleStep_, 0.3f, 2.0f);
                int budget = 5000;
                for (float x = -R; x <= R && budget > 0; x += step) {
                    for (float y = -R; y <= R && budget > 0; y += step) {
                        for (float z = -R; z <= R && budget > 0; z += step) {
                            if (x * x + y * y + z * z > R * R) continue;
                            const XMFLOAT3 pt(c.x + x, c.y + y, c.z + z);
                            --budget;
                            const bool lit = OpticsEngine::EvaluateLight(pt, orb, ctx.Occluders);
                            d.Point(pt, lit ? IM_COL32(120, 230, 255, 200) : IM_COL32(140, 40, 60, 140), lit ? 2.0f : 1.5f);
                        }
                    }
                }
            }
        }
    }

    // ------------------------------------------------------------------------
    // Collision / Phase / Orbs タブ
    // ------------------------------------------------------------------------
    inline void LuminousDebugTool::TabCollision(LuminousDebugContext& ctx) {
        ImGui::SliderFloat("Overlay distance", &overlayDistance_, 5.0f, 120.0f, "%.0f m");
        ImGui::Checkbox("Current floor only", &overlayCurrentFloorOnly_);
        DebugToolDetail::ToolTip("Wireframes are drawn on top of the scene without depth test.");

        ImGui::SeparatorText("Player colliders (PlayerSystem)");
        ImGui::Checkbox("Walls", &showWalls_); ImGui::SameLine();
        ImGui::Checkbox("Stairs", &showStairs_); ImGui::SameLine();
        ImGui::Checkbox("Pedestals", &showPedestals_);
        ImGui::Checkbox("Chest / specials", &showChest_); ImGui::SameLine();
        ImGui::Checkbox("Props", &showProps_); ImGui::SameLine();
        ImGui::Checkbox("Player", &showPlayerCollider_);
        ImGui::Checkbox("Floor tiles", &showFloorTiles_);
        ImGui::TextColored(ImVec4(1.0f, 0.43f, 0.35f, 1.0f), "normal wall"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.9f, 0.35f, 1.0f, 1.0f), "phase"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(0.63f, 0.63f, 0.67f, 1.0f), "grate"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.86f, 0.35f, 1.0f), "stairs"); ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.63f, 0.24f, 1.0f), "pedestal/chest");

        ImGui::SeparatorText("Phase material colliders (baked)");
        ImGui::Checkbox("Phase wall solid spans", &showWallSpans_);
        ImGui::Checkbox("Phase floor hole masks", &showHoleMasks_);

        ImGui::SeparatorText("Light occluders (OpticsEngine)");
        ImGui::Checkbox("Optics AABBs", &showOptics_);
        DebugToolDetail::ToolTip("Blue: blocks orb light. Grey: passes light (phase / grate). Arches, windows and stairs use per-asset shapes.");

        ImGui::SeparatorText("Probe at player");
        const auto& p = ctx.Player;
        bool onStairs = false;
        const float ground = PlayerSystem::EvaluateGroundHeight(p.Position, p.Position.y, ctx.Stage.Objects, ctx.Orbs, ctx.Occluders, ctx.Bakes, onStairs);
        const bool tile = PlayerSystem::HasFloorTileUnder(p.Position, ctx.Stage.Objects, ctx.Orbs, ctx.Occluders, ctx.Bakes);
        ImGui::Text("Ground height %s%.3f  on stairs %s", ground < -50.0f ? "(none) " : "", ground, onStairs ? "yes" : "no");
        ImGui::Text("Walkable tile under player: %s", tile ? "yes" : "no (hole / void)");
    }

    inline void LuminousDebugTool::TabPhase(LuminousDebugContext& ctx) {
        ImGui::Checkbox("Show live phase samples", &showPhaseSamples_);
        ImGui::SliderInt("Samples per axis", &phaseSampleDensity_, 2, 24);
        ImGui::Checkbox("Disable GPU orb occlusion atlas", &disableOrbOcclusion_);
        DebugToolDetail::ToolTip("Compare the visual phase state with and without orb shadows. Cyan = lit (open / hole), magenta = dark (solid).");

        ImGui::SeparatorText("Phase objects");
        if (ImGui::BeginTable("phase", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp, ImVec2(0.0f, 360.0f))) {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("ID");
            ImGui::TableSetupColumn("Asset");
            ImGui::TableSetupColumn("Floor");
            ImGui::TableSetupColumn("Lit %");
            ImGui::TableSetupColumn("Bake");
            ImGui::TableHeadersRow();

            auto& objects = ctx.Stage.Objects;
            for (int i = 0; i < static_cast<int>(objects.size()); ++i) {
                const auto& o = objects[static_cast<size_t>(i)];
                if (o.Phase != MaterialPhase::Phase) continue;

                // 6x6 サンプルの点灯率 (光学的な真値)
                int lit = 0;
                constexpr int n = 6;
                const float yaw = XMConvertToRadians(o.Rotation.y);
                const float c = std::cos(yaw), s = std::sin(yaw);
                for (int a = 0; a < n; ++a) {
                    for (int b = 0; b < n; ++b) {
                        XMFLOAT3 pt;
                        if (o.Category == AssetCategory::Wall) {
                            const float lx = -1.0f + 2.0f * (a + 0.5f) / n;
                            pt = XMFLOAT3(o.Position.x + lx * c, o.Position.y + 0.1f + 2.8f * (b + 0.5f) / n, o.Position.z - lx * s);
                        } else {
                            const float half = GRID_CELL_SIZE * 0.5f;
                            pt = XMFLOAT3(o.Position.x - half + GRID_CELL_SIZE * (a + 0.5f) / n, o.Position.y + 0.05f,
                                          o.Position.z - half + GRID_CELL_SIZE * (b + 0.5f) / n);
                        }
                        if (OpticsEngine::EvaluateCompositePhase(pt, ctx.Orbs, ctx.Occluders)) ++lit;
                    }
                }

                // 事前ベイク (台座に据わっている宝玉ぶん)
                std::string bakeInfo;
                for (const auto& orb : ctx.Orbs) {
                    if (orb.Carrier != OrbCarrier::Pedestal || orb.IsAnimating) continue;
                    for (const auto& bake : ctx.Bakes) {
                        if (bake.PedestalInstanceId != orb.PedestalInstanceId) continue;
                        for (const auto& w : bake.Walls) {
                            if (w.WallInstanceId != o.InstanceId) continue;
                            bakeInfo += w.IsFullyOpen ? "open " : (w.IsFullySolid ? "solid " : ("spans" + std::to_string(w.SolidSpanCount) + " "));
                        }
                        for (const auto& h : bake.Holes) {
                            if (h.FloorInstanceId != o.InstanceId) continue;
                            int bits = 0;
                            for (int iz = 0; iz < PHASE_MASK_DIM; ++iz)
                                for (int ix = 0; ix < PHASE_MASK_DIM; ++ix)
                                    if (h.IsLitCell(ix, iz)) ++bits;
                            bakeInfo += "hole" + std::to_string(bits) + "/256 ";
                        }
                    }
                }

                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                char idLabel[32];
                std::snprintf(idLabel, sizeof(idLabel), "#%llu##ph%d", static_cast<unsigned long long>(o.InstanceId), i);
                if (ImGui::Selectable(idLabel, selectedObjectIndex_ == i, ImGuiSelectableFlags_SpanAllColumns)) selectedObjectIndex_ = i;
                ImGui::TableNextColumn(); ImGui::TextUnformatted(o.AssetId.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%dF", o.FloorIndex + 1);
                ImGui::TableNextColumn(); ImGui::Text("%3d%%", lit * 100 / (n * n));
                ImGui::TableNextColumn(); ImGui::TextUnformatted(bakeInfo.empty() ? "-" : bakeInfo.c_str());
            }
            ImGui::EndTable();
        }
        ImGui::TextDisabled("Selected rows are highlighted in the viewport.");
    }

    inline void LuminousDebugTool::TabOrbs(LuminousDebugContext& ctx) {
        ImGui::SeparatorText("Influence area overlay");
        ImGui::Checkbox("Sphere", &showOrbSpheres_); ImGui::SameLine();
        ImGui::Checkbox("Floor hole circles", &showOrbFloorCircles_);
        ImGui::Checkbox("Occluders in range", &showOrbOccluders_);
        ImGui::Checkbox("Occlusion ray fan", &showOrbRays_); ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderInt("rays", &orbRayCount_, 8, 512);
        ImGui::Checkbox("Lit sample volume", &showOrbSamples_); ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        ImGui::SliderFloat("step", &orbSampleStep_, 0.3f, 2.0f, "%.2f m");
        DebugToolDetail::ToolTip("Yellow rays stop at the first light occluder (red dot). Cyan points are inside the lit volume.");

        ImGui::SeparatorText("Parameters");
        if (ImGui::SliderFloat("Influence radius (all orbs)", &g_LuminousConfig.OrbInfluenceRadius, 0.5f, 15.0f, "%.2f m")) {
            for (auto& orb : ctx.Orbs) orb.Radius = g_LuminousConfig.OrbInfluenceRadius;
        }
        ImGui::TextDisabled("Pedestal collider bakes use the radius at stage load.");

        ImGui::SeparatorText("Orbs");
        for (auto& orb : ctx.Orbs) {
            char header[64];
            std::snprintf(header, sizeof(header), "Orb #%llu  (%s)", static_cast<unsigned long long>(orb.OrbId), DebugToolDetail::CarrierName(orb));
            if (!ImGui::CollapsingHeader(header, ImGuiTreeNodeFlags_DefaultOpen)) continue;
            ImGui::PushID(static_cast<int>(orb.OrbId));
            ImGui::Text("Position (%.2f, %.2f, %.2f)", orb.WorldPosition.x, orb.WorldPosition.y, orb.WorldPosition.z);
            ImGui::Text("Pedestal #%llu", static_cast<unsigned long long>(orb.PedestalInstanceId));
            ImGui::DragFloat("Radius", &orb.Radius, 0.05f, 0.1f, 20.0f, "%.2f m");

            int inRange = 0, blocking = 0;
            for (const auto& g : ctx.Occluders) {
                if (!DebugSphereTouchesBox(orb.WorldPosition, orb.Radius, g)) continue;
                ++inRange;
                if (g.BlocksLight()) ++blocking;
            }
            ImGui::Text("Occluders touching sphere: %d (blocking %d)", inRange, blocking);

            // 水平 64 方向の遮蔽率
            int blockedRays = 0;
            for (int i = 0; i < 64; ++i) {
                const float a = XM_2PI * i / 64.0f;
                const XMFLOAT3 end(orb.WorldPosition.x + std::cos(a) * orb.Radius, orb.WorldPosition.y, orb.WorldPosition.z + std::sin(a) * orb.Radius);
                if (DebugRayFirstHit(orb, end, ctx.Occluders) < 1.0f) ++blockedRays;
            }
            ImGui::Text("Horizontal rays blocked: %d / 64", blockedRays);

            if (ImGui::Button("Spectate orb")) {
                SetSpectator(ctx, true);
                spectatorPos_ = XMFLOAT3(orb.WorldPosition.x - 3.0f, orb.WorldPosition.y + 2.5f, orb.WorldPosition.z - 3.0f);
                spectatorYaw_ = XMConvertToRadians(45.0f);
                spectatorPitch_ = XMConvertToRadians(30.0f);
            }
            ImGui::PopID();
        }
    }
#else
    inline void LuminousDebugTool::DrawUI(LuminousDebugContext&, float) {}
#endif
}
