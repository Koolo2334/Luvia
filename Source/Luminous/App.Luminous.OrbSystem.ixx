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
#include <cmath>
#include <algorithm>
#include <string>
#include <cstdint>

export module App.Luminous.OrbSystem;

import Engine.Input;
import App.Luminous.Types;
import App.Luminous.Optics;
import App.Luminous.PlayerSystem;
import App.Luminous.InputConfig;
import App.Luminous.StageData;

export namespace App::Luminous {

    using namespace DirectX;
    using namespace Engine::Input;

    // ====================================================================
    // ゴール誘導パーティクル
    // --------------------------------------------------------------------
    // 手持ちの宝玉から一定間隔で粒子を放出し、各粒子は「放出した瞬間の宝玉位置」
    // からゴールまでの軌道 (2 次ベジェ + 螺旋) を放出時点で確定させて進む。
    // プレイヤーが動いても既に出た粒子の軌道は変わらないため、
    // 移動すると過去の軌道が残像のように残る。
    // ====================================================================
    struct GuidanceParticleSprite {
        XMFLOAT3 Position;
        float Radius;
        XMFLOAT4 Color;   // rgb: HDR 発光色, a: 不透明度
    };

    class GoalGuidanceStream {
    public:
        static constexpr float EMIT_RATE = 28.0f;      // 個/秒
        static constexpr float TRAVEL_SPEED = 1.8f;    // m/秒 (軌道長からの寿命算出に使う)
        static constexpr float MIN_LIFE = 1.2f;
        static constexpr float MAX_LIFE = 7.5f;
        static constexpr size_t MAX_PARTICLES = 512;

        void Reset() {
            particles_.clear();
            emitAccumulator_ = 0.0f;
        }

        size_t Count() const { return particles_.size(); }

        // emitting が false の間は放出を止める (既存の粒子は寿命まで飛び続ける)
        void Update(float dt, bool emitting, const XMFLOAT3& orbPos, const XMFLOAT3& goalPos) {
            if (dt <= 0.0f) return;

            for (auto& p : particles_) p.Age += dt;
            particles_.erase(
                std::remove_if(particles_.begin(), particles_.end(), [](const Particle& p) { return p.Age >= p.Life; }),
                particles_.end());

            if (!emitting) {
                emitAccumulator_ = 0.0f;
                return;
            }

            emitAccumulator_ += dt * EMIT_RATE;
            while (emitAccumulator_ >= 1.0f) {
                emitAccumulator_ -= 1.0f;
                if (particles_.size() >= MAX_PARTICLES) {
                    emitAccumulator_ = 0.0f;
                    break;
                }
                // 同一フレーム内で複数出す場合も位相がずれるよう、経過分だけ進めておく
                Spawn(orbPos, goalPos, emitAccumulator_ / EMIT_RATE);
            }
        }

        void Collect(std::vector<GuidanceParticleSprite>& out) const {
            out.reserve(out.size() + particles_.size());
            for (const auto& p : particles_) {
                out.push_back(Evaluate(p));
            }
        }

    private:
        struct Particle {
            XMFLOAT3 P0, P1, P2;
            float Age = 0.0f;
            float Life = 1.0f;
            float Seed = 0.0f;
            int Strand = 0;
        };

        float NextRandom() {
            rngState_ = rngState_ * 1664525u + 1013904223u;
            return static_cast<float>((rngState_ >> 8) & 0xFFFFFF) / static_cast<float>(0x1000000);
        }

        void Spawn(const XMFLOAT3& orbPos, const XMFLOAT3& goalPos, float initialAge) {
            Particle p;
            p.P0 = orbPos;
            p.P2 = goalPos;

            const float dx = goalPos.x - orbPos.x;
            const float dy = goalPos.y - orbPos.y;
            const float dz = goalPos.z - orbPos.z;
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            const float lift = std::clamp(dist * 0.3f, 0.6f, 2.2f);
            p.P1 = XMFLOAT3((orbPos.x + goalPos.x) * 0.5f,
                            (orbPos.y + goalPos.y) * 0.5f + lift,
                            (orbPos.z + goalPos.z) * 0.5f);

            // 制御点を通る折れ線長で軌道長を近似する
            auto len = [](const XMFLOAT3& a, const XMFLOAT3& b) {
                const float x = b.x - a.x, y = b.y - a.y, z = b.z - a.z;
                return std::sqrt(x * x + y * y + z * z);
            };
            const float pathLen = 0.5f * (dist + len(p.P0, p.P1) + len(p.P1, p.P2));
            p.Life = std::clamp(pathLen / TRAVEL_SPEED, MIN_LIFE, MAX_LIFE);
            p.Age = (std::min)(initialAge, p.Life * 0.5f);
            p.Seed = NextRandom();
            p.Strand = static_cast<int>(strandCounter_++ % 3u);
            particles_.push_back(p);
        }

