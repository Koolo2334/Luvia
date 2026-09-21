module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <string>
#include <entt/entt.hpp>

export module App.Luminous.PlayerSystem;

import Engine.Core.SystemContext;
import Engine.Core.Components.Transform;
import Engine.Graphics.Components.Camera;
import Engine.Graphics.CameraAPI;
import Engine.Input;
import Engine.Math;
import App.Luminous.Types;
import App.Luminous.Optics;
import App.Luminous.TerrainSystem;
import App.Luminous.InputConfig;

export namespace App::Luminous {

    using namespace DirectX;
    using namespace Engine::Input;
    using namespace Engine::Math;

    // ====================================================================
    // プレイヤー状態コンポーネント (仕様書 第4条・第7条 準拠)
    // ====================================================================
    // 視線の先にあるインタラクト対象の種類 (画面下の操作説明の出し分けに使う)
    enum class LuminousInteractKind : uint8_t {
        None,
        TakeOrb,     // 宝玉が載った台座
        PlaceOrb,    // 宝玉を持った状態で空の台座
        OpenChest    // ゴールの宝箱
    };

    struct LuminousPlayerComponent {
        XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f }; // 足元ワールド座標
        float Yaw = 0.0f;                        // 水平視線角 (ラジアン, 0 = +Z)
        float Pitch = 0.0f;                      // 垂直視線角 (ラジアン, -89° ~ +89°)
        float EyeHeight = PLAYER_EYE_HEIGHT;     // 1.60m アイレベル
        float Radius = PLAYER_RADIUS;            // 0.35m 円柱半径
        float MoveSpeed = 4.2f;                  // 移動速度 (m/s)
        float LookSpeed = 0.0025f;               // マウス感度

        bool IsHoldingOrb = false;               // 宝玉手持ち中フラグ (最大1個)
        uint64_t HeldOrbId = 0;                  // 手持ち宝玉ID

        bool CanInteract = false;                // 視線内にインタラクト対象があるか
        std::string InteractPrompt;              // "[L] 宝玉を置く", "[L] 宝玉を取る", "[L] 宝箱を開く"
        LuminousInteractKind InteractKind = LuminousInteractKind::None;
        uint64_t TargetInstanceId = 0;           // インタラクト対象のInstanceId
        bool TargetIsGoal = false;               // 対象がゴール宝箱か

        bool IsStageCleared = false;                      // クリア達成フラグ
        float ClearTimer = 0.0f;                          // クリア演出タイマー
        int32_t OrbPickupCount = 0;                       // 宝玉所持回数 (Phase 6)
        int32_t PedestalInsertCount = 0;                  // 台座脱着回数 (Phase 6)

        // 慣性物理・ヘッドボブ・階段昇降用 (Phase 2)
        XMFLOAT3 Velocity = { 0.0f, 0.0f, 0.0f };         // 水平・垂直速度 (m/s)
        float WalkDistanceAccumulator = 0.0f;             // 歩行距離累積
        XMFLOAT2 HeadBobOffset = { 0.0f, 0.0f };          // ヘッドボブ変位 (X: 左右, Y: 上下)
        bool IsGrounded = true;                           // 接地中フラグ
        float CurrentFloorHeight = 0.0f;                  // 現在の足元基準面高さ
        bool IsOnStairs = false;                          // 階段昇降中フラグ

        // 足元消失退避グライド状態 (Phase 5)
        bool IsGliding = false;
        float GlideTimer = 0.0f;
        float GlideDuration = 0.15f;
        XMFLOAT3 GlideStartPos = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 GlideTargetPos = { 0.0f, 0.0f, 0.0f };