        static GuidanceParticleSprite Evaluate(const Particle& p) {
            const float t = std::clamp(p.Age / p.Life, 0.0f, 1.0f);
            const float omt = 1.0f - t;
            const float w0 = omt * omt;
            const float w1 = 2.0f * omt * t;
            const float w2 = t * t;

            XMFLOAT3 b(
                w0 * p.P0.x + w1 * p.P1.x + w2 * p.P2.x,
                w0 * p.P0.y + w1 * p.P1.y + w2 * p.P2.y,
                w0 * p.P0.z + w1 * p.P1.z + w2 * p.P2.z);

            // 接線 T(t) = 2(1-t)(P1 - P0) + 2t(P2 - P1)
            XMFLOAT3 tangent(
                2.0f * omt * (p.P1.x - p.P0.x) + 2.0f * t * (p.P2.x - p.P1.x),
                2.0f * omt * (p.P1.y - p.P0.y) + 2.0f * t * (p.P2.y - p.P1.y),
                2.0f * omt * (p.P1.z - p.P0.z) + 2.0f * t * (p.P2.z - p.P1.z));
            float tLen = std::sqrt(tangent.x * tangent.x + tangent.y * tangent.y + tangent.z * tangent.z);
            if (tLen > 1e-4f) {
                tangent.x /= tLen; tangent.y /= tLen; tangent.z /= tLen;
            } else {
                tangent = XMFLOAT3(0.0f, 1.0f, 0.0f);
            }

            const XMFLOAT3 up = (std::abs(tangent.y) < 0.92f) ? XMFLOAT3(0.0f, 1.0f, 0.0f) : XMFLOAT3(1.0f, 0.0f, 0.0f);
            XMFLOAT3 n(
                up.y * tangent.z - up.z * tangent.y,
                up.z * tangent.x - up.x * tangent.z,
                up.x * tangent.y - up.y * tangent.x);
            const float nLen = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
            if (nLen > 1e-4f) { n.x /= nLen; n.y /= nLen; n.z /= nLen; }
            const XMFLOAT3 bn(
                tangent.y * n.z - tangent.z * n.y,
                tangent.z * n.x - tangent.x * n.z,
                tangent.x * n.y - tangent.y * n.x);

            // 3 本の編み込み螺旋。位相は粒子自身の経過時間と乱数だけで決まる
            const float envelope = std::sin(t * XM_PI);
            const float swirl = t * 4.0f * XM_PI + p.Age * 1.15f + p.Seed * XM_2PI
                              + static_cast<float>(p.Strand) * (XM_2PI / 3.0f);
            const float swirlR = envelope * (0.22f + 0.07f * std::sin(t * 6.0f * XM_PI + p.Seed * 9.0f));
            const float wisp1 = std::sin(t * 8.0f * XM_PI + p.Age * 1.8f + p.Seed * 5.0f) * 0.045f * envelope;
            const float wisp2 = std::cos(t * 6.0f * XM_PI - p.Age * 1.4f) * 0.045f * envelope;
            const float ca = std::cos(swirl);
            const float sa = std::sin(swirl);

            GuidanceParticleSprite s;
            s.Position = XMFLOAT3(
                b.x + (n.x * ca + bn.x * sa) * swirlR + n.x * wisp1 + bn.x * wisp2,
                b.y + (n.y * ca + bn.y * sa) * swirlR + n.y * wisp1 + bn.y * wisp2,
                b.z + (n.z * ca + bn.z * sa) * swirlR + n.z * wisp1 + bn.z * wisp2);

            auto smooth = [](float e0, float e1, float x) {
                const float k = std::clamp((x - e0) / (e1 - e0), 0.0f, 1.0f);
                return k * k * (3.0f - 2.0f * k);
            };
            const float fade = smooth(0.0f, 0.06f, t) * (1.0f - smooth(0.82f, 1.0f, t));
            const float twinkle = 0.75f + 0.25f * std::sin(p.Age * 9.0f + p.Seed * 40.0f);
            const float pulse = 0.85f + 0.30f * std::sin(p.Age * 5.0f + p.Seed * 20.0f);

            s.Radius = (0.020f + 0.018f * envelope) * pulse;
            if (p.Strand == 2) {
                s.Color = XMFLOAT4(0.55f, 1.60f, 2.40f, fade * twinkle * 0.85f);   // 氷青
            } else {
                s.Color = XMFLOAT4(2.40f, 1.85f, 0.70f, fade * twinkle * 0.90f);   // 金
            }
            return s;
        }

        std::vector<Particle> particles_;
        float emitAccumulator_ = 0.0f;
        uint32_t rngState_ = 0x9E3779B9u;
        uint32_t strandCounter_ = 0;
    };

    // ====================================================================
    // 宝玉・台座・ゴール誘導システム (仕様書 第4条・第5条 準拠)
    // ====================================================================
    class OrbSystem {
    public:
        // ステージ定義から初期宝玉状態を生成
        static void InitializeOrbsFromStage(
            const LuminousStage& stage,
            std::vector<OrbRuntimeState>& outOrbs,
            LuminousPlayerComponent& player)
        {
            outOrbs.clear();
            uint64_t orbIdCounter = 1;

            for (const auto& obj : stage.Objects) {
                if (obj.Category == AssetCategory::Pedestal && obj.HasInitialOrb) {
                    OrbRuntimeState orb;
                    orb.OrbId = orbIdCounter++;
                    orb.Carrier = OrbCarrier::Pedestal;
                    orb.PedestalInstanceId = obj.InstanceId;
                    orb.WorldPosition = XMFLOAT3(
                        obj.Position.x,
                        obj.Position.y + PEDESTAL_SOCKET_HEIGHT,
                        obj.Position.z
                    );
                    orb.Radius = DEFAULT_ORB_RADIUS;
                    outOrbs.push_back(orb);
                }
            }

            if (outOrbs.empty()) {
                OrbRuntimeState orb;
                orb.OrbId = orbIdCounter++;
                orb.Carrier = OrbCarrier::Player;
                orb.PedestalInstanceId = 0;
                orb.WorldPosition = player.Position;
                orb.Radius = DEFAULT_ORB_RADIUS;
                outOrbs.push_back(orb);

                player.IsHoldingOrb = true;
                player.HeldOrbId = orb.OrbId;
                player.OrbPickupCount = 1;
            }
        }

        // 宝玉位置の毎フレーム更新 (手持ちスウェイ追従 or 台座浮遊 or 脱着放物線アニメーション)
        // ================================================================
        // 回帰テスト: 手持ち宝玉が壁にめり込まないこと
        //
        // 宝玉は光源でもあるため、壁にめり込むと光源が壁の向こう側へ出て
        // 本来ありえない影が生まれ、その影を足場として悪用できてしまう。
        // ================================================================
        static bool RunHeldOrbWallClipTest(std::string& outReport)
        {
            // セル (1,1) の西辺に通常壁。プレイヤーはその東側に立ち、西を向く。
            std::vector<PlacedObject> objects;
            uint64_t nextId = 1;
            for (int x = 0; x < 3; ++x) {
                for (int z = 0; z < 3; ++z) {
                    PlacedObject f;
                    f.InstanceId = nextId++;
                    f.AssetId = "Floor_1x1";
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    objects.push_back(f);
                }
            }
            PlacedObject wall;
            wall.InstanceId = nextId++;
            wall.AssetId = "Wall_Brick";
            wall.Category = AssetCategory::Wall;
            wall.Placement = PlacementType::EdgeSnap;
            wall.FloorIndex = 0;
            wall.Cell = GridCoord{ 1, 1, 0 };
            wall.Edge = CellEdge::West;
            float wYaw = 0.0f;
            wall.Position = EdgeToWorld(1, 1, 0, CellEdge::West, wYaw);
            wall.Rotation.y = wYaw;
            objects.push_back(wall);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);

            // 壁からプレイヤー半径ぶんだけ離れて立ち、真西を向く
            const XMFLOAT3 eye(wall.Position.x + PLAYER_RADIUS + 0.02f, 1.55f, wall.Position.z);
            const float yaw = XMConvertToRadians(270.0f);   // -X
            const float pitch = 0.0f;