        // 反転壁復元斥力状態 (Phase 5)
        bool IsBeingRepelled = false;
        float GlideHudTimer = 0.0f;
        float RepulseHudTimer = 0.0f;
    };

    // ====================================================================
    // FPSプレイヤー制御システム (Phase 2 物理・階段・ヘッドボブ刷新)
    // ====================================================================
    class PlayerSystem {
    public:
        // ================================================================
        // Phase 2 調査/回帰テスト: 通常床 → 影になった反転床 → 向こう側 の通り抜け
        //
        // 壁で四方を囲んだ台座に宝玉を 1 個だけ置く。囲われているため宝玉の光は
        // 水平方向へ一切漏れず、隣接する反転床マスは全面が影になる。
        // このとき反転床は「足場」として残るため、通常床から反転床を渡って
        // 向こう側の通常床まで歩けなければならない。
        //
        // 対照として、囲いの壁を外した場合は反転床が穴になり手前で止まること。
        // ================================================================
        struct PhaseFloorTraversalResult {
            float EnclosedReachedZ = 0.0f;   // 囲いあり (影) で到達した Z
            float ExposedReachedZ = 0.0f;    // 囲いなし (穴) で到達した Z
            bool  EnclosedCrossed = false;
            bool  ExposedBlocked = false;
        };

        enum class TraversalWalls { None, Enclose, SingleWestEdge };

        static PhaseFloorTraversalResult SimulatePhaseFloorTraversal(TraversalWalls walls)
        {
            // --- シーン構築: 5x5 の通常床、セル (2,2) だけ反転床、台座+宝玉は (1,2)
            std::vector<PlacedObject> objects;
            uint64_t nextId = 1;
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject f;
                    f.InstanceId = nextId++;
                    f.AssetId = "Floor_1x1";
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    f.Phase = (x == 2 && z == 2) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    objects.push_back(f);
                }
            }

            PlacedObject ped;
            ped.InstanceId = nextId++;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 2, 0);
            ped.HasInitialOrb = true;
            objects.push_back(ped);
            const uint64_t pedId = ped.InstanceId;

            auto addWall = [&](int cx, int cz, CellEdge edge) {
                PlacedObject w;
                w.InstanceId = nextId++;
                w.AssetId = "Wall_Brick";
                w.Category = AssetCategory::Wall;
                w.Placement = PlacementType::EdgeSnap;
                w.FloorIndex = 0;
                w.Cell = GridCoord{ cx, cz, 0 };
                w.Edge = edge;
                float yaw = 0.0f;
                w.Position = EdgeToWorld(cx, cz, 0, edge, yaw);
                w.Rotation.y = yaw;
                objects.push_back(w);
            };

            if (walls == TraversalWalls::Enclose) {
                // 台座マスを四方から囲う -> 反転床は全面が影
                addWall(1, 2, CellEdge::North);
                addWall(1, 2, CellEdge::South);
                addWall(1, 2, CellEdge::East);
                addWall(1, 2, CellEdge::West);
            } else if (walls == TraversalWalls::SingleWestEdge) {
                // ユーザー報告の構成: 反転床マスの西辺に壁 1 枚、その向こうに台座
                addWall(2, 2, CellEdge::West);
            }

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);

            OrbRuntimeState orb;
            orb.Carrier = OrbCarrier::Pedestal;
            orb.PedestalInstanceId = pedId;
            orb.WorldPosition = XMFLOAT3(ped.Position.x, ped.Position.y + PEDESTAL_SOCKET_HEIGHT, ped.Position.z);
            orb.Radius = DEFAULT_ORB_RADIUS;
            std::vector<OrbRuntimeState> orbs{ orb };

            // --- セル (2,0) の中心から北へ 5cm 刻みで歩く
            XMFLOAT3 cur = GridToWorldCenter(2, 0, 0);
            const float goalZ = GridToWorldCenter(2, 4, 0).z;
            XMFLOAT3 vel(0.0f, 0.0f, 0.0f);

            int stuck = 0;
            for (int step = 0; step < 400 && cur.z < goalZ; ++step) {
                XMFLOAT3 desired(cur.x, cur.y, cur.z + 0.05f);

                // UpdatePlayer と同じ順序・同じ 2 パス構成で解決する
                for (int pass = 0; pass < 2; ++pass) {
                    desired = TerrainSystem::ConstrainMovementAgainstHoles(
                        cur, desired, objects, orbs, occluders, bakes, false, &vel);
                    desired = CollideWithWalls(
                        cur, desired, PLAYER_RADIUS, objects, orbs, occluders, bakes, false, vel);

                    if (HasFloorTileUnder(cur, objects, orbs, occluders, bakes) &&
                        !HasFloorTileUnder(desired, objects, orbs, occluders, bakes)) {
                        XMFLOAT3 testX(desired.x, desired.y, cur.z);
                        XMFLOAT3 testZ(cur.x, desired.y, desired.z);
                        if (HasFloorTileUnder(testX, objects, orbs, occluders, bakes)) {
                            desired.z = cur.z;
                        } else if (HasFloorTileUnder(testZ, objects, orbs, occluders, bakes)) {
                            desired.x = cur.x;
                        } else {
                            desired = cur;
                        }
                    }
                }

                const float advanced = desired.z - cur.z;
                cur = desired;
                if (advanced < 1e-4f) {
                    if (++stuck >= 5) break;
                } else {
                    stuck = 0;
                }
            }

            PhaseFloorTraversalResult r;
            if (walls == TraversalWalls::None) r.ExposedReachedZ = cur.z;
            else r.EnclosedReachedZ = cur.z;
            return r;
        }

        // ================================================================
        // Phase 2 回帰テスト: 反転床タイル内部の「影 <-> 光」境界での挙動
        //
        // 影の上を歩いて光の領域へ近づいたとき、通常の壁と同じように
        // その場で綺麗に停止しなければならない。
        // 以前は穴内部侵入の緊急退避が発火し、宝玉の光半径ぶん (数メートル)
        // 離れた別のマスまで放射状に押し出されていた。
        // ================================================================
        struct ShadowEdgeResult {
            bool  Built = false;          // 影と光が同居するタイルを作れたか
            bool  EnteredLit = false;     // 光 (穴) の中に入ってしまったか
            int   LitCells = 0;
            int   DarkCells = 0;
            float MaxJump = 0.0f;         // 1 ステップあたりの最大移動量 (m)
            float TotalDrift = 0.0f;      // 開始点からの最終的な距離 (m)
            float SlideDistance = 0.0f;   // 境界に押し当てたまま接線方向へ進めた距離 (m)
            float SlideRequested = 0.0f;  // 同じく要求した距離 (m)
            float SlideA = 0.0f;
            float SlideB = 0.0f;
            float EndU = 0.0f;            // 停止位置 (サブセル単位)
            float EndV = 0.0f;
            int   DirX = 0;
            int   DirZ = 0;
            int   StartIx = 0;
            int   StartIz = 0;
            float StepSize = 0.0f;
        };

        static ShadowEdgeResult SimulateShadowEdgeApproach()
        {
            ShadowEdgeResult r;

            // --- 5x5 の床。(2,2) が反転床。台座 (1,1) の宝玉に対し、
            //     (2,2) の南辺に通常壁を置くと、斜めから射す光が壁の東端を
            //     回り込むため、タイル内に影と光の境界ができる。
            std::vector<PlacedObject> objects;
            uint64_t nextId = 1;
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject f;
                    f.InstanceId = nextId++;
                    f.AssetId = "Floor_1x1";
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    f.Phase = (x == 2 && z == 2) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    objects.push_back(f);
                }
            }

            PlacedObject ped;
            ped.InstanceId = nextId++;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 1, 0);
            ped.HasInitialOrb = true;
            objects.push_back(ped);
            const uint64_t pedId = ped.InstanceId;

            PlacedObject w;
            w.InstanceId = nextId++;
            w.AssetId = "Wall_Brick";
            w.Category = AssetCategory::Wall;
            w.Placement = PlacementType::EdgeSnap;
            w.FloorIndex = 0;
            w.Cell = GridCoord{ 2, 2, 0 };
            w.Edge = CellEdge::South;
            float wYaw = 0.0f;
            w.Position = EdgeToWorld(2, 2, 0, CellEdge::South, wYaw);
            w.Rotation.y = wYaw;
            objects.push_back(w);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);

            OrbRuntimeState orb;
            orb.Carrier = OrbCarrier::Pedestal;
            orb.PedestalInstanceId = pedId;
            orb.WorldPosition = XMFLOAT3(ped.Position.x, ped.Position.y + PEDESTAL_SOCKET_HEIGHT, ped.Position.z);
            orb.Radius = DEFAULT_ORB_RADIUS;
            std::vector<OrbRuntimeState> orbs{ orb };

            // --- 反転床タイルのマスクを取り出し、影と光が同居していることを確認
            uint64_t phaseFloorId = 0;
            for (const auto& o : objects) {
                if (o.Category == AssetCategory::Floor && o.Phase == MaterialPhase::Phase) {
                    phaseFloorId = o.InstanceId;
                    break;
                }
            }
            const CircularHole* mask =
                TerrainSystem::FindPhaseFloorHoleMask(bakes, orb, phaseFloorId);
            if (mask == nullptr) return r;

            int litCount = 0, darkCount = 0;
            for (int iz = 0; iz < PHASE_MASK_DIM; ++iz) {
                for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                    if (mask->IsLitCell(ix, iz)) ++litCount; else ++darkCount;
                }
            }
            r.LitCells = litCount;
            r.DarkCells = darkCount;
            if (litCount == 0 || darkCount == 0) return r;   // 境界が存在しない構成

            const float sub = mask->CellSize / (float) PHASE_MASK_DIM;

            // --- 「影のサブセル」と、その隣が「光のサブセル」になっている箇所を探す。
            //     進入方向の手前側はプレイヤー半径ぶん以上が影であること。
            //     見つけたら、影が続く限り後退した地点をスタートにする。
            const int dirs[4][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 } };
            const int back = (int) std::ceil(PLAYER_RADIUS / sub) + 1;

            int startIx = -1, startIz = -1, dirX = 0, dirZ = 0;
            for (int d = 0; d < 4 && startIx < 0; ++d) {
                const int dx = dirs[d][0];
                const int dz = dirs[d][1];
                // タイル端はステージの通常壁が接している可能性があるため、
                // 進行方向と直交する側は端から余裕を取った範囲だけを候補にする
                const int margin = back + 2;
                for (int iz = 0; iz < PHASE_MASK_DIM && startIx < 0; ++iz) {
                    for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                        const int perp = (dx != 0) ? iz : ix;
                        if (perp < margin || perp > PHASE_MASK_DIM - 1 - margin) continue;
                        if (mask->IsLitCell(ix, iz)) continue;                 // ここは影
                        if (!mask->IsLitCell(ix + dx, iz + dz)) continue;      // 進行方向の隣が光

                        bool clear = true;
                        for (int k = 1; k <= back; ++k) {
                            const int bx = ix - dx * k;
                            const int bz = iz - dz * k;
                            if (bx < 0 || bx >= PHASE_MASK_DIM || bz < 0 || bz >= PHASE_MASK_DIM ||
                                mask->IsLitCell(bx, bz)) { clear = false; break; }
                        }
                        if (!clear) continue;

                        // 影が続く限りさらに後退して助走距離を稼ぐ
                        int sx = ix, sz = iz;
                        while (true) {
                            const int nx = sx - dx;
                            const int nz = sz - dz;
                            if (nx < 0 || nx >= PHASE_MASK_DIM || nz < 0 || nz >= PHASE_MASK_DIM) break;
                            if (mask->IsLitCell(nx, nz)) break;
                            sx = nx; sz = nz;
                        }
                        startIx = sx; startIz = sz; dirX = dx; dirZ = dz;
                        break;
                    }
                }
            }
            if (startIx < 0) return r;
            r.Built = true;

            XMFLOAT3 cur(mask->CellMinXZ.x + ((float) startIx + 0.5f) * sub,
                         0.0f,
                         mask->CellMinXZ.y + ((float) startIz + 0.5f) * sub);
            const XMFLOAT3 origin = cur;

            const float step = 0.02f;
            r.StepSize = step;
            XMFLOAT3 vel(0.0f, 0.0f, 0.0f);

            for (int i = 0; i < 120; ++i) {
                XMFLOAT3 desired(cur.x + (float) dirX * step, cur.y, cur.z + (float) dirZ * step);
                for (int pass = 0; pass < 2; ++pass) {
                    desired = TerrainSystem::ConstrainMovementAgainstHoles(
                        cur, desired, objects, orbs, occluders, bakes, false, &vel);
                    desired = CollideWithWalls(
                        cur, desired, PLAYER_RADIUS, objects, orbs, occluders, bakes, false, vel);
                }

                float jump = std::sqrt((desired.x - cur.x) * (desired.x - cur.x) +
                                       (desired.z - cur.z) * (desired.z - cur.z));
                if (jump > r.MaxJump) r.MaxJump = jump;
                cur = desired;

                // 光 (穴) のサブセルに入っていないこと
                int cix = (int) std::floor((cur.x - mask->CellMinXZ.x) / sub);
                int ciz = (int) std::floor((cur.z - mask->CellMinXZ.y) / sub);
                if (mask->IsLitCell(cix, ciz)) r.EnteredLit = true;
            }

            r.TotalDrift = std::sqrt((cur.x - origin.x) * (cur.x - origin.x) +
                                     (cur.z - origin.z) * (cur.z - origin.z));

            // --- 境界に押し当てたまま斜めに入力し、通常の壁と同じように接線方向へ
            //     滑れることを確認する。単に「その場で停止させるだけ」の実装だと
            //     ここが 0 になる。
            //
            //     影の輪郭は斜めに走るため、軸に沿った方向は接線にならない。
            //     停止位置における穴境界の法線を実測し、その接線を使う。
            const XMFLOAT3 slideStart = cur;

            float nX = 0.0f, nZ = 0.0f;
            {
                float bestD2 = 1e18f, qx = 0.0f, qz = 0.0f;
                for (int iz = 0; iz < PHASE_MASK_DIM; ++iz) {
                    for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                        if (!mask->IsLitCell(ix, iz)) continue;
                        const float sx0 = mask->CellMinXZ.x + (float) ix * sub;
                        const float sz0 = mask->CellMinXZ.y + (float) iz * sub;
                        const float cx = std::clamp(slideStart.x, sx0, sx0 + sub);
                        const float cz = std::clamp(slideStart.z, sz0, sz0 + sub);
                        const float d2 = (slideStart.x - cx) * (slideStart.x - cx) +
                                         (slideStart.z - cz) * (slideStart.z - cz);
                        if (d2 < bestD2) { bestD2 = d2; qx = cx; qz = cz; }
                    }
                }
                const float d = std::sqrt(bestD2);
                if (d < 1e-4f) return r;
                nX = (slideStart.x - qx) / d;
                nZ = (slideStart.z - qz) / d;
            }

            const int slideSteps = 40;
            constexpr float kDiag = 0.70710678f;
            r.SlideRequested = (float) slideSteps * step * kDiag;

            for (int sign = -1; sign <= 1; sign += 2) {
                // 接線 (境界に沿う向き)
                const float tX = -nZ * (float) sign;
                const float tZ = nX * (float) sign;
                // 「境界へ押し当てながら接線方向へ」 = 接線 - 法線 を正規化
                const float mX = (tX - nX) * kDiag;
                const float mZ = (tZ - nZ) * kDiag;

                XMFLOAT3 p = slideStart;
                XMFLOAT3 v(0.0f, 0.0f, 0.0f);
                for (int i = 0; i < slideSteps; ++i) {
                    XMFLOAT3 desired(p.x + mX * step, p.y, p.z + mZ * step);
                    for (int pass = 0; pass < 2; ++pass) {
                        desired = TerrainSystem::ConstrainMovementAgainstHoles(
                            p, desired, objects, orbs, occluders, bakes, false, &v);
                        desired = CollideWithWalls(
                            p, desired, PLAYER_RADIUS, objects, orbs, occluders, bakes, false, v);
                    }
                    p = desired;

                    int cix = (int) std::floor((p.x - mask->CellMinXZ.x) / sub);
                    int ciz = (int) std::floor((p.z - mask->CellMinXZ.y) / sub);
                    if (mask->IsLitCell(cix, ciz)) r.EnteredLit = true;
                }

                const float moved = (p.x - slideStart.x) * tX + (p.z - slideStart.z) * tZ;
                if (moved > r.SlideDistance) r.SlideDistance = moved;
                if (sign < 0) r.SlideA = moved; else r.SlideB = moved;
            }
            r.EndU = (slideStart.x - mask->CellMinXZ.x) / sub;
            r.EndV = (slideStart.z - mask->CellMinXZ.y) / sub;
            r.DirX = dirX; r.DirZ = dirZ;
            r.StartIx = startIx; r.StartIz = startIz;
            return r;
        }

        // ================================================================
        // 回帰テスト: 台座脱着アニメーション中もコリジョンが描画に追従すること
        //
        // 宝玉の光で消えている反転壁の中にプレイヤーが立っている状態で台座を
        // インタラクトすると、以前は 0.25 秒のアニメーションの間だけ
        // 当たり判定が宝玉を無視して壁が実体化し、プレイヤーが押し出されていた。
        // ================================================================
        static bool RunOrbAnimationCollisionTest(std::string& outReport)
        {
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

            // セル (1,1) の西辺に反転壁
            PlacedObject wall;
            wall.InstanceId = nextId++;
            wall.AssetId = "Wall_Brick";
            wall.Category = AssetCategory::Wall;
            wall.Placement = PlacementType::EdgeSnap;
            wall.FloorIndex = 0;
            wall.Cell = GridCoord{ 1, 1, 0 };
            wall.Edge = CellEdge::West;
            wall.Phase = MaterialPhase::Phase;
            float wYaw = 0.0f;
            wall.Position = EdgeToWorld(1, 1, 0, CellEdge::West, wYaw);
            wall.Rotation.y = wYaw;
            objects.push_back(wall);

            // すぐ西隣の台座に宝玉 (この壁は光で消える)
            PlacedObject ped;
            ped.InstanceId = nextId++;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(0, 1, 0);
            ped.HasInitialOrb = true;
            objects.push_back(ped);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);

            OrbRuntimeState orb;
            orb.OrbId = 1;
            orb.Carrier = OrbCarrier::Pedestal;
            orb.PedestalInstanceId = ped.InstanceId;
            orb.WorldPosition = XMFLOAT3(ped.Position.x, ped.Position.y + PEDESTAL_SOCKET_HEIGHT, ped.Position.z);
            orb.Radius = DEFAULT_ORB_RADIUS;

            // 消えている壁の中に立つ
            const XMFLOAT3 inWall(wall.Position.x, 0.0f, wall.Position.z);

            auto resolve = [&](const std::vector<OrbRuntimeState>& orbs, bool holding) {
                XMFLOAT3 vel(0.0f, 0.0f, 0.0f);
                return CollideWithWalls(inWall, inWall, PLAYER_RADIUS, objects, orbs,
                                        occluders, bakes, holding, vel);
            };

            auto pushDist = [&](const XMFLOAT3& p) {
                return std::sqrt((p.x - inWall.x) * (p.x - inWall.x) +
                                 (p.z - inWall.z) * (p.z - inWall.z));
            };

            // 1. 台座に据わったまま: 壁は消えているので押し出されない (前提条件)
            std::vector<OrbRuntimeState> settled{ orb };
            const float settledPush = pushDist(resolve(settled, false));

            // 2. アニメーション中 (放物線の頂点付近)。取得 (台座 -> 手元) と
            //    設置 (手元 -> 台座) の両方向を確認する。
            auto makeAnim = [&](OrbCarrier target) {
                OrbRuntimeState a = orb;
                a.IsAnimating = true;
                a.AnimTime = 0.125f;
                a.AnimDuration = 0.25f;
                a.AnimStartPos = orb.WorldPosition;
                a.AnimEndPos = XMFLOAT3(inWall.x + 0.4f, 1.4f, inWall.z);
                a.TargetCarrier = target;
                a.WorldPosition = XMFLOAT3(
                    (orb.WorldPosition.x + a.AnimEndPos.x) * 0.5f,
                    (orb.WorldPosition.y + a.AnimEndPos.y) * 0.5f + 0.45f,
                    (orb.WorldPosition.z + a.AnimEndPos.z) * 0.5f);
                return a;
            };

            std::vector<OrbRuntimeState> takeAnim{ makeAnim(OrbCarrier::Player) };
            const float takePush = pushDist(resolve(takeAnim, true));

            std::vector<OrbRuntimeState> putAnim{ makeAnim(OrbCarrier::Pedestal) };
            const float putPush = pushDist(resolve(putAnim, false));

            outReport = "settledPush=" + std::to_string(settledPush)
                + " takeAnimPush=" + std::to_string(takePush)
                + " putAnimPush=" + std::to_string(putPush);

            if (settledPush > 1e-3f) return false;  // 前提が崩れている
            if (takePush > 1e-3f) return false;     // 取得アニメーション中に押し出された
            if (putPush > 1e-3f) return false;      // 設置アニメーション中に押し出された

            return true;
        }

        // ================================================================
        // 回帰テスト: 宝玉所持中は反転マテリアルのコリジョンが影を参照しないこと
        //
        // 宝玉を壁際に寄せて意図しない影を作り、その影で残った反転床を
        // 足場として使う抜け道を封じる。
        // ================================================================
        static bool RunHeldOrbPhaseTransparencyTest(std::string& outReport)
        {
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
                    f.Phase = (x == 1 && z == 1) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    objects.push_back(f);
                }
            }
            // 反転床マスの西辺に通常壁 (この壁が影を作りうる)
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
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);

            uint64_t phaseFloorId = 0;
            for (const auto& o : objects) {
                if (o.Category == AssetCategory::Floor && o.Phase == MaterialPhase::Phase) {
                    phaseFloorId = o.InstanceId;
                    break;
                }
            }

            // 壁の西側 (影を作れる位置) で宝玉を持つ
            OrbRuntimeState held;
            held.OrbId = 1;
            held.Carrier = OrbCarrier::Player;
            held.Radius = DEFAULT_ORB_RADIUS;
            held.WorldPosition = XMFLOAT3(wall.Position.x - 0.35f, 1.35f, wall.Position.z);

            // 壁の影に入る反転床上の点 (壁の東側)
            const XMFLOAT3 shadowed(wall.Position.x + 0.45f, 0.0f, wall.Position.z + 0.30f);

            const bool litWhileHeld = TerrainSystem::IsPhaseFloorLit(
                bakes, occluders, held, phaseFloorId, shadowed.x, shadowed.z, 0.0f);

            // 同じ配置でも「所持していない移動中の宝玉」なら影は影として扱われる
            OrbRuntimeState moving = held;
            moving.Carrier = OrbCarrier::Pedestal;
            moving.IsAnimating = true;
            moving.TargetCarrier = OrbCarrier::Pedestal;
            const bool litWhileMoving = TerrainSystem::IsPhaseFloorLit(
                bakes, occluders, moving, phaseFloorId, shadowed.x, shadowed.z, 0.0f);

            // 反転壁の側も同様
            PlacedObject phaseWall = wall;
            phaseWall.InstanceId = nextId++;
            phaseWall.Cell = GridCoord{ 1, 1, 0 };
            phaseWall.Edge = CellEdge::East;
            phaseWall.Phase = MaterialPhase::Phase;
            float pYaw = 0.0f;
            phaseWall.Position = EdgeToWorld(1, 1, 0, CellEdge::East, pYaw);
            phaseWall.Rotation.y = pYaw;

            const bool wallOpenHeld = TerrainSystem::IsPhaseWallOpenAt(
                occluders, held, phaseWall, phaseWall.Position.x, phaseWall.Position.z);

            outReport = "litWhileHeld=" + std::string(litWhileHeld ? "yes" : "no")
                + " litWhileMoving=" + std::string(litWhileMoving ? "yes" : "no")
                + " wallOpenHeld=" + std::string(wallOpenHeld ? "yes" : "no");

            // 所持中は影を無視して「消えている」扱い
            if (!litWhileHeld) return false;
            if (!wallOpenHeld) return false;
            // 所持していない宝玉では従来どおり影が効く (前提の確認)
            if (litWhileMoving) return false;
            return true;
        }

        static bool RunPhaseFloorShadowEdgeTest(std::string& outReport)
        {
            const ShadowEdgeResult r = SimulateShadowEdgeApproach();

            outReport = "built=" + std::string(r.Built ? "yes" : "no")
                + " lit=" + std::to_string(r.LitCells) + " dark=" + std::to_string(r.DarkCells)
                + " enteredLit=" + std::string(r.EnteredLit ? "yes" : "no")
                + " maxJump=" + std::to_string(r.MaxJump)
                + " totalDrift=" + std::to_string(r.TotalDrift)
                + " slide=" + std::to_string(r.SlideDistance)
                + "/" + std::to_string(r.SlideRequested)
                + " (A=" + std::to_string(r.SlideA) + " B=" + std::to_string(r.SlideB) + ")"
                + " start=(" + std::to_string(r.StartIx) + "," + std::to_string(r.StartIz) + ")"
                + " dir=(" + std::to_string(r.DirX) + "," + std::to_string(r.DirZ) + ")"
                + " end=(" + std::to_string(r.EndU) + "," + std::to_string(r.EndV) + ")";

            if (!r.Built) return false;          // 影と光が同居するタイルを構築できていない
            if (r.EnteredLit) return false;      // 穴の中へ入ってしまった
            // 1 ステップで歩幅を大きく超える移動 = 押し出しによる瞬間移動
            if (r.MaxJump > r.StepSize * 2.0f) return false;
            // 開始点から光へ向かって進むだけなので、タイル 1 枚ぶん以上動くことはない
            if (r.TotalDrift > GRID_CELL_SIZE) return false;
            // 境界に押し当てたまま接線方向へ滑れること (通常の壁と同じ挙動)
            if (r.SlideDistance < r.SlideRequested * 0.5f) return false;

            return true;
        }

        static bool RunPhaseFloorTraversalTest(std::string& outReport)
        {
            const float cellSouthEdge = 2.0f * GRID_CELL_SIZE;   // 反転床セル (2,2) の南辺 z = 4.212
            const float cellNorthEdge = 3.0f * GRID_CELL_SIZE;   // 北辺 z = 6.318
            const float goalZ = GridToWorldCenter(2, 4, 0).z;    // 向こう側 z = 9.477

            const float enclosedZ = SimulatePhaseFloorTraversal(TraversalWalls::Enclose).EnclosedReachedZ;
            const float exposedZ = SimulatePhaseFloorTraversal(TraversalWalls::None).ExposedReachedZ;
            const float edgeWallZ = SimulatePhaseFloorTraversal(TraversalWalls::SingleWestEdge).EnclosedReachedZ;

            outReport = "enclosed=" + std::to_string(enclosedZ)
                + " edgeWall=" + std::to_string(edgeWallZ)
                + " exposed(hole)=" + std::to_string(exposedZ)
                + " / phaseCell z=[" + std::to_string(cellSouthEdge) + "," + std::to_string(cellNorthEdge) + "]"
                + " goalZ=" + std::to_string(goalZ);

            // 囲いあり: 反転床を渡り切って向こう側 (北辺より先) まで到達すること
            if (enclosedZ < cellNorthEdge) return false;
            // 西辺に壁 1 枚 (ユーザー報告構成): 中央縦断路は全て影なので同様に渡り切れること
            if (edgeWallZ < cellNorthEdge) return false;
            // 囲いなし: 穴の手前 (南辺付近) で止まること
            if (exposedZ >= cellSouthEdge) return false;

            return true;
        }

        // ================================================================
        // 回帰テスト: 階段を登りきってそのまま上の階の床へ歩いて進めるか。
        // 1F (0,0) -> 階段 (0,1)-(0,2) -> 2F (0,3)-(0,4) を北へ 5cm 刻みで歩く。
        // ================================================================
        static bool RunStairsClimbTest(std::string& outReport)
        {
            std::vector<PlacedObject> objects;
            uint64_t nextId = 1;
            auto addFloor = [&](int x, int z, int f) {
                PlacedObject o;
                o.InstanceId = nextId++;
                o.AssetId = "Floor_1x1";
                o.Category = AssetCategory::Floor;
                o.Placement = PlacementType::CellSnap;
                o.FloorIndex = f;
                o.Cell = GridCoord{ x, z, f };
                o.Position = GridToWorldCenter(x, z, f);
                objects.push_back(o);
            };
            auto addWall = [&](int x, int z, int f, CellEdge edge) {
                PlacedObject w;
                w.InstanceId = nextId++;
                w.AssetId = "Wall_Brick";
                w.Category = AssetCategory::Wall;
                w.Placement = PlacementType::EdgeSnap;
                w.FloorIndex = f;
                w.Cell = GridCoord{ x, z, f };
                w.Edge = edge;
                float yaw = 0.0f;
                w.Position = EdgeToWorld(x, z, f, edge, yaw);
                w.Rotation.y = yaw;
                objects.push_back(w);
            };

            addFloor(0, 0, 0);
            PlacedObject stairs;
            stairs.InstanceId = nextId++;
            stairs.AssetId = "Stairs_Straight";
            stairs.Category = AssetCategory::Stairs;
            stairs.Placement = PlacementType::CellSnap;
            stairs.FloorIndex = 0;
            stairs.Cell = GridCoord{ 0, 1, 0 };
            stairs.Position = XMFLOAT3(GridToWorldCenter(0, 1, 0).x, 0.0f, GridToWorldCenter(0, 1, 0).z + GRID_CELL_SIZE * 0.5f);
            objects.push_back(stairs);
            addFloor(0, 3, 1);
            addFloor(0, 4, 1);
            for (int z = 0; z <= 2; ++z) {
                addWall(0, z, 0, CellEdge::West);
                addWall(0, z, 0, CellEdge::East);
            }
            for (int z = 3; z <= 4; ++z) {
                addWall(0, z, 1, CellEdge::West);
                addWall(0, z, 1, CellEdge::East);
            }
            addWall(0, 4, 1, CellEdge::North);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);
            const std::vector<OrbRuntimeState> orbs;

            XMFLOAT3 cur = GridToWorldCenter(0, 0, 0);
            const float goalZ = GridToWorldCenter(0, 4, 1).z;
            XMFLOAT3 vel(0.0f, 0.0f, 0.0f);
            for (int step = 0; step < 600 && cur.z < goalZ; ++step) {
                XMFLOAT3 desired(cur.x, cur.y, cur.z + 0.05f);
                for (int pass = 0; pass < 2; ++pass) {
                    desired = TerrainSystem::ConstrainMovementAgainstHoles(
                        cur, desired, objects, orbs, occluders, bakes, false, &vel);
                    desired = CollideWithWalls(
                        cur, desired, PLAYER_RADIUS, objects, orbs, occluders, bakes, false, vel);
                    if (HasFloorTileUnder(cur, objects, orbs, occluders, bakes) &&
                        !HasFloorTileUnder(desired, objects, orbs, occluders, bakes)) {
                        desired = cur;
                    }
                }
                bool onStairs = false;
                const float groundY = EvaluateGroundHeight(desired, cur.y, objects, orbs, occluders, bakes, onStairs);
                if (groundY > -50.0f) desired.y = groundY;
                if (std::abs(desired.z - cur.z) < 1e-5f && std::abs(desired.x - cur.x) < 1e-5f) break; // 進めなくなった
                cur = desired;
            }

            outReport = "reached z=" + std::to_string(cur.z) + " y=" + std::to_string(cur.y)
                + " (goal z=" + std::to_string(goalZ) + ", 2F y=" + std::to_string(GRID_FLOOR_HEIGHT) + ")";
            return cur.z >= goalZ - 0.06f && std::abs(cur.y - GRID_FLOOR_HEIGHT) < 0.05f;
        }

        // ================================================================
        // Phase 2 回帰テスト: 反転床マスの辺に通常壁を立て、その向こう側の台座に
        // 宝玉を置いた構成で、影になった床がすべての当たり判定で「足場」として
        // 扱われることを検証する (ユーザー報告の再現手順そのまま)
        // ================================================================
        static bool RunPhaseFloorShadowCollisionTests(std::string& outErrorMsg)
        {
            // 実ステージ相当の構成を手で組む (5x5 の床 + 中央 (2,2) が反転床 + 台座 (1,1))
            std::vector<PlacedObject> objects;
            uint64_t nextId = 1;
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject f;
                    f.InstanceId = nextId++;
                    f.AssetId = "Floor_1x1";
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    f.Phase = (x == 2 && z == 2) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    objects.push_back(f);
                }
            }

            PlacedObject ped;
            ped.InstanceId = nextId++;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 2, 0); // 壁のちょうど向こう側
            ped.HasInitialOrb = true;
            objects.push_back(ped);

            // 反転床セル (2,2) の West 辺 (world x = 4.212) に通常壁。台座はこの壁の向こう側。
            PlacedObject blocker;
            blocker.InstanceId = nextId++;
            blocker.AssetId = "Wall_Brick";
            blocker.Category = AssetCategory::Wall;
            blocker.Placement = PlacementType::EdgeSnap;
            blocker.FloorIndex = 0;
            blocker.Cell = GridCoord{ 2, 2, 0 };
            blocker.Edge = CellEdge::West;
            float bYaw = 0.0f;
            blocker.Position = EdgeToWorld(2, 2, 0, CellEdge::West, bYaw);
            blocker.Rotation.y = bYaw;
            objects.push_back(blocker);

            const auto occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            const auto bakes = TerrainSystem::BakePedestalColliders(objects);

            // 台座 (1,1) に挿さった宝玉
            uint64_t pedId = 0;
            XMFLOAT3 pedPos(0.0f, 0.0f, 0.0f);
            for (const auto& o : objects) {
                if (o.Category == AssetCategory::Pedestal && o.HasInitialOrb) {
                    pedId = o.InstanceId;
                    pedPos = o.Position;
                    break;
                }
            }
            if (pedId == 0) { outErrorMsg = "ShadowCollision Failed: sample stage has no orb pedestal."; return false; }

            OrbRuntimeState orb;
            orb.Carrier = OrbCarrier::Pedestal;
            orb.PedestalInstanceId = pedId;
            orb.WorldPosition = XMFLOAT3(pedPos.x, pedPos.y + PEDESTAL_SOCKET_HEIGHT, pedPos.z);
            orb.Radius = DEFAULT_ORB_RADIUS;
            std::vector<OrbRuntimeState> orbs{ orb };

            // 壁の影になる床上の点 (セル (2,2) の北寄り)
            const XMFLOAT3 shadowPt = GridToWorldCenter(2, 2, 0); // 反転床セルの中心

            // 1. 事前ベイクのマスクが「影」と判定していること
            bool maskSaysShadow = false;
            for (const auto& b : bakes) {
                if (b.PedestalInstanceId != pedId) continue;
                for (const auto& h : b.Holes) {
                    if (!h.HasMask) continue;
                    if (h.IsLitAt(shadowPt.x, shadowPt.z)) continue;
                    maskSaysShadow = true;
                }
            }
            if (!maskSaysShadow) {
                outErrorMsg = "ShadowCollision Failed: the baked mask does not mark the point as shadowed.";
                return false;
            }

            // 2. 落下防止フェンスが「床がある」と答えること
            if (!HasFloorTileUnder(shadowPt, objects, orbs, occluders, bakes)) {
                outErrorMsg = "ShadowCollision Failed: HasFloorTileUnder treats the shadowed floor as a hole.";
                return false;
            }

            // 3. 接地高さが床面を返すこと
            bool onStairs = false;
            const float groundY = EvaluateGroundHeight(shadowPt, 0.0f, objects, orbs, occluders, bakes, onStairs);
            if (std::abs(groundY) > 0.05f) {
                outErrorMsg = "ShadowCollision Failed: EvaluateGroundHeight found no ground on the shadowed floor.";
                return false;
            }

            // 4. 穴コリジョンが押し戻さないこと
            const XMFLOAT3 fromPt(shadowPt.x, 0.0f, shadowPt.z - 0.40f);
            const XMFLOAT3 outPt = TerrainSystem::ConstrainMovementAgainstHoles(
                fromPt, shadowPt, objects, orbs, occluders, bakes, false, nullptr);
            if (std::abs(outPt.x - shadowPt.x) + std::abs(outPt.z - shadowPt.z) > 1e-3f) {
                outErrorMsg = "ShadowCollision Failed: ConstrainMovementAgainstHoles pushes the player off the shadowed floor.";
                return false;
            }

            return true;
        }

        // 水平位置に床タイル（または階段）が存在するか検査 (全フロア対応・反転床穴侵入防止)
        static bool HasFloorTileUnder(
            const XMFLOAT3& p,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionGeometry>& occluders,
            const std::vector<PedestalColliderBake>& bakedPedestals)
        {
            constexpr float half = GRID_CELL_SIZE * 0.5f;
            for (const auto& obj : objects) {
                if (obj.Category == AssetCategory::Floor || obj.AssetId == "StartDais") {
                    float floorY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                    if (std::abs(p.y - floorY) > 1.2f) {
                        continue;
                    }

                    if (p.x >= obj.Position.x - half && p.x <= obj.Position.x + half &&
                        p.z >= obj.Position.z - half && p.z <= obj.Position.z + half)
                    {
                        // 反転床の場合、光が照射されて穴化しているなら床として扱わない
                        if (obj.Phase == MaterialPhase::Phase) {
                            bool isHole = false;
                            for (const auto& orb : orbs) {
                                // アニメーション中の宝玉も補間位置で判定 (描画と一致させる)
                                float dy = std::abs(p.y - orb.WorldPosition.y);
                                if (dy < orb.Radius) {
                                    float rHole = std::sqrt(orb.Radius * orb.Radius - dy * dy);
                                    float dx = p.x - orb.WorldPosition.x;
                                    float dz = p.z - orb.WorldPosition.z;
                                    if (dx * dx + dz * dz < rHole * rHole &&
                                        TerrainSystem::IsPhaseFloorLit(bakedPedestals, occluders, orb, obj.InstanceId,
                                                                       p.x, p.z, obj.Position.y)) {
                                        isHole = true;
                                        break;
                                    }
                                }
                            }
                            if (isHole) continue;
                        }
                        return true;
                    }
                } else if (obj.Category == AssetCategory::Stairs || obj.AssetId == "Stairs_Straight") {
                    float stairBaseY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                    if (p.y >= stairBaseY - 0.5f && p.y <= stairBaseY + GRID_FLOOR_HEIGHT + 1.2f) {
                        float rad = XMConvertToRadians(obj.Rotation.y);
                        float cosR = std::cos(rad);
                        float sinR = std::sin(rad);
                        float dx = p.x - obj.Position.x;
                        float dz = p.z - obj.Position.z;
                        float localX = dx * cosR - dz * sinR;
                        float localZ = dx * sinR + dz * cosR;
                        if (std::abs(localX) <= 1.053f + 0.15f && localZ >= -2.106f - 0.25f && localZ <= 2.106f + 0.25f) {
                            return true;
                        }
                    }
                }
            }
            return false;
        }

        // 接地高さ・階段昇降高さの計算 (仕様書 2.1 / 2.2)
        static float EvaluateGroundHeight(
            const XMFLOAT3& pos,
            float currentY,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionGeometry>& occluders,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            bool& outIsOnStairs)
        {
            outIsOnStairs = false;

            // 1. 階段 (Stairs_Straight) 判定 (傾斜角 36.06°)
            for (const auto& obj : objects) {
                if (obj.AssetId != "Stairs_Straight") continue;

                float rad = XMConvertToRadians(obj.Rotation.y);
                float cosR = std::cos(rad);
                float sinR = std::sin(rad);

                float dx = pos.x - obj.Position.x;
                float dz = pos.z - obj.Position.z;

                float localX = dx * cosR - dz * sinR;
                float localZ = dx * sinR + dz * cosR;

                constexpr float halfWidth = 1.053f;  // 幅 2.106m
                constexpr float halfLength = 2.106f; // 奥行き 4.212m

                if (std::abs(localX) <= halfWidth + 0.15f && localZ >= -halfLength - 0.25f && localZ <= halfLength + 0.25f) {
                    float t = std::clamp((localZ + halfLength) / (2.0f * halfLength), 0.0f, 1.0f);
                    float stairY = obj.Position.y + t * GRID_FLOOR_HEIGHT;

                    // 横や後ろからの吸い付き防止: プレイヤー高さが斜面より45cm以上低い場合は登れない
                    if (currentY < stairY - 0.45f) {
                        continue;
                    }

                    if (currentY >= obj.Position.y - 0.5f && currentY <= obj.Position.y + GRID_FLOOR_HEIGHT + 1.2f) {
                        outIsOnStairs = true;
                        return stairY;
                    }
                }
            }

            // 2. 通常床・召喚陣の上面高さ判定
            float highestFloorBelow = 0.0f;
            bool foundFloor = false;

            for (const auto& obj : objects) {
                if (obj.Category != AssetCategory::Floor && obj.AssetId != "StartDais") continue;

                float half = GRID_CELL_SIZE * 0.5f;
                if (pos.x >= obj.Position.x - half && pos.x <= obj.Position.x + half &&
                    pos.z >= obj.Position.z - half && pos.z <= obj.Position.z + half)
                {
                    // 反転床の場合、光が照射されて穴化しているなら床として扱わない
                    if (obj.Phase == MaterialPhase::Phase) {
                        bool isHole = false;
                        for (const auto& orb : orbs) {
                            // アニメーション中の宝玉も補間位置で判定 (描画と一致させる)
                            float dy = std::abs(pos.y - orb.WorldPosition.y);
                            if (dy < orb.Radius) {
                                float rHole = std::sqrt(orb.Radius * orb.Radius - dy * dy);
                                float dx = pos.x - orb.WorldPosition.x;
                                float dz = pos.z - orb.WorldPosition.z;
                                if (dx * dx + dz * dz < rHole * rHole &&
                                    TerrainSystem::IsPhaseFloorLit(bakedPedestals, occluders, orb, obj.InstanceId,
                                                                   pos.x, pos.z, obj.Position.y)) {
                                    isHole = true;
                                    break;
                                }
                            }
                        }
                        if (isHole) continue; // 穴化しているため接地無効（空中歩行阻止）
                    }

                    float surfaceY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                    if (obj.AssetId == "StartDais") {
                        surfaceY += 0.28f;
                    }

                    if (surfaceY <= currentY + 0.5f) {
                        if (!foundFloor || surfaceY > highestFloorBelow) {
                            highestFloorBelow = surfaceY;
                            foundFloor = true;
                        }
                    }
                }
            }

            if (foundFloor) {
                return highestFloorBelow;
            }

            return -100.0f; // 穴または床外（空中歩行完全防止）
        }

        // 移動と視線回転の更新
        static void UpdatePlayer(
            LuminousPlayerComponent& player,
            Engine::Core::TransformComponent& camTransform,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionWall>& occlusionWalls,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            float deltaTime)
        {
            if (player.IsStageCleared) {
                player.ClearTimer += deltaTime;
                return; // クリア演出中は移動停止
            }

            // ------------------------------------------------------------
            // 0. 足元消失時の滑らかな0.15秒スライド退避中 (Phase 5)
            // ------------------------------------------------------------
            if (player.IsGliding) {
                player.GlideTimer += deltaTime;
                float tau = std::clamp(player.GlideTimer / player.GlideDuration, 0.0f, 1.0f);
                float s = tau * tau * (3.0f - 2.0f * tau); // Smoothstep
                player.Position.x = (1.0f - s) * player.GlideStartPos.x + s * player.GlideTargetPos.x;
                player.Position.z = (1.0f - s) * player.GlideStartPos.z + s * player.GlideTargetPos.z;
                player.Velocity = { 0.0f, 0.0f, 0.0f };

                if (tau >= 1.0f) {
                    player.IsGliding = false;
                    player.Position = player.GlideTargetPos;
                }

                camTransform.LocalPosition = XMFLOAT3(
                    player.Position.x,
                    player.Position.y + player.EyeHeight,
                    player.Position.z
                );
                camTransform.IsDirty = true;
                return;
            }

            // ------------------------------------------------------------
            // 1. 視線回転 (マウス / ゲームパッド)
            // ------------------------------------------------------------
            if (Input::IsCursorLocked()) {
                Vector2 mouseDelta = Input::GetMouseDelta();
                player.Yaw += mouseDelta.x * player.LookSpeed;
                player.Pitch += mouseDelta.y * player.LookSpeed;
            }

            // ゲームパッドの右スティックによる視点操作 (Look アクション)
            {
                const Vector2 look = LuminousInput::LookAxis();
                if (look.x != 0.0f || look.y != 0.0f) {
                    // 応答カーブ: 倒し量の大きさだけを指数で曲げ、向きは保つ
                    const float mag = std::sqrt(look.x * look.x + look.y * look.y);
                    const float curved = std::pow((std::min)(mag, 1.0f), (std::max)(0.1f, g_LuminousConfig.PadLookResponseExponent));
                    const float k = (mag > 1e-4f) ? curved / mag : 0.0f;
                    player.Yaw += look.x * k * g_LuminousConfig.PadLookSpeedX * deltaTime;
                    // スティックは上が +Y。画面の上を向くには Pitch を減らす
                    player.Pitch -= look.y * k * g_LuminousConfig.PadLookSpeedY * deltaTime;
                }
            }

            constexpr float PITCH_LIMIT = XMConvertToRadians(88.0f);
            player.Pitch = std::clamp(player.Pitch, -PITCH_LIMIT, PITCH_LIMIT);
            if (player.Yaw > XM_2PI) player.Yaw -= XM_2PI;
            if (player.Yaw < 0.0f)   player.Yaw += XM_2PI;

            // デバッグテストキー (Phase 5 自動検証用)。
            // レギュレーション対象外の入力なので、コマンドラインでシーンを直接
            // 指定した開発起動時のみ有効。通常のプレイでは無効。
            if (LuminousDebugKeys::Enabled) {
            if (Input::GetKeyHold(KeyCode::F7)) {
                // Anti-Crush テスト: 反転壁復元境界に配置
                player.Position = XMFLOAT3(5.265f, 0.0f, 6.318f);
                player.IsBeingRepelled = true;
                player.RepulseHudTimer = 1.2f;
            }
            if (Input::GetKeyHold(KeyCode::F8)) {
                // Footing Loss テスト: 反転床の穴内部に配置 (CheckFootingLossTriggerが自動検知して安全床へグライド)
                player.Position = XMFLOAT3(5.265f, 0.0f, 5.265f);
            }
            if (Input::GetKeyHold(KeyCode::F10)) {
                // Hole Constraint テスト: 反転床の穴手前(4.2, 0, 4.2)に配置、穴を見下ろす
                player.Position = XMFLOAT3(4.2f, 0.0f, 4.2f);
                player.Yaw = XMConvertToRadians(45.0f);
                player.Pitch = XMConvertToRadians(35.0f);
            }
            if (Input::GetKeyHold(KeyCode::F9)) {
                // GoalChest テスト: 宝箱直前に配置、45度で宝箱を見下ろす
                for (const auto& obj : objects) {
                    if (obj.AssetId == "GoalChest") {
                        player.Position = XMFLOAT3(obj.Position.x - 0.7f, obj.Position.y, obj.Position.z - 0.7f);
                        player.Yaw = XMConvertToRadians(45.0f);
                        player.Pitch = XMConvertToRadians(28.0f);
                        break;
                    }
                }
            }
            if (Input::GetKeyHold(KeyCode::F6)) {
                // Stairs entrance test: In front of stairs facing North (ascending)
                player.Position = XMFLOAT3(7.371f, 0.0f, 1.4f);
                player.Yaw = 0.0f;
                player.Pitch = 0.0f;
            }
            if (Input::GetKeyHold(KeyCode::F11)) {
                // 2F Balcony test: On 2F balcony looking South down the stairs
                player.Position = XMFLOAT3(7.371f, 3.063f, 6.8f);
                player.Yaw = XMConvertToRadians(180.0f);
                player.Pitch = XMConvertToRadians(25.0f);
            }
            if (Input::GetKeyHold(KeyCode::F3)) {
                // Candle inspection: In front of candle looking directly at it
                player.Position = XMFLOAT3(0.553f, 0.0f, 2.0f);
                player.Yaw = 0.0f; // facing North
                player.Pitch = XMConvertToRadians(50.0f); // looking down at candle
            }
            if (Input::GetKeyHold(KeyCode::F4)) {
                // Torch inspection: Backed up from wall torch looking at West wall
                player.Position = XMFLOAT3(1.8f, 0.0f, 5.265f);
                player.Yaw = XMConvertToRadians(270.0f); // facing West
                player.Pitch = XMConvertToRadians(-20.0f); // looking up at torch and flame
            }
            if (Input::GetKeyHold(KeyCode::F1)) {
                // Phase 2 traversal test: south of the phase floor cell (2,2), facing North
                player.Position = XMFLOAT3(5.265f, 0.0f, 3.159f);
                player.Yaw = 0.0f;
                player.Pitch = 0.0f;
            }
            if (Input::GetKeyHold(KeyCode::F2)) {
                // Phase 2 shadow-edge test: east side of the phase floor cell (2,2), facing West
                player.Position = XMFLOAT3(6.252f, 0.0f, 5.067f);
                player.Yaw = XMConvertToRadians(270.0f);
                player.Pitch = 0.0f;
            }

            } // LuminousDebugKeys::Enabled

            // ------------------------------------------------------------
            // 2. 移動入力 (WASD) と運動方程式（指数関数的慣性補間）
            // ------------------------------------------------------------
            float forwardInput = 0.0f;
            float rightInput = 0.0f;

            // W/A/S/D・左スティック・十字キーを合成した Move アクション
            {
                const Vector2 move = LuminousInput::MoveAxis();
                forwardInput = move.y;
                rightInput = move.x;
            }

            float sinYaw = std::sin(player.Yaw);
            float cosYaw = std::cos(player.Yaw);

            // 前方ベクトル & 右ベクトル (水平面上)
            XMFLOAT3 forwardVec(sinYaw, 0.0f, cosYaw);
            XMFLOAT3 rightVec(cosYaw, 0.0f, -sinYaw);

            XMFLOAT3 moveDir = { 0.0f, 0.0f, 0.0f };
            if (forwardInput != 0.0f || rightInput != 0.0f) {
                moveDir.x = forwardVec.x * forwardInput + rightVec.x * rightInput;
                moveDir.z = forwardVec.z * forwardInput + rightVec.z * rightInput;

                float len = std::sqrt(moveDir.x * moveDir.x + moveDir.z * moveDir.z);
                if (len > 1e-4f) {
                    moveDir.x /= len;
                    moveDir.z /= len;
                }
            }

            // 目標速度 (v_target)
            float currentMoveSpeed = g_LuminousConfig.MoveSpeed;
            XMFLOAT3 targetVelocity = {
                moveDir.x * currentMoveSpeed,
                0.0f,
                moveDir.z * currentMoveSpeed
            };

            // 運動方程式（慣性補間・加減速）:
            // v(t+dt) = v(t) + (v_target - v(t)) * (1 - e^(-k * dt))
            // 加速時 k = 18.0 s^-1、減速・制動時 k = 24.0 s^-1
            float targetSpeed = std::sqrt(targetVelocity.x * targetVelocity.x + targetVelocity.z * targetVelocity.z);
            float k = (targetSpeed > 0.01f) ? 18.0f : 24.0f;
            float alpha = 1.0f - std::exp(-k * deltaTime);

            player.Velocity.x += (targetVelocity.x - player.Velocity.x) * alpha;
            player.Velocity.z += (targetVelocity.z - player.Velocity.z) * alpha;

            if (std::abs(player.Velocity.x) < 1e-4f) player.Velocity.x = 0.0f;
            if (std::abs(player.Velocity.z) < 1e-4f) player.Velocity.z = 0.0f;

            float currentSpeed = std::sqrt(player.Velocity.x * player.Velocity.x + player.Velocity.z * player.Velocity.z);

            // ------------------------------------------------------------
            // 3. 移動試行とコリジョン判定
            // ------------------------------------------------------------
            XMFLOAT3 currentPos = player.Position;
            XMFLOAT3 desiredPos = {
                currentPos.x + player.Velocity.x * deltaTime,
                currentPos.y,
                currentPos.z + player.Velocity.z * deltaTime
            };

            // ------------------------------------------------------------
            // 3. コリジョン判定（反発力ゼロ・通常の壁と同様に綺麗に停止・2パス完全拘束）
            // ------------------------------------------------------------
            // 2パス反復判定 (穴進入阻止 -> 壁判定 を2回実行し、常に壁判定を最終確定として壁すり抜けを数学的に根絶)
            for (int pass = 0; pass < 2; ++pass) {
                // 3-1. 反転床の穴への進入阻止（通常の壁と同様の非弾性コリジョン・綺麗に停止＆接線スライド）
                desiredPos = TerrainSystem::ConstrainMovementAgainstHoles(
                    currentPos, desiredPos, objects, orbs, occlusionWalls, bakedPedestals, player.IsHoldingOrb, &player.Velocity);

                // 3-2. 通常壁・格子・反転壁との円柱コライダースライド判定（反発ゼロ・壁滑走）
                desiredPos = CollideWithWalls(
                    currentPos, desiredPos, player.Radius, objects, orbs, occlusionWalls, bakedPedestals, player.IsHoldingOrb, player.Velocity);

                // 3-3. 床マス非存在領域への侵入防止（Item 7: 奈落落下防止フェンス - 全階層・穴完全対応）
                if (HasFloorTileUnder(currentPos, objects, orbs, occlusionWalls, bakedPedestals) && !HasFloorTileUnder(desiredPos, objects, orbs, occlusionWalls, bakedPedestals)) {
                    XMFLOAT3 testX(desiredPos.x, desiredPos.y, currentPos.z);
                    XMFLOAT3 testZ(currentPos.x, desiredPos.y, desiredPos.z);
                    if (HasFloorTileUnder(testX, objects, orbs, occlusionWalls, bakedPedestals)) {
                        desiredPos.z = currentPos.z;
                        player.Velocity.z = 0.0f;
                    } else if (HasFloorTileUnder(testZ, objects, orbs, occlusionWalls, bakedPedestals)) {
                        desiredPos.x = currentPos.x;
                        player.Velocity.x = 0.0f;
                    } else {
                        desiredPos.x = currentPos.x;
                        desiredPos.z = currentPos.z;
                        player.Velocity.x = 0.0f;
                        player.Velocity.z = 0.0f;
                    }
                }
            }

            player.IsBeingRepelled = false;
            player.RepulseHudTimer = 0.0f;
            player.IsGliding = false;
            player.GlideHudTimer = 0.0f;

            // 速度の最大値クランプ（過大速度の防止）
            float curSpd = std::sqrt(player.Velocity.x * player.Velocity.x + player.Velocity.z * player.Velocity.z);
            if (curSpd > currentMoveSpeed) {
                player.Velocity.x *= (currentMoveSpeed / curSpd);
                player.Velocity.z *= (currentMoveSpeed / curSpd);
            }

            // ------------------------------------------------------------
            // 4. 接地判定と階段昇降物理（Stair Traversal - 仕様書 2.1 / 2.2）
            // ------------------------------------------------------------
            bool onStairs = false;
            float targetGroundY = EvaluateGroundHeight(desiredPos, currentPos.y, objects, orbs, occlusionWalls, bakedPedestals, onStairs);
            player.IsOnStairs = onStairs;

            if (onStairs) {
                // 階段上では傾斜面高さに直接追従（段差ガタつきゼロ・傾斜36.06°連続滑走）
                desiredPos.y = targetGroundY;
            } else if (targetGroundY > -50.0f) {
                // 通常床・召喚陣では微小差スナップ＋滑らかな高低差補間
                float diffY = targetGroundY - desiredPos.y;
                if (std::abs(diffY) < 0.02f) {
                    desiredPos.y = targetGroundY;
                } else {
                    desiredPos.y += diffY * (1.0f - std::exp(-20.0f * deltaTime));
                }
            } else {
                // 穴または奈落の上にいる場合: 地面なし（落下）
                if (!player.IsGliding) {
                    desiredPos.y -= 9.8f * deltaTime;
                }
            }

            // 奈落落下リスポーン (ステージ開始地点へ安全復帰)
            if (desiredPos.y < -15.0f) {
                desiredPos = { 1.5f * GRID_CELL_SIZE, 0.0f, 1.5f * GRID_CELL_SIZE };
                player.Velocity = { 0.0f, 0.0f, 0.0f };
            }
            player.CurrentFloorHeight = targetGroundY;
            player.Position = desiredPos;

            // ------------------------------------------------------------
            // 5. ヘッドボブ計算（歩行時の視界微細揺れ - 仕様書 2.2）
            // Bob_Y = 0.025 * sin(8*pi*walkDist), Bob_X = 0.015 * cos(4*pi*walkDist)
            // ------------------------------------------------------------
            if (currentSpeed > 0.15f) {
                float bobFreq = 4.0f * g_LuminousConfig.CameraShakeSpeed;
                player.WalkDistanceAccumulator += currentSpeed * deltaTime;
                float targetBobY = 0.025f * g_LuminousConfig.CameraShakeIntensity * std::sin(player.WalkDistanceAccumulator * XM_PI * bobFreq);
                float targetBobX = 0.015f * g_LuminousConfig.CameraShakeIntensity * std::cos(player.WalkDistanceAccumulator * XM_PI * (bobFreq * 0.5f));

                player.HeadBobOffset.x += (targetBobX - player.HeadBobOffset.x) * (1.0f - std::exp(-15.0f * deltaTime));
                player.HeadBobOffset.y += (targetBobY - player.HeadBobOffset.y) * (1.0f - std::exp(-15.0f * deltaTime));
            } else {
                // 静止時は速やかに中心(0, 0)へ滑らかに減衰
                player.HeadBobOffset.x += (0.0f - player.HeadBobOffset.x) * (1.0f - std::exp(-12.0f * deltaTime));
                player.HeadBobOffset.y += (0.0f - player.HeadBobOffset.y) * (1.0f - std::exp(-12.0f * deltaTime));
            }

            // ------------------------------------------------------------
            // 6. カメラTransformの同期 (アイレベル 1.6m + ヘッドボブ)
            // ------------------------------------------------------------
            camTransform.LocalPosition = XMFLOAT3(
                player.Position.x + rightVec.x * player.HeadBobOffset.x,
                player.Position.y + player.EyeHeight + player.HeadBobOffset.y,
                player.Position.z + rightVec.z * player.HeadBobOffset.x
            );

            XMVECTOR rotQuat = XMQuaternionRotationRollPitchYaw(player.Pitch, player.Yaw, 0.0f);
            XMStoreFloat4(&camTransform.LocalRotation, rotQuat);
            camTransform.IsDirty = true;
        }

        // ================================================================
        // 視線レイキャストによるインタラクト対象検出 (台座 / ゴール)
        // ================================================================
        static void UpdateInteractionRaycast(
            LuminousPlayerComponent& player,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs)
        {
            player.CanInteract = false;
            player.InteractPrompt.clear();
            player.InteractKind = LuminousInteractKind::None;
            player.TargetInstanceId = 0;
            player.TargetIsGoal = false;

            if (player.IsStageCleared) return;

            XMFLOAT3 eyePos(player.Position.x, player.Position.y + player.EyeHeight, player.Position.z);
            float sinYaw = std::sin(player.Yaw);
            float cosYaw = std::cos(player.Yaw);
            float sinPitch = std::sin(player.Pitch);
            float cosPitch = std::cos(player.Pitch);

            // 視線レイ方向
            XMFLOAT3 rayDir(
                sinYaw * cosPitch,
                -sinPitch,
                cosYaw * cosPitch
            );

            float bestDist = g_LuminousConfig.InteractionDistance;

            for (const auto& obj : objects) {
                // 同一フロア判定: プレイヤー足元高さとオブジェクト高さの差が1.2m以上なら不可（上下階層への誤インタラクト根絶）
                if (std::abs(player.Position.y - obj.Position.y) > 1.2f) continue;

                // 壁遮蔽（LOS: Line of Sight）判定: 視線上に同一階層の壁があればインタラクト不可
                bool occluded = false;
                for (const auto& wall : objects) {
                    if (wall.Category != AssetCategory::Wall) continue;
                    if (std::abs(wall.Position.y - obj.Position.y) > 1.2f) continue;

                    // 反転壁で光が開口している場合は遮蔽なし
                    if (wall.Phase == MaterialPhase::Phase) {
                        if (player.IsHoldingOrb) continue;
                        bool cutoutOpen = false;
                        for (const auto& orb : orbs) {
                            if (orb.Carrier == OrbCarrier::Pedestal && !orb.IsAnimating) {
                                float dy = std::abs(orb.WorldPosition.y - wall.Position.y);
                                if (dy < orb.Radius) {
                                    float rCutout = std::sqrt(orb.Radius * orb.Radius - dy * dy);
                                    float dwx = wall.Position.x - orb.WorldPosition.x;
                                    float dwz = wall.Position.z - orb.WorldPosition.z;
                                    if (dwx * dwx + dwz * dwz < rCutout * rCutout) {
                                        cutoutOpen = true;
                                        break;
                                    }
                                }
                            }
                        }
                        if (cutoutOpen) continue;
                    }

                    float halfLen = GRID_CELL_SIZE * 0.5f;
                    bool isRot = (std::abs(std::fmod(wall.Rotation.y, 180.0f)) > 45.0f);
                    float q1x = wall.Position.x - (isRot ? 0.0f : halfLen);
                    float q1z = wall.Position.z - (isRot ? halfLen : 0.0f);
                    float q2x = wall.Position.x + (isRot ? 0.0f : halfLen);
                    float q2z = wall.Position.z + (isRot ? halfLen : 0.0f);

                    auto ccw = [](float ax, float az, float bx, float bz, float cx, float cz) {
                        return (cz - az) * (bx - ax) > (bz - az) * (cx - ax);
                    };

                    if ((ccw(eyePos.x, eyePos.z, obj.Position.x, obj.Position.z, q1x, q1z) !=
                         ccw(eyePos.x, eyePos.z, obj.Position.x, obj.Position.z, q2x, q2z)) &&
                        (ccw(q1x, q1z, q2x, q2z, eyePos.x, eyePos.z) !=
                         ccw(q1x, q1z, q2x, q2z, obj.Position.x, obj.Position.z)))
                    {
                        occluded = true;
                        break;
                    }
                }
                if (occluded) continue;

                // 1. 台座 (Pedestal)
                if (obj.AssetId == "Pedestal" || obj.Category == AssetCategory::Pedestal) {
                    float toX = obj.Position.x - eyePos.x;
                    float toZ = obj.Position.z - eyePos.z;
                    float horizDist = std::sqrt(toX * toX + toZ * toZ);

                    // 台座中心ソケット
                    XMFLOAT3 socketPos(obj.Position.x, obj.Position.y + PEDESTAL_SOCKET_HEIGHT, obj.Position.z);
                    float toY = socketPos.y - eyePos.y;
                    float dist3D = std::sqrt(toX * toX + toY * toY + toZ * toZ);

                    if (dist3D <= bestDist || (horizDist <= g_LuminousConfig.InteractionDistance && std::abs(eyePos.y - (obj.Position.y + 0.6f)) < 2.5f)) {
                        float horizRayLen = std::sqrt(rayDir.x * rayDir.x + rayDir.z * rayDir.z);
                        float horizDot = (horizRayLen > 1e-4f && horizDist > 1e-4f)
                            ? (toX * rayDir.x + toZ * rayDir.z) / (horizDist * horizRayLen)
                            : 0.0f;

                        float dot3D = (toX * rayDir.x + toY * rayDir.y + toZ * rayDir.z) / (dist3D > 1e-4f ? dist3D : 1.0f);
                        float rayYAtTarget = eyePos.y + rayDir.y * horizDist;
                        bool hitDirection = (dot3D > 0.60f) || (horizDot > 0.60f && rayYAtTarget >= obj.Position.y - 0.5f && rayYAtTarget <= obj.Position.y + 2.5f);

                        if (hitDirection) {
                            // 台座に宝玉が載っているか調べる
                            bool hasOrbOnPedestal = false;
                            for (const auto& orb : orbs) {
                                if (orb.Carrier == OrbCarrier::Pedestal && orb.PedestalInstanceId == obj.InstanceId) {
                                    hasOrbOnPedestal = true;
                                    break;
                                }
                            }

                            if (player.IsHoldingOrb) {
                                if (!hasOrbOnPedestal) {
                                    bestDist = dist3D;
                                    player.CanInteract = true;
                                    player.InteractPrompt = "[L] Place Orb on Pedestal";
                                    player.InteractKind = LuminousInteractKind::PlaceOrb;
                                    player.TargetInstanceId = obj.InstanceId;
                                    player.TargetIsGoal = false;
                                }
                            } else {
                                if (hasOrbOnPedestal) {
                                    bestDist = dist3D;
                                    player.CanInteract = true;
                                    player.InteractPrompt = "[L] Take Orb";
                                    player.InteractKind = LuminousInteractKind::TakeOrb;
                                    player.TargetInstanceId = obj.InstanceId;
                                    player.TargetIsGoal = false;
                                }
                            }
                        }
                    }
                }

                // 2. ゴール宝箱 (GoalChest)
                if (obj.AssetId == "GoalChest") {
                    float toX = obj.Position.x - eyePos.x;
                    float toZ = obj.Position.z - eyePos.z;
                    float horizDist = std::sqrt(toX * toX + toZ * toZ);

                    XMFLOAT3 chestCenter(obj.Position.x, obj.Position.y + 0.6f, obj.Position.z);
                    float toY = chestCenter.y - eyePos.y;
                    float dist3D = std::sqrt(toX * toX + toY * toY + toZ * toZ);

                    if (dist3D <= bestDist || (horizDist <= g_LuminousConfig.InteractionDistance && std::abs(eyePos.y - (obj.Position.y + 0.6f)) < 2.5f)) {
                        float horizRayLen = std::sqrt(rayDir.x * rayDir.x + rayDir.z * rayDir.z);
                        float horizDot = (horizRayLen > 1e-4f && horizDist > 1e-4f)
                            ? (toX * rayDir.x + toZ * rayDir.z) / (horizDist * horizRayLen)
                            : 0.0f;

                        float dot3D = (toX * rayDir.x + toY * rayDir.y + toZ * rayDir.z) / (dist3D > 1e-4f ? dist3D : 1.0f);
                        float rayYAtTarget = eyePos.y + rayDir.y * horizDist;
                        bool hitDirection = (dot3D > 0.60f) || (horizDot > 0.60f && rayYAtTarget >= obj.Position.y - 0.5f && rayYAtTarget <= obj.Position.y + 2.5f);

                        if (hitDirection) {
                            bestDist = dist3D;
                            player.CanInteract = true;
                            player.InteractPrompt = "[L] Open Treasure Chest (CLEAR)";
                            player.InteractKind = LuminousInteractKind::OpenChest;
                            player.TargetInstanceId = obj.InstanceId;
                            player.TargetIsGoal = true;
                        }
                    }
                }
            }
        }

    private:
        // ================================================================
        // 壁との円柱コリジョンスライド
        // ================================================================
        static XMFLOAT3 CollideWithWalls(
            const XMFLOAT3& currentPos,
            const XMFLOAT3& desiredPos,
            float radius,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionGeometry>& occlusionWalls,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            bool isHoldingOrb,
            XMFLOAT3& inoutVelocity)
        {
            XMFLOAT3 pos = desiredPos;

            for (const auto& obj : objects) {
                // 階段 (Stairs_Straight) の側面・背面コリジョン (登り口以外からの進入を壁として遮断)
                if (obj.Category == AssetCategory::Stairs || obj.AssetId == "Stairs_Straight") {
                    float rad = XMConvertToRadians(obj.Rotation.y);
                    float cosR = std::cos(rad);
                    float sinR = std::sin(rad);

                    float dx = pos.x - obj.Position.x;
                    float dz = pos.z - obj.Position.z;

                    float localX = dx * cosR - dz * sinR;
                    float localZ = dx * sinR + dz * cosR;

                    constexpr float halfWidth = 1.053f;
                    constexpr float halfLength = 2.106f;

                    // 階段の斜面高さ (localZ = -halfLength が登り口、+halfLength が登りきり)
                    float t = std::clamp((localZ + halfLength) / (2.0f * halfLength), 0.0f, 1.0f);
                    float stairY = obj.Position.y + t * GRID_FLOOR_HEIGHT;

                    // プレイヤーの足元が斜面高さより下にある場合、階段の側面・背面は剛体壁として遮蔽
                    if (pos.y < stairY - 0.25f && pos.y >= obj.Position.y - 0.2f) {
                        // 側面壁 (localX = ±halfWidth)
                        if (localZ >= -halfLength && localZ <= halfLength) {
                            if (localX > halfWidth - radius && localX < halfWidth + radius) {
                                float pushOut = (halfWidth + radius) - localX;
                                pos.x += pushOut * cosR;
                                pos.z += -pushOut * sinR;
                            } else if (localX < -halfWidth + radius && localX > -halfWidth - radius) {
                                float pushOut = (-halfWidth - radius) - localX;
                                pos.x += pushOut * cosR;
                                pos.z += -pushOut * sinR;
                            }
                        }

                        // 背面壁 (localZ = +halfLength: 登りきり背面の高い壁)
                        if (std::abs(localX) <= halfWidth) {
                            if (localZ > halfLength - radius && localZ < halfLength + radius) {
                                float pushOut = (halfLength + radius) - localZ;
                                pos.x += pushOut * sinR;
                                pos.z += pushOut * cosR;
                            }
                        }
                    }
                    continue;
                }

                // 台座 (Pedestal) コリジョン (円柱 R=0.45m)
                if (obj.Category == AssetCategory::Pedestal || obj.AssetId == "Pedestal") {
                    if (pos.y >= obj.Position.y - 0.2f && pos.y <= obj.Position.y + 1.25f) {
                        float dx = pos.x - obj.Position.x;
                        float dz = pos.z - obj.Position.z;
                        float dSq = dx * dx + dz * dz;
                        float minD = radius + 0.45f;
                        if (dSq < minD * minD && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = minD - d;
                            float nx = dx / d;
                            float nz = dz / d;
                            pos.x += nx * ov;
                            pos.z += nz * ov;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    }
                    continue;
                }

                // 宝箱 (GoalChest) コリジョン (回転OBB: 幅1.35m x 奥行0.95m x 高さ0.9m)
                if (obj.AssetId == "GoalChest") {
                    if (pos.y >= obj.Position.y - 0.2f && pos.y <= obj.Position.y + 1.10f) {
                        float rad = XMConvertToRadians(obj.Rotation.y);
                        float cosR = std::cos(rad);
                        float sinR = std::sin(rad);

                        float dx = pos.x - obj.Position.x;
                        float dz = pos.z - obj.Position.z;

                        float localX = dx * cosR - dz * sinR;
                        float localZ = dx * sinR + dz * cosR;

                        constexpr float hw = 0.68f;
                        constexpr float hd = 0.48f;

                        float cx = std::clamp(localX, -hw, hw);
                        float cz = std::clamp(localZ, -hd, hd);
                        float ldx = localX - cx;
                        float ldz = localZ - cz;
                        float dSq = ldx * ldx + ldz * ldz;
                        if (dSq < radius * radius && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = radius - d;
                            float nlx = ldx / d;
                            float nlz = ldz / d;
                            float pushX = nlx * ov;
                            float pushZ = nlz * ov;
                            pos.x += pushX * cosR + pushZ * sinR;
                            pos.z += -pushX * sinR + pushZ * cosR;
                            float nx = nlx * cosR + nlz * sinR;
                            float nz = -nlx * sinR + nlz * cosR;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    }
                    continue;
                }

                // その他 Special コリジョン (※StartDaisは歩行通過可能な台座のため除外)
                if (obj.Category == AssetCategory::Special && obj.AssetId != "StartDais") {
                    if (pos.y >= obj.Position.y - 0.2f && pos.y <= obj.Position.y + 1.10f) {
                        float hw = 0.55f;
                        float hd = 0.45f;
                        float minX = obj.Position.x - hw;
                        float maxX = obj.Position.x + hw;
                        float minZ = obj.Position.z - hd;
                        float maxZ = obj.Position.z + hd;
                        float cx = std::clamp(pos.x, minX, maxX);
                        float cz = std::clamp(pos.z, minZ, maxZ);
                        float dx = pos.x - cx;
                        float dz = pos.z - cz;
                        float dSq = dx * dx + dz * dz;
                        if (dSq < radius * radius && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = radius - d;
                            float nx = dx / d;
                            float nz = dz / d;
                            pos.x += nx * ov;
                            pos.z += nz * ov;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    }
                    continue;
                }

                // その他 Prop コリジョン (円柱 R=0.40m)
                if (obj.Category == AssetCategory::Prop) {
                    if (pos.y >= obj.Position.y - 0.2f && pos.y <= obj.Position.y + 1.20f) {
                        float dx = pos.x - obj.Position.x;
                        float dz = pos.z - obj.Position.z;
                        float dSq = dx * dx + dz * dz;
                        float minD = radius + 0.40f;
                        if (dSq < minD * minD && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = minD - d;
                            float nx = dx / d;
                            float nz = dz / d;
                            pos.x += nx * ov;
                            pos.z += nz * ov;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    }
                    continue;
                }

                if (obj.Category != AssetCategory::Wall) continue;

                // フロア判定 (Y: 上層床より25cm下までに制限し、2Fプレイヤーへのコリジョン漏れを防止)
                float floorY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                if (pos.y < floorY - 0.2f || pos.y >= floorY + GRID_FLOOR_HEIGHT - 0.25f) continue;

                // 反転壁の場合の球状くり抜き判定
                if (obj.Phase == MaterialPhase::Phase) {
                    // 手持ち宝玉: 事前ベイクが無いためその場で判定し、描画と完全に一致させる。
                    // 消えて見える壁だけを通り抜けられ、遮蔽体の影で残って見える壁は剛体のまま。
                    bool openedByCarried = false;
                    for (const auto& orb : orbs) {
                        // 台座に据わっている宝玉のみ事前ベイクを使う。
                        // 脱着アニメーション中は動いているためこちらのレイキャストで扱う。
                        if (orb.Carrier == OrbCarrier::Pedestal && !orb.IsAnimating) continue;
                        if (TerrainSystem::IsPhaseWallOpenAt(occlusionWalls, orb, obj, pos.x, pos.z)) {
                            openedByCarried = true;
                            break;
                        }
                    }
                    if (openedByCarried) goto next_wall;

                    // 台座に宝玉がセットされているか探索
                    for (const auto& orb : orbs) {
                        if (orb.Carrier != OrbCarrier::Pedestal || orb.IsAnimating) continue;

                        for (const auto& bake : bakedPedestals) {
                            if (bake.PedestalInstanceId != orb.PedestalInstanceId) continue;
                            for (const auto& wBake : bake.Walls) {
                                if (wBake.WallInstanceId != obj.InstanceId) continue;

                                if (wBake.IsFullyOpen) {
                                    // 完全にくり抜かれて開口しているため通行自由
                                    goto next_wall;
                                }
                                if (wBake.IsFullySolid) {
                                    break; // 通常剛体壁として判定
                                }

                                // 左右の残存ウィングとのみ衝突判定
                                auto collideAABB = [&](const XMFLOAT3& minB, const XMFLOAT3& maxB) {
                                    float cx = std::clamp(pos.x, minB.x, maxB.x);
                                    float cz = std::clamp(pos.z, minB.z, maxB.z);
                                    float dx = pos.x - cx;
                                    float dz = pos.z - cz;
                                    float dSq = dx * dx + dz * dz;
                                    if (dSq < radius * radius) {
                                        float d = std::sqrt(dSq);
                                        if (d > 1e-4f) {
                                            float ov = radius - d;
                                            float nx = dx / d;
                                            float nz = dz / d;
                                            pos.x += nx * ov;
                                            pos.z += nz * ov;
                                            // 接線速度投影（反発ゼロ・壁滑走）
                                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                                            if (vDotN < 0.0f) {
                                                inoutVelocity.x -= vDotN * nx;
                                                inoutVelocity.z -= vDotN * nz;
                                            }
                                        }
                                    }
                                };

                                // 遮蔽体の影で開口が分断される場合もあるため全区間を判定
                                for (int32_t sp = 0; sp < wBake.SolidSpanCount; ++sp) {
                                    collideAABB(wBake.SolidMin[sp], wBake.SolidMax[sp]);
                                }
                                goto next_wall;
                            }
                        }
                    }
                }

                // 通路用アーチ門 (Wall_DoorArc): 左右の柱のみ剛体判定し、中央2m開口部は通行可能
                if (obj.AssetId == "Wall_DoorArc") {
                    float rad = XMConvertToRadians(obj.Rotation.y);
                    float cosR = std::cos(rad);
                    float sinR = std::sin(rad);

                    float dx = pos.x - obj.Position.x;
                    float dz = pos.z - obj.Position.z;

                    float localX = dx * cosR - dz * sinR;
                    float localZ = dx * sinR + dz * cosR;

                    auto collideLocalBox = [&](float minLX, float maxLX, float minLZ, float maxLZ) {
                        float cx = std::clamp(localX, minLX, maxLX);
                        float cz = std::clamp(localZ, minLZ, maxLZ);
                        float ldx = localX - cx;
                        float ldz = localZ - cz;
                        float dSq = ldx * ldx + ldz * ldz;
                        if (dSq < radius * radius && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = radius - d;
                            float nlx = ldx / d;
                            float nlz = ldz / d;
                            float pushX = nlx * ov;
                            float pushZ = nlz * ov;
                            pos.x += pushX * cosR + pushZ * sinR;
                            pos.z += -pushX * sinR + pushZ * cosR;
                            float nx = nlx * cosR + nlz * sinR;
                            float nz = -nlx * sinR + nlz * cosR;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    };

                    // 左柱 (localX: [-1.66, -1.00]) & 右柱 (localX: [1.00, 1.66])、中央開口部はノーコリジョン
                    collideLocalBox(-1.66f, -1.00f, -0.37f, 0.37f);
                    collideLocalBox(1.00f, 1.66f, -0.37f, 0.37f);
                    goto next_wall;
                }

                // 崩落大壁 (Wall_Ruined): 3マス長 (6.32m)。左右の立壁ウィングのみ剛体判定し、中央の崩壊開口部(幅約1.33m)は通行可能
                if (obj.AssetId == "Wall_Ruined") {
                    float rad = XMConvertToRadians(obj.Rotation.y);
                    float cosR = std::cos(rad);
                    float sinR = std::sin(rad);

                    float dx = pos.x - obj.Position.x;
                    float dz = pos.z - obj.Position.z;

                    float localX = dx * cosR - dz * sinR;
                    float localZ = dx * sinR + dz * cosR;

                    auto collideWing = [&](float minLX, float maxLX, float minLZ, float maxLZ) {
                        float cx = std::clamp(localX, minLX, maxLX);
                        float cz = std::clamp(localZ, minLZ, maxLZ);
                        float ldx = localX - cx;
                        float ldz = localZ - cz;
                        float dSq = ldx * ldx + ldz * ldz;
                        if (dSq < radius * radius && dSq > 1e-6f) {
                            float d = std::sqrt(dSq);
                            float ov = radius - d;
                            float nlx = ldx / d;
                            float nlz = ldz / d;
                            float pushX = nlx * ov;
                            float pushZ = nlz * ov;
                            pos.x += pushX * cosR + pushZ * sinR;
                            pos.z += -pushX * sinR + pushZ * cosR;
                            float nx = nlx * cosR + nlz * sinR;
                            float nz = -nlx * sinR + nlz * cosR;
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    };

                    // 左立壁ウィング: localX in [-3.16, -0.95], 右立壁ウィング: localX in [0.38, 3.16] (中央開口部は通行可能)
                    collideWing(-3.16f, -0.95f, -0.25f, 0.25f);
                    collideWing(0.38f, 3.16f, -0.25f, 0.25f);
                    goto next_wall;
                }

                // 通常壁・暗がり反転壁・格子壁のAABBコリジョン
                {
                    float halfLen = GRID_CELL_SIZE * 0.5f;
                    float halfThick = 0.16f;
                    bool isRotated = (std::abs(std::fmod(obj.Rotation.y, 180.0f)) > 45.0f);

                    float minX = obj.Position.x - (isRotated ? halfThick : halfLen);
                    float maxX = obj.Position.x + (isRotated ? halfThick : halfLen);
                    float minZ = obj.Position.z - (isRotated ? halfLen : halfThick);
                    float maxZ = obj.Position.z + (isRotated ? halfLen : halfThick);

                    float closestX = std::clamp(pos.x, minX, maxX);
                    float closestZ = std::clamp(pos.z, minZ, maxZ);
                    float diffX = pos.x - closestX;
                    float diffZ = pos.z - closestZ;
                    float distSq = diffX * diffX + diffZ * diffZ;

                    if (distSq < radius * radius) {
                        float dist = std::sqrt(distSq);
                        if (dist > 1e-5f) {
                            float overlap = radius - dist;
                            float nx = diffX / dist;
                            float nz = diffZ / dist;
                            pos.x += nx * overlap;
                            pos.z += nz * overlap;
                            // 接線速度投影（反発ゼロ・壁滑走）
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        } else {
                            float dx = currentPos.x - obj.Position.x;
                            float dz = currentPos.z - obj.Position.z;
                            float nx = 0.0f;
                            float nz = 0.0f;
                            if (std::abs(dx) > std::abs(dz)) {
                                nx = (dx > 0) ? 1.0f : -1.0f;
                                pos.x = obj.Position.x + nx * (halfLen + radius);
                            } else {
                                nz = (dz > 0) ? 1.0f : -1.0f;
                                pos.z = obj.Position.z + nz * (halfLen + radius);
                            }
                            float vDotN = inoutVelocity.x * nx + inoutVelocity.z * nz;
                            if (vDotN < 0.0f) {
                                inoutVelocity.x -= vDotN * nx;
                                inoutVelocity.z -= vDotN * nz;
                            }
                        }
                    }
                }

            next_wall:;
            }

            return pos;
        }
    };
}