            LuminousPlayerComponent player;
            player.IsHoldingOrb = true;
            player.HeldOrbId = 1;

            OrbRuntimeState orb;
            orb.OrbId = 1;
            orb.Carrier = OrbCarrier::Player;
            orb.Radius = DEFAULT_ORB_RADIUS;
            orb.WorldPosition = eye;
            std::vector<OrbRuntimeState> orbs{ orb };

            // 実際の更新経路で位置を確定させる
            UpdateOrbs(orbs, player, eye, yaw, pitch, 0.0f, 1.0f / 60.0f, occluders);
            const XMFLOAT3 constrained = orbs[0].WorldPosition;

            // 拘束なしの素の手元位置 (比較用)
            std::vector<OrbRuntimeState> raw{ orb };
            UpdateOrbs(raw, player, eye, yaw, pitch, 0.0f, 1.0f / 60.0f, {});
            const XMFLOAT3 unconstrained = raw[0].WorldPosition;

            // 壁 AABB を宝玉半径ぶん膨らませたものに入っていないこと
            auto insideExpandedWall = [&](const XMFLOAT3& p) {
                for (const auto& g : occluders) {
                    if (!g.BlocksLight()) continue;
                    const float r = ORB_HOLD_RADIUS - 0.03f;   // 停止マージンぶん緩める
                    if (p.x > g.MinX - r && p.x < g.MaxX + r &&
                        p.y > g.MinY - r && p.y < g.MaxY + r &&
                        p.z > g.MinZ - r && p.z < g.MaxZ + r) {
                        return true;
                    }
                }
                return false;
            };

            const bool rawInside = insideExpandedWall(unconstrained);
            const bool fixedInside = insideExpandedWall(constrained);

            outReport = "rawX=" + std::to_string(unconstrained.x)
                + " constrainedX=" + std::to_string(constrained.x)
                + " wallX=" + std::to_string(wall.Position.x)
                + " rawInside=" + std::string(rawInside ? "yes" : "no")
                + " fixedInside=" + std::string(fixedInside ? "yes" : "no");

            if (!rawInside) return false;    // 前提: 拘束が無ければめり込む配置であること
            if (fixedInside) return false;   // 拘束後はめり込まないこと
            return true;
        }

        // ================================================================
        // 回帰テスト: 通り抜けられる開口部 (アーチ門) では手持ち宝玉を押し戻さないこと
        //
        // 以前は壁をセル幅の板として扱っていたため、アーチの中に立って前を向くだけで
        // 宝玉が目の前へ引き寄せられ、画面を覆ったりガタついたりしていた。
        // ================================================================
        static bool RunHeldOrbArchwayTest(std::string& outReport)
        {
            std::vector<PlacedObject> objects;
            PlacedObject arch;
            arch.InstanceId = 1;
            arch.AssetId = "Wall_DoorArc";
            arch.Category = AssetCategory::Wall;
            arch.Placement = PlacementType::EdgeSnap;
            arch.FloorIndex = 0;
            arch.Cell = GridCoord{ 1, 1, 0 };
            arch.Edge = CellEdge::West;
            float yawDeg = 0.0f;
            arch.Position = EdgeToWorld(1, 1, 0, CellEdge::West, yawDeg);
            arch.Rotation.y = yawDeg;
            objects.push_back(arch);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects, false);

            // アーチの真ん中に立ち、開口部の向こう (西) を向く
            const XMFLOAT3 eye(arch.Position.x + 0.05f, 1.55f, arch.Position.z);
            const float yaw = XMConvertToRadians(270.0f);

            LuminousPlayerComponent player;
            player.IsHoldingOrb = true;
            player.HeldOrbId = 1;

            OrbRuntimeState orb;
            orb.OrbId = 1;
            orb.Carrier = OrbCarrier::Player;
            orb.Radius = DEFAULT_ORB_RADIUS;
            orb.WorldPosition = eye;

            std::vector<OrbRuntimeState> constrained{ orb };
            UpdateOrbs(constrained, player, eye, yaw, 0.0f, 0.0f, 1.0f / 60.0f, occluders);
            std::vector<OrbRuntimeState> raw{ orb };
            UpdateOrbs(raw, player, eye, yaw, 0.0f, 0.0f, 1.0f / 60.0f, {});

            const float dx = constrained[0].WorldPosition.x - raw[0].WorldPosition.x;
            const float dy = constrained[0].WorldPosition.y - raw[0].WorldPosition.y;
            const float dz = constrained[0].WorldPosition.z - raw[0].WorldPosition.z;
            const float shift = std::sqrt(dx * dx + dy * dy + dz * dz);

            outReport = "occluders=" + std::to_string(occluders.size()) + " shift=" + std::to_string(shift);
            return shift < 1e-3f;
        }

        // 手持ち宝玉が壁にめり込まないようにする。
        //
        // 宝玉は光源でもあるため、壁にめり込むと光源が壁の向こう側へ出て
        // 本来ありえない遮蔽 (影) が生まれ、その影を足場や壁として悪用できてしまう。
        //
        // 単純に目線へ引き寄せると、壁に密着したときに宝玉が目前まで来て
        // 画面を覆ってしまう。そこでまず「前方へ突き出す量」だけを段階的に縮め、
        // 手元の左右・下方向の保持位置はできるだけ保つ。
        // それでも塞がれている場合 (真横が壁など) にかぎり、
        // 目線からの線分を打ち切る。
        //
        // 遮蔽体はアセットごとの形状 (OpticsEngine::GetAssetLocalOccluders) なので、
        // アーチ門・窓・崩れた壁の開口部では押し戻さない。
        static bool IsHeldOrbPathClear(
            const XMFLOAT3& eyePos,
            const XMFLOAT3& candidate,
            const std::vector<OcclusionGeometry>& occluders)
        {
            const float r = ORB_HOLD_RADIUS;
            for (const auto& g : occluders) {
                // 光を遮る通常マテリアルのみが対象。
                // 反転壁は宝玉所持中は素通しであり、格子・柵は元から光を通すため除外。
                if (!g.BlocksLight()) continue;
                const float t = SegmentAABBEntryT(
                    eyePos, candidate,
                    g.MinX - r, g.MaxX + r,
                    g.MinY - r, g.MaxY + r,
                    g.MinZ - r, g.MaxZ + r);
                if (t >= 0.0f) return false;
            }
            return true;
        }

        static XMFLOAT3 ConstrainHeldOrbPosition(
            const XMFLOAT3& eyePos,
            const XMFLOAT3& forward,
            const XMFLOAT3& right,
            const XMFLOAT3& up,
            float fwdAmt, float rightAmt, float upAmt,
            const std::vector<OcclusionGeometry>& occluders)
        {
            auto compose = [&](float f) {
                return XMFLOAT3(
                    eyePos.x + forward.x * f + right.x * rightAmt + up.x * upAmt,
                    eyePos.y + forward.y * f + right.y * rightAmt + up.y * upAmt,
                    eyePos.z + forward.z * f + right.z * rightAmt + up.z * upAmt);
            };

            const XMFLOAT3 desired = compose(fwdAmt);
            if (occluders.empty()) return desired;

            // 1. 前方成分だけを縮めて逃がす
            static constexpr float kScales[] = { 1.0f, 0.75f, 0.5f, 0.25f, 0.0f };
            for (float scale : kScales) {
                const XMFLOAT3 cand = compose(fwdAmt * scale);
                if (IsHeldOrbPathClear(eyePos, cand, occluders)) return cand;
            }

            // 2. 真横・真下も塞がれている場合は目線からの線分を打ち切る
            const float dx = desired.x - eyePos.x;
            const float dy = desired.y - eyePos.y;
            const float dz = desired.z - eyePos.z;
            const float len = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (len < 1e-4f) return desired;

            const float r = ORB_HOLD_RADIUS;
            float bestT = 1.0f;
            for (const auto& g : occluders) {
                if (!g.BlocksLight()) continue;
                const float t = SegmentAABBEntryT(
                    eyePos, desired,
                    g.MinX - r, g.MaxX + r,
                    g.MinY - r, g.MaxY + r,
                    g.MinZ - r, g.MaxZ + r);
                if (t >= 0.0f && t < bestT) bestT = t;
            }
            if (bestT >= 1.0f) return desired;

            const float safeLen = (std::max)(0.0f, bestT * len - 0.02f);
            const float k = safeLen / len;
            return XMFLOAT3(eyePos.x + dx * k, eyePos.y + dy * k, eyePos.z + dz * k);
        }

        static void UpdateOrbs(
            std::vector<OrbRuntimeState>& orbs,
            LuminousPlayerComponent& player,
            const XMFLOAT3& eyePos,
            float playerYaw,
            float playerPitch,
            float totalTime,
            float deltaTime,
            const std::vector<OcclusionGeometry>& occluders = {})
        {
            float sinYaw = std::sin(playerYaw);
            float cosYaw = std::cos(playerYaw);
            float sinPitch = std::sin(playerPitch);
            float cosPitch = std::cos(playerPitch);

            // カメラの視線基準基底ベクトル
            XMFLOAT3 forward(sinYaw * cosPitch, -sinPitch, cosYaw * cosPitch);
            XMFLOAT3 right(cosYaw, 0.0f, -sinYaw);
            XMFLOAT3 up(sinYaw * sinPitch, cosPitch, cosYaw * sinPitch);

            // 今フレームの手元位置 (スウェイなし)。脱着アニメーションの手元側の端点に使う。
            // 取った瞬間の位置で固定すると、アニメーション中にプレイヤーが動いたとき
            // 終了時に宝玉が手元へ瞬間移動したり、置くときに宝玉が置き去りになったりする。
            const float liveBob = std::sin(totalTime * 2.5f) * 0.015f;
            const XMFLOAT3 liveHand = ConstrainHeldOrbPosition(
                eyePos, forward, right, up, 0.58f, 0.32f, -0.22f + liveBob, occluders);

            for (auto& orb : orbs) {
                // 1. 台座脱着放物線アニメーション (仕様書 4.2 / Phase 4)
                if (orb.IsAnimating) {
                    orb.AnimTime += deltaTime;
                    float tau = (orb.AnimDuration > 0.0f) ? (std::min)(1.0f, orb.AnimTime / orb.AnimDuration) : 1.0f;

                    if (orb.TargetCarrier == OrbCarrier::Player) {
                        orb.AnimEndPos = liveHand;     // 台座 -> 手元: 到着点が手に追従
                    } else {
                        orb.AnimStartPos = liveHand;   // 手元 -> 台座: 出発点が手に追従
                    }

                    // 放物線弧線補間: P(tau) = (1-tau)*P_start + tau*P_end + (0, 0.45 * sin(pi * tau), 0)
                    float arcY = 0.45f * std::sin(XM_PI * tau);
                    orb.WorldPosition.x = (1.0f - tau) * orb.AnimStartPos.x + tau * orb.AnimEndPos.x;
                    orb.WorldPosition.y = (1.0f - tau) * orb.AnimStartPos.y + tau * orb.AnimEndPos.y + arcY;
                    orb.WorldPosition.z = (1.0f - tau) * orb.AnimStartPos.z + tau * orb.AnimEndPos.z;

                    if (tau >= 1.0f) {
                        orb.IsAnimating = false;
                        orb.Carrier = orb.TargetCarrier;
                        orb.PedestalInstanceId = orb.TargetPedestalId;
                        orb.WorldPosition = orb.AnimEndPos;
                        // 手持ちになった瞬間にスウェイが跳ねないよう、回転の基準を取り直す
                        orb.HasPrevRot = false;
                        orb.SwayOffset = XMFLOAT2(0.0f, 0.0f);
                    }
                    continue;
                }

                // 2. 手持ちViewmodel 状態 (視線遅延スウェイ ＆ 呼吸パルス)
                if (orb.Carrier == OrbCarrier::Player) {
                    // カメラ回転の角速度検出
                    if (!orb.HasPrevRot) {
                        orb.LastYaw = playerYaw;
                        orb.LastPitch = playerPitch;
                        orb.HasPrevRot = true;
                    }

                    float deltaYaw = playerYaw - orb.LastYaw;
                    while (deltaYaw > XM_PI) deltaYaw -= XM_2PI;
                    while (deltaYaw < -XM_PI) deltaYaw += XM_2PI;
                    float deltaPitch = playerPitch - orb.LastPitch;

                    orb.LastYaw = playerYaw;
                    orb.LastPitch = playerPitch;

                    // スウェイ変位の加算 (カメラの旋回と逆方向へわずかに偏向)
                    orb.SwayOffset.x -= deltaYaw * 0.12f;
                    orb.SwayOffset.y += deltaPitch * 0.12f;

                    // 最大スウェイ幅の制限
                    orb.SwayOffset.x = (std::max)(-0.08f, (std::min)(0.08f, orb.SwayOffset.x));
                    orb.SwayOffset.y = (std::max)(-0.06f, (std::min)(0.06f, orb.SwayOffset.y));

                    // 指数バネ減衰 (復元力)
                    float damping = std::exp(-14.0f * deltaTime);
                    orb.SwayOffset.x *= damping;
                    orb.SwayOffset.y *= damping;

                    // 有機的な呼吸パルス浮動 (上下微小振動)
                    float bobbing = std::sin(totalTime * 2.5f) * 0.015f;

                    float finalFwd   = 0.58f;
                    float finalRight = 0.32f + orb.SwayOffset.x;
                    float finalUp    = -0.22f + orb.SwayOffset.y + bobbing;

                    // 壁へのめり込みを防止 (描画・光源・当たり判定すべてこの位置を使う)
                    orb.WorldPosition = ConstrainHeldOrbPosition(
                        eyePos, forward, right, up, finalFwd, finalRight, finalUp, occluders);
                }
            }
        }

        // 調べるボタンでの脱着インタラクト処理 (0.25秒放物線アニメーション開始)
        static void HandleInteraction(
            LuminousPlayerComponent& player,
            std::vector<OrbRuntimeState>& orbs,
            const std::vector<PlacedObject>& objects,
            const XMFLOAT3& eyePos,
            float playerYaw,
            float playerPitch)
        {
            if (!LuminousInput::InteractPressed()) {
                return;
            }

            if (!player.CanInteract) {
                return;
            }

            // 1. ゴール宝箱インタラクト -> ステージクリア！
            if (player.TargetIsGoal) {
                player.IsStageCleared = true;
                player.ClearTimer = 0.0f;
                return;
            }

            // 2. 台座インタラクト (脱着)
            uint64_t pedId = player.TargetInstanceId;
            const PlacedObject* targetPed = nullptr;
            for (const auto& obj : objects) {
                if (obj.InstanceId == pedId) {
                    targetPed = &obj;
                    break;
                }
            }
            if (!targetPed) return;

            float sinYaw = std::sin(playerYaw);
            float cosYaw = std::cos(playerYaw);
            float sinPitch = std::sin(playerPitch);
            float cosPitch = std::cos(playerPitch);
            XMFLOAT3 forward(sinYaw * cosPitch, -sinPitch, cosYaw * cosPitch);
            XMFLOAT3 right(cosYaw, 0.0f, -sinYaw);
            XMFLOAT3 up(sinYaw * sinPitch, cosPitch, cosYaw * sinPitch);

            XMFLOAT3 handPos(
                eyePos.x + forward.x * 0.58f + right.x * 0.32f + up.x * -0.22f,
                eyePos.y + forward.y * 0.58f + right.y * 0.32f + up.y * -0.22f,
                eyePos.z + forward.z * 0.58f + right.z * 0.32f + up.z * -0.22f
            );

            XMFLOAT3 pedSocketPos(
                targetPed->Position.x,
                targetPed->Position.y + PEDESTAL_SOCKET_HEIGHT,
                targetPed->Position.z
            );

            if (player.IsHoldingOrb) {
                // 手持ち宝玉 -> 台座へ放物線配置アニメーション開始
                for (auto& orb : orbs) {
                    if (orb.OrbId == player.HeldOrbId) {
                        orb.IsAnimating = true;
                        orb.AnimTime = 0.0f;
                        orb.AnimDuration = 0.25f;
                        orb.AnimStartPos = orb.WorldPosition;
                        orb.AnimEndPos = pedSocketPos;
                        orb.TargetCarrier = OrbCarrier::Pedestal;
                        orb.TargetPedestalId = pedId;
                        break;
                    }
                }
                player.IsHoldingOrb = false;
                player.HeldOrbId = 0;
                player.PedestalInsertCount++;
            } else {
                // 台座上の宝玉 -> 手元へ放物線吸着アニメーション開始
                for (auto& orb : orbs) {
                    if (orb.Carrier == OrbCarrier::Pedestal && orb.PedestalInstanceId == pedId) {
                        orb.IsAnimating = true;
                        orb.AnimTime = 0.0f;
                        orb.AnimDuration = 0.25f;
                        orb.AnimStartPos = orb.WorldPosition;
                        orb.AnimEndPos = handPos;
                        orb.TargetCarrier = OrbCarrier::Player;
                        orb.TargetPedestalId = 0;
                        player.IsHoldingOrb = true;
                        player.HeldOrbId = orb.OrbId;
                        player.OrbPickupCount++;
                        break;
                    }
                }
            }
        }
    };
}
