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

export module App.Luminous.TerrainSystem;

import App.Luminous.Types;
import App.Luminous.Optics;

export namespace App::Luminous {

    using namespace DirectX;

    // 前方宣言
    struct LuminousPlayerComponent;

    // ====================================================================
    // 地形反転＆コリジョン制御システム (Phase 5 球状くり抜き・安全機構刷新)
    // ====================================================================
    // 反転壁の開口スキャン解像度 (1 セル 2.106m を 32 分割 = 約 6.6cm)
    constexpr int WALL_SCAN_SLICES = 32;

    // 反転壁の通行判定でサンプリングする高さ (その階の床面からのオフセット, m)。
    // 足元・腰・頭のすべてで光が届いている区間だけを通れる開口とし、
    // 「壁の下部が残って見えるのに通り抜けられる」状態を防ぐ。
    constexpr float PHASE_WALL_SAMPLE_HEIGHTS[] = { 0.2f, 1.0f, 1.8f };

    class TerrainSystem {
    public:
        // ================================================================
        // 1. 全台座の球状くり抜きコリジョン事前計算 (ステージロード時 O(1) ベイク)
        // ================================================================
        static std::vector<PedestalColliderBake> BakePedestalColliders(
            const std::vector<PlacedObject>& objects)
        {
            std::vector<PedestalColliderBake> bakes;

            // Phase 2: 遮蔽ジオメトリと空間分割グリッドはステージ内で共通。
            // ロード時に一度だけ構築し、以降のレイキャストは DDA で O(1) 相当。
            const std::vector<OcclusionGeometry> occluders = OpticsEngine::BuildOcclusionGeometry(objects);
            SpatialGrid grid;
            grid.Build(occluders);

            for (const auto& ped : objects) {
                if (ped.Category != AssetCategory::Pedestal) continue;

                PedestalColliderBake bake;
                bake.PedestalInstanceId = ped.InstanceId;

                XMFLOAT3 orbPos(ped.Position.x, ped.Position.y + PEDESTAL_SOCKET_HEIGHT, ped.Position.z);
                float R = DEFAULT_ORB_RADIUS;

                OrbRuntimeState probeOrb;
                probeOrb.WorldPosition = orbPos;
                probeOrb.Radius = R;

                // この台座に宝玉を挿したとき、点 p に光が届くか (通常壁のみが遮蔽)
                auto isLit = [&](const XMFLOAT3& p) {
                    return OpticsEngine::EvaluateLight(p, probeOrb, occluders, &grid);
                    };

                const float halfCell = GRID_CELL_SIZE * 0.5f;

                // ------------------------------------------------------------
                // 1-1. 反転床: 球状くり抜き円 ∩ 遮蔽体の影マスク
                //      セルを 16x16 サブセルに分割し、円内かつ光が届くサブセルだけを
                //      「穴」としてビットマスクへ焼き込む。壁の影になっている部分は
                //      影の形そのままに足場として残る。
                // ------------------------------------------------------------
                for (const auto& obj : objects) {
                    if (obj.Category != AssetCategory::Floor || obj.Phase != MaterialPhase::Phase) continue;

                    float floorY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                    float deltaY = std::abs(orbPos.y - floorY);
                    if (deltaY >= R) continue;

                    float r_hole = std::sqrt(R * R - deltaY * deltaY);
                    float dx = obj.Position.x - orbPos.x;
                    float dz = obj.Position.z - orbPos.z;
                    float dHoriz = std::sqrt(dx * dx + dz * dz);

                    if (dHoriz - halfCell >= r_hole) continue; // このセルに穴は開かない

                    CircularHole hole;
                    hole.CenterXZ = XMFLOAT2(orbPos.x, orbPos.z);
                    hole.Radius = r_hole;
                    hole.FloorIndex = obj.FloorIndex;
                    hole.FloorInstanceId = obj.InstanceId;
                    hole.CellMinXZ = XMFLOAT2(obj.Position.x - halfCell, obj.Position.z - halfCell);
                    hole.CellSize = GRID_CELL_SIZE;
                    hole.HasMask = true;

                    const float sub = GRID_CELL_SIZE / static_cast<float>(PHASE_MASK_DIM);
                    for (int iz = 0; iz < PHASE_MASK_DIM; ++iz) {
                        for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                            float sx = hole.CellMinXZ.x + (static_cast<float>(ix) + 0.5f) * sub;
                            float sz = hole.CellMinXZ.y + (static_cast<float>(iz) + 0.5f) * sub;

                            float ox = sx - orbPos.x;
                            float oz = sz - orbPos.z;
                            if (ox * ox + oz * oz >= r_hole * r_hole) continue; // 円の外は元々穴でない

                            // 床上面の直上でサンプリング (床スラブによる自己遮蔽を避ける)
                            if (isLit(XMFLOAT3(sx, floorY + 0.05f, sz))) {
                                hole.SetLit(ix, iz);
                            }
                        }
                    }

                    bake.Holes.push_back(hole);
                }

                // ------------------------------------------------------------
                // 1-2. 反転壁: 壁の長辺を等分してスキャンし、
                //      「球内 かつ 光が届く」区間だけを開口、それ以外を剛体区間として保存。
                //      遮蔽体の影で開口が分断される場合も正しく複数区間になる。
                // ------------------------------------------------------------
                for (const auto& obj : objects) {
                    if (obj.Category != AssetCategory::Wall || obj.Phase != MaterialPhase::Phase) continue;

                    WallCutoutCollider wCutout;
                    wCutout.WallInstanceId = obj.InstanceId;
                    wCutout.FloorIndex = obj.FloorIndex;

                    const float halfLen = halfCell;
                    const float halfThick = 0.16f;
                    const bool isRot = (std::abs(std::fmod(obj.Rotation.y, 180.0f)) > 45.0f);
                    const float floorY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                    const float topY = floorY + GRID_FLOOR_HEIGHT;

                    // 壁面までの垂直距離が光半径を超えていれば一切くり抜かれない
                    const float dPlane = isRot ? std::abs(orbPos.x - obj.Position.x)
                                               : std::abs(orbPos.z - obj.Position.z);
                    if (dPlane >= R) {
                        wCutout.IsFullySolid = true;
                        bake.Walls.push_back(wCutout);
                        continue;
                    }

                    const float r_cutout = std::sqrt(R * R - dPlane * dPlane);

                    // 長辺 (isRot なら Z 軸、そうでなければ X 軸) 方向のスキャン
                    const float axisCenter = isRot ? obj.Position.z : obj.Position.x;
                    const float axisMin = axisCenter - halfLen;
                    const float orbAxis = isRot ? orbPos.z : orbPos.x;
                    const float sliceW = GRID_CELL_SIZE / static_cast<float>(WALL_SCAN_SLICES);

                    // プレイヤーの体が通る高さ (足元〜頭) の全サンプルで光が届く区間だけを開口とする。
                    // 別の階から照らした場合など、壁の下部だけ光が届かず残って見える区間は通れない。
                    bool openSlice[WALL_SCAN_SLICES] = {};
                    int openCount = 0;
                    for (int i = 0; i < WALL_SCAN_SLICES; ++i) {
                        const float a = axisMin + (static_cast<float>(i) + 0.5f) * sliceW;
                        if (std::abs(a - orbAxis) >= r_cutout) continue;

                        bool litAllHeights = true;
                        for (float h : PHASE_WALL_SAMPLE_HEIGHTS) {
                            const float sampleY = (std::min)(floorY + h, topY - 0.10f);
                            const XMFLOAT3 sample = isRot ? XMFLOAT3(obj.Position.x, sampleY, a)
                                                          : XMFLOAT3(a, sampleY, obj.Position.z);
                            const float sdx = sample.x - orbPos.x;
                            const float sdy = sample.y - orbPos.y;
                            const float sdz = sample.z - orbPos.z;
                            if (sdx * sdx + sdy * sdy + sdz * sdz >= R * R || !isLit(sample)) {
                                litAllHeights = false; // 球の外 or 遮蔽体の影 -> 壁は残る
                                break;
                            }
                        }
                        if (!litAllHeights) continue;

                        openSlice[i] = true;
                        ++openCount;
                    }

                    if (openCount == 0) {
                        wCutout.IsFullySolid = true;
                        bake.Walls.push_back(wCutout);
                        continue;
                    }
                    if (openCount == WALL_SCAN_SLICES) {
                        wCutout.IsFullyOpen = true;
                        bake.Walls.push_back(wCutout);
                        continue;
                    }

                    // 閉じたスライスの連続区間を AABB へまとめる
                    int runStart = -1;
                    for (int i = 0; i <= WALL_SCAN_SLICES; ++i) {
                        const bool solid = (i < WALL_SCAN_SLICES) && !openSlice[i];
                        if (solid && runStart < 0) {
                            runStart = i;
                        } else if (!solid && runStart >= 0) {
                            const float a0 = axisMin + static_cast<float>(runStart) * sliceW;
                            const float a1 = axisMin + static_cast<float>(i) * sliceW;
                            if (isRot) {
                                wCutout.AddSolidSpan(
                                    XMFLOAT3(obj.Position.x - halfThick, floorY, a0),
                                    XMFLOAT3(obj.Position.x + halfThick, topY, a1));
                            } else {
                                wCutout.AddSolidSpan(
                                    XMFLOAT3(a0, floorY, obj.Position.z - halfThick),
                                    XMFLOAT3(a1, topY, obj.Position.z + halfThick));
                            }
                            runStart = -1;
                        }
                    }

                    bake.Walls.push_back(wCutout);
                }

                bakes.push_back(bake);
            }

            return bakes;
        }

        // ================================================================
        // Phase 2: 遮蔽ベイクの単体テスト
        // 「基本は球状にくり抜き、遮蔽されている部分は影の形のままくり抜かない」
        // という仕様が事前計算に正しく反映されているかを検証する。
        // ================================================================
        static inline int baselineLit_ = 0;

        static bool RunOcclusionBakeUnitTests(std::string& outErrorMsg)
        {
            auto makeBase = []() {
                std::vector<PlacedObject> objs;

                PlacedObject ped;
                ped.InstanceId = 1;
                ped.AssetId = "Pedestal";
                ped.Category = AssetCategory::Pedestal;
                ped.FloorIndex = 0;
                ped.Position = GridToWorldCenter(1, 1, 0);
                objs.push_back(ped);

                PlacedObject floorCell;
                floorCell.InstanceId = 2;
                floorCell.AssetId = "Floor_1x1";
                floorCell.Category = AssetCategory::Floor;
                floorCell.FloorIndex = 0;
                floorCell.Cell = GridCoord{ 2, 2, 0 };
                floorCell.Position = GridToWorldCenter(2, 2, 0);
                floorCell.Phase = MaterialPhase::Phase;
                objs.push_back(floorCell);

                return objs;
                };

            // 遮蔽体となる通常壁: セル (2,2) の West 辺 (X = 4.212, Z が長辺)
            auto makeBlocker = [](const std::string& assetId) {
                PlacedObject w;
                w.InstanceId = 3;
                w.AssetId = assetId;
                w.Category = AssetCategory::Wall;
                w.FloorIndex = 0;
                w.Position = XMFLOAT3(2.0f * GRID_CELL_SIZE, 0.0f, GridToWorldCenter(2, 2, 0).z);
                w.Rotation.y = 90.0f; // Z 方向に伸びる壁
                return w;
                };

            auto findHole = [](const std::vector<PedestalColliderBake>& bakes) -> const CircularHole* {
                for (const auto& b : bakes) {
                    for (const auto& h : b.Holes) {
                        if (h.FloorInstanceId == 2) return &h;
                    }
                }
                return nullptr;
                };

            auto countLit = [](const CircularHole& h) {
                int lit = 0;
                const float sub = h.CellSize / static_cast<float>(PHASE_MASK_DIM);
                for (int iz = 0; iz < PHASE_MASK_DIM; ++iz) {
                    for (int ix = 0; ix < PHASE_MASK_DIM; ++ix) {
                        const float x = h.CellMinXZ.x + (static_cast<float>(ix) + 0.5f) * sub;
                        const float z = h.CellMinXZ.y + (static_cast<float>(iz) + 0.5f) * sub;
                        const float dx = x - h.CenterXZ.x;
                        const float dz = z - h.CenterXZ.y;
                        if (dx * dx + dz * dz >= h.Radius * h.Radius) continue;
                        if (h.IsLitAt(x, z)) ++lit;
                    }
                }
                return lit;
                };

            // --- テスト 1: 遮蔽体なし -> 円内のサブセルは全て光が届き穴になる
            {
                auto objs = makeBase();
                auto bakes = BakePedestalColliders(objs);
                const CircularHole* h = findHole(bakes);
                if (!h) { outErrorMsg = "BakeTest 1 Failed: phase floor hole was not baked."; return false; }
                if (!h->HasMask) { outErrorMsg = "BakeTest 1 Failed: occlusion mask missing."; return false; }
                const int lit = countLit(*h);
                if (lit < 100) { outErrorMsg = "BakeTest 1 Failed: unoccluded cell should be almost fully carved."; return false; }
                baselineLit_ = lit;
            }

            // --- テスト 2: 通常壁を挟む -> 影のサブセルはくり抜かれない
            {
                auto objs = makeBase();
                objs.push_back(makeBlocker("Wall_Brick"));
                auto bakes = BakePedestalColliders(objs);
                const CircularHole* h = findHole(bakes);
                if (!h) { outErrorMsg = "BakeTest 2 Failed: phase floor hole was not baked."; return false; }
                const int lit = countLit(*h);
                if (lit >= baselineLit_) {
                    outErrorMsg = "BakeTest 2 Failed: a normal wall must shadow part of the phase floor.";
                    return false;
                }
                if (lit == 0) {
                    outErrorMsg = "BakeTest 2 Failed: the wall must not shadow the whole cell.";
                    return false;
                }
            }

            // --- テスト 3: 格子壁は光を透過するので影を落とさない
            {
                auto objs = makeBase();
                objs.push_back(makeBlocker("Wall_Grate"));
                auto bakes = BakePedestalColliders(objs);
                const CircularHole* h = findHole(bakes);
                if (!h) { outErrorMsg = "BakeTest 3 Failed: phase floor hole was not baked."; return false; }
                if (countLit(*h) != baselineLit_) {
                    outErrorMsg = "BakeTest 3 Failed: a grate must NOT shadow the phase floor.";
                    return false;
                }
            }

            // --- テスト 4: 反転壁の開口も遮蔽で分断される
            {
                auto objs = makeBase();
                PlacedObject pw;
                pw.InstanceId = 4;
                pw.AssetId = "Wall_Brick";
                pw.Category = AssetCategory::Wall;
                pw.FloorIndex = 0;
                pw.Cell = GridCoord{ 2, 3, 0 };
                pw.Position = XMFLOAT3(GridToWorldCenter(2, 3, 0).x, 0.0f, 3.0f * GRID_CELL_SIZE);
                pw.Rotation.y = 0.0f; // X 方向に伸びる反転壁
                pw.Phase = MaterialPhase::Phase;
                objs.push_back(pw);

                auto solidLength = [](const WallCutoutCollider& w) {
                    if (w.IsFullySolid) return GRID_CELL_SIZE;
                    if (w.IsFullyOpen) return 0.0f;
                    float len = 0.0f;
                    for (int32_t k = 0; k < w.SolidSpanCount; ++k) {
                        const float lx = std::abs(w.SolidMax[k].x - w.SolidMin[k].x);
                        const float lz = std::abs(w.SolidMax[k].z - w.SolidMin[k].z);
                        len += (lx > lz) ? lx : lz;
                    }
                    return len;
                    };
                auto wallSolidLength = [&](const std::vector<PedestalColliderBake>& bakes, float& out) {
                    for (const auto& b : bakes) {
                        for (const auto& w : b.Walls) {
                            if (w.WallInstanceId != 4) continue;
                            out = solidLength(w);
                            return true;
                        }
                    }
                    return false;
                    };

                float openLen = 0.0f;
                if (!wallSolidLength(BakePedestalColliders(objs), openLen)) {
                    outErrorMsg = "BakeTest 4 Failed: phase wall cutout was not baked.";
                    return false;
                }
                if (openLen >= GRID_CELL_SIZE - 0.01f) {
                    outErrorMsg = "BakeTest 4 Failed: without an occluder the phase wall must be partly carved.";
                    return false;
                }

                objs.push_back(makeBlocker("Wall_Brick"));
                float blockedLen = 0.0f;
                if (!wallSolidLength(BakePedestalColliders(objs), blockedLen)) {
                    outErrorMsg = "BakeTest 4 Failed: phase wall cutout missing with blocker.";
                    return false;
                }
                if (blockedLen <= openLen + 0.01f) {
                    outErrorMsg = "BakeTest 4 Failed: an occluder must leave more of the phase wall solid.";
                    return false;
                }
            }

            // --- テスト 4b: 上の階の台座から照らした 1F の反転壁。
            //     壁の上部しか光球に入らず下部が残って見えるので、通れる開口を作ってはならない。
            {
                std::vector<PlacedObject> objs;
                PlacedObject upperPed;
                upperPed.InstanceId = 11;
                upperPed.AssetId = "Pedestal";
                upperPed.Category = AssetCategory::Pedestal;
                upperPed.FloorIndex = 1;
                // 壁面 (z = 3 セル) から水平に 1.2 セル (約 2.5m) 離れた 2F の台座
                upperPed.Position = XMFLOAT3(GridToWorldCenter(2, 4, 1).x, GRID_FLOOR_HEIGHT, 4.2f * GRID_CELL_SIZE);
                objs.push_back(upperPed);

                PlacedObject lowerWall;
                lowerWall.InstanceId = 12;
                lowerWall.AssetId = "Wall_Brick";
                lowerWall.Category = AssetCategory::Wall;
                lowerWall.FloorIndex = 0;
                lowerWall.Cell = GridCoord{ 2, 3, 0 };
                lowerWall.Position = XMFLOAT3(GridToWorldCenter(2, 3, 0).x, 0.0f, 3.0f * GRID_CELL_SIZE);
                lowerWall.Rotation.y = 0.0f;
                lowerWall.Phase = MaterialPhase::Phase;
                objs.push_back(lowerWall);

                bool found = false;
                for (const auto& b : BakePedestalColliders(objs)) {
                    for (const auto& w : b.Walls) {
                        if (w.WallInstanceId != 12) continue;
                        found = true;
                        if (!w.IsFullySolid) {
                            outErrorMsg = "BakeTest 4b Failed: a phase wall lit only near its top from the floor above must stay solid.";
                            return false;
                        }
                    }
                }
                if (!found) {
                    outErrorMsg = "BakeTest 4b Failed: lower-floor phase wall was not baked.";
                    return false;
                }
            }

            // --- テスト 5: 影のサブセル上ではプレイヤーが押し戻されない (床が残っている)
            //     逆に光が届いているサブセルでは穴として押し戻される
            {
                auto objs = makeBase();
                objs.push_back(makeBlocker("Wall_Brick"));
                auto bakes = BakePedestalColliders(objs);
                const auto occluders = OpticsEngine::BuildOcclusionGeometry(objs);

                const CircularHole* h = findHole(bakes);
                if (!h) { outErrorMsg = "BakeTest 5 Failed: phase floor hole was not baked."; return false; }

                OrbRuntimeState orb;
                orb.Carrier = OrbCarrier::Pedestal;
                orb.PedestalInstanceId = 1;
                orb.WorldPosition = XMFLOAT3(GridToWorldCenter(1, 1, 0).x,
                                             PEDESTAL_SOCKET_HEIGHT,
                                             GridToWorldCenter(1, 1, 0).z);
                orb.Radius = DEFAULT_ORB_RADIUS;
                std::vector<OrbRuntimeState> orbs{ orb };

                // 遮蔽壁 (x = 4.212, z in [4.212, 6.318]) の影になる点と、光が届く点
                const XMFLOAT3 shadowPt(4.60f, 0.0f, 5.80f);
                const XMFLOAT3 litPt(6.00f, 0.0f, 4.50f);

                if (h->IsLitAt(shadowPt.x, shadowPt.z)) {
                    outErrorMsg = "BakeTest 5 Failed: a point behind the wall must not be lit.";
                    return false;
                }
                if (!h->IsLitAt(litPt.x, litPt.z)) {
                    outErrorMsg = "BakeTest 5 Failed: a point beside the wall must be lit.";
                    return false;
                }

                // 影の上へは素通しで進めること
                XMFLOAT3 fromShadow(shadowPt.x, 0.0f, shadowPt.z - 0.40f);
                XMFLOAT3 outShadow = ConstrainMovementAgainstHoles(
                    fromShadow, shadowPt, objs, orbs, occluders, bakes, false, nullptr);
                const float dShadow = std::abs(outShadow.x - shadowPt.x) + std::abs(outShadow.z - shadowPt.z);
                if (dShadow > 1e-3f) {
                    outErrorMsg = "BakeTest 5 Failed: the player must be able to stand on the shadowed floor.";
                    return false;
                }

                // 光が届いている穴の上へは進めないこと
                XMFLOAT3 fromLit(litPt.x, 0.0f, litPt.z - 0.40f);
                XMFLOAT3 outLit = ConstrainMovementAgainstHoles(
                    fromLit, litPt, objs, orbs, occluders, bakes, false, nullptr);
                const float dLit = std::abs(outLit.x - litPt.x) + std::abs(outLit.z - litPt.z);
                if (dLit <= 1e-3f) {
                    outErrorMsg = "BakeTest 5 Failed: the lit part of the phase floor must still block the player.";
                    return false;
                }
            }

            return true;
        }

        // ================================================================
        // Phase 2: 手持ち宝玉用 — (x, z) 付近で反転壁が実際にくり抜かれているか
        // 事前ベイクの無い手持ち/移動中の宝玉に対し、その場で 1 本レイを飛ばして
        // 描画側 (OrbOcclusionPass) と同じ判定を行う。
        // 「消えて見える壁は通れる / 影で残って見える壁は通れない」を保証する。
        // ================================================================
        // その宝玉をプレイヤーが所持しているか (取得アニメーション中も所持側)
        static bool IsOrbHeldByPlayer(const OrbRuntimeState& orb) {
            return orb.IsAnimating ? (orb.TargetCarrier == OrbCarrier::Player)
                                   : (orb.Carrier == OrbCarrier::Player);
        }

        static bool IsPhaseWallOpenAt(
            const std::vector<OcclusionGeometry>& occluders,
            const OrbRuntimeState& orb,
            const PlacedObject& wall,
            float x, float z)
        {
            const float halfLen = GRID_CELL_SIZE * 0.5f;
            const float floorY = static_cast<float>(wall.FloorIndex) * GRID_FLOOR_HEIGHT;
            const float topY = floorY + GRID_FLOOR_HEIGHT;
            const bool isRot = (std::abs(std::fmod(wall.Rotation.y, 180.0f)) > 45.0f);

            // プレイヤーに最も近い壁中心線上の点
            const float axisCenter = isRot ? wall.Position.z : wall.Position.x;
            const float axisPos = std::clamp(isRot ? z : x, axisCenter - halfLen, axisCenter + halfLen);

            // 足元〜頭の全高さが球の内側にある場合だけ通れる (壁の一部が残って見える所は通さない)
            XMFLOAT3 p{};
            for (float h : PHASE_WALL_SAMPLE_HEIGHTS) {
                const float sampleY = (std::min)(floorY + h, topY - 0.10f);
                p = isRot ? XMFLOAT3(wall.Position.x, sampleY, axisPos)
                          : XMFLOAT3(axisPos, sampleY, wall.Position.z);
                const float dx = p.x - orb.WorldPosition.x;
                const float dy = p.y - orb.WorldPosition.y;
                const float dz = p.z - orb.WorldPosition.z;
                if (dx * dx + dy * dy + dz * dz >= orb.Radius * orb.Radius) return false; // 球の外は元から壁
            }

            // 宝玉を所持している間は、当たり判定側では遮蔽 (影) を一切考慮せず
            // 光球内の反転壁をすべて素通しとして扱う。
            //
            // 理由は 2 つ。
            //  - 最適化: 毎フレームのレイキャストが不要になる。
            //  - 不正防止: 宝玉を壁際に寄せて意図しない影を作り、その影で
            //    残った反転マテリアルを足場や壁として使うことを封じる。
            // (描画側は従来どおり遮蔽を反映する。所持中に到達できる反転壁は
            //  必ず光球内かつ遮蔽なしなので、実プレイ上の見た目とは一致する)
            if (IsOrbHeldByPlayer(orb)) return true;

            if (occluders.empty()) return true;
            for (float h : PHASE_WALL_SAMPLE_HEIGHTS) {
                p.y = (std::min)(floorY + h, topY - 0.10f);
                if (!OpticsEngine::EvaluateLight(p, orb, occluders)) return false;
            }
            return true;
        }

        // ================================================================
        // Phase 2: 遮蔽マスク参照 — (x, z) の反転床サブセルに宝玉の光が届いているか
        // ================================================================
        static bool IsPhaseFloorLit(
            const std::vector<PedestalColliderBake>& bakes,
            const std::vector<OcclusionGeometry>& occluders,
            const OrbRuntimeState& orb,
            uint64_t floorInstanceId,
            float x, float z, float floorY)
        {
            // 台座に挿された宝玉はステージロード時のサブセルマスクをビットテストするだけ。
            // ただし脱着アニメーション中は宝玉が台座から離れて動いているため、
            // 事前ベイクは使わず下のレイキャストで判定する (描画と一致させる)。
            if (orb.Carrier == OrbCarrier::Pedestal && !orb.IsAnimating) {
                for (const auto& b : bakes) {
                    if (b.PedestalInstanceId != orb.PedestalInstanceId) continue;
                    for (const auto& h : b.Holes) {
                        if (h.FloorInstanceId != floorInstanceId) continue;
                        return h.IsLitAt(x, z);
                    }
                    return true;
                }
                return true;
            }

            // 宝玉を所持している間は遮蔽 (影) を考慮せず、光球内の反転床を
            // すべて「消えている」ものとして扱う (IsPhaseWallOpenAt と同じ理由)。
            if (IsOrbHeldByPlayer(orb)) return true;

            // 台座へ戻す最中など、所持していない移動中の宝玉は事前ベイクが無いため
            // その場で 1 本だけレイを飛ばし、描画と判定を一致させる。
            if (occluders.empty()) return true;
            return OpticsEngine::EvaluateLight(XMFLOAT3(x, floorY + 0.05f, z), orb, occluders);
        }

        // 台座宝玉 × 反転床タイルの事前ベイク済みサブセルマスクを引く。
        // マスクがあるタイルでは、穴の実体は「点灯サブセルの和集合」そのものであり、
        // 円弧・タイル辺・影の輪郭のすべてがこの 1 つの形状に含まれる。
        static const CircularHole* FindPhaseFloorHoleMask(
            const std::vector<PedestalColliderBake>& bakes,
            const OrbRuntimeState& orb,
            uint64_t floorInstanceId)
        {
            if (orb.Carrier != OrbCarrier::Pedestal || orb.IsAnimating) return nullptr;
            for (const auto& b : bakes) {
                if (b.PedestalInstanceId != orb.PedestalInstanceId) continue;
                for (const auto& h : b.Holes) {
                    if (h.FloorInstanceId != floorInstanceId) continue;
                    return h.HasMask ? &h : nullptr;
                }
                return nullptr;
            }
            return nullptr;
        }

        // ================================================================
        // 2. 反転床の円形くり抜き進入阻止 (落下防止・円弧接線スライド)
        // ================================================================
        static XMFLOAT3 ConstrainMovementAgainstHoles(
            const XMFLOAT3& currentPos,
            const XMFLOAT3& desiredPos,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionGeometry>& occluders,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            bool isHoldingOrb,
            XMFLOAT3* inoutVelocity = nullptr)
        {
            XMFLOAT3 constrainedPos = desiredPos;
            constexpr float radius = PLAYER_RADIUS;
            constexpr float halfCell = GRID_CELL_SIZE * 0.5f;

            // 各反転床オブジェクトについて判定
            for (const auto& obj : objects) {
                if (obj.Category != AssetCategory::Floor || obj.Phase != MaterialPhase::Phase) continue;

                float floorY = static_cast<float>(obj.FloorIndex) * GRID_FLOOR_HEIGHT;
                if (std::abs(currentPos.y - floorY) > 1.2f) continue;

                float minX = obj.Position.x - halfCell;
                float maxX = obj.Position.x + halfCell;
                float minZ = obj.Position.z - halfCell;
                float maxZ = obj.Position.z + halfCell;

                // プレイヤー円柱がタイルに全く触れていなければ何も起こらない。
                // (穴の境界は必ずタイル内にあるため、距離が半径を超えたら押し戻しは生じない)
                {
                    float ex = (std::max)(0.0f, (std::max)(minX - constrainedPos.x, constrainedPos.x - maxX));
                    float ez = (std::max)(0.0f, (std::max)(minZ - constrainedPos.z, constrainedPos.z - maxZ));
                    if (ex * ex + ez * ez > (radius + 0.05f) * (radius + 0.05f)) continue;
                }

                // 各アクティブ宝玉による円形穴（H = Tile AABB ∩ Disk(orb, rHole)）とのコリジョン判定
                for (const auto& orb : orbs) {
                    // アニメーション中の宝玉も (補間中の位置で) 判定に含める。
                    // 除外すると描画だけが穴になり、見た目と当たり判定がずれる。
                    float dy = std::abs(floorY - orb.WorldPosition.y);
                    if (dy >= orb.Radius) continue;

                    float rHole = std::sqrt(orb.Radius * orb.Radius - dy * dy);
                    float orbX = orb.WorldPosition.x;
                    float orbZ = orb.WorldPosition.z;

                    // タイルAABBと光球円板が交差しているか高速判定
                    float closestBoxX = std::clamp(orbX, minX, maxX);
                    float closestBoxZ = std::clamp(orbZ, minZ, maxZ);
                    float dOrbToBoxSq = (orbX - closestBoxX) * (orbX - closestBoxX) +
                                        (orbZ - closestBoxZ) * (orbZ - closestBoxZ);
                    if (dOrbToBoxSq >= rHole * rHole) continue; // このタイルに穴は開いていない

                    // Phase 2: 遮蔽体の影になっているサブセルは床が残っているため穴として扱わない。
                    //
                    // 重要: マスクは「プレイヤーの居る点」ではなく「穴側の点」で引く必要がある。
                    // タイル外から近づく場合、プレイヤー位置はタイルの外側にありマスクを引けないため、
                    // 以前はここで常に「光っている = 穴」と誤判定し、影で床が残っている
                    // タイルにも進入できなかった。逆にタイル内の影から光の領域へ向かう場合は、
                    // プレイヤー位置が影であるために穴が無いものとして扱われ、穴へ落ちてしまう。
                    // どちらも判定点を「押し戻す対象である穴の側」へ寄せることで解消する。
                    constexpr float probeInset = 0.02f;                       // タイル内へ引き込む最小量
                    constexpr float probeStep = GRID_CELL_SIZE / (float) PHASE_MASK_DIM * 0.55f; // サブセル半分強
                    auto isHoleAt = [&](float px, float pz) -> bool {
                        float cx = std::clamp(px, minX + probeInset, maxX - probeInset);
                        float cz = std::clamp(pz, minZ + probeInset, maxZ - probeInset);
                        return IsPhaseFloorLit(bakedPedestals, occluders, orb, obj.InstanceId, cx, cz, floorY);
                    };

                    bool inBox = (constrainedPos.x >= minX && constrainedPos.x <= maxX &&
                                  constrainedPos.z >= minZ && constrainedPos.z <= maxZ);
                    float toOrbX = constrainedPos.x - orbX;
                    float toOrbZ = constrainedPos.z - orbZ;
                    float dToOrbSq = toOrbX * toOrbX + toOrbZ * toOrbZ;
                    float dToOrb = std::sqrt(dToOrbSq);
                    // 立っている点そのものが穴かどうかは、その点で直接引く
                    bool inHole = (inBox && dToOrb < rHole &&
                                   isHoleAt(constrainedPos.x, constrainedPos.z));

                    // 1. 穴内部へ入り込んでしまった場合
                    if (inHole) {
                        // 直前の位置が安全なら、そこへ戻すだけ (通常の壁と同じ「その場停止」)。
                        // 以前は穴の円の外側まで放射状に押し出していたため、影と光の
                        // 境界に触れた瞬間に離れたマスまで飛ばされていた。
                        bool currentIsHole =
                            (currentPos.x >= minX && currentPos.x <= maxX &&
                             currentPos.z >= minZ && currentPos.z <= maxZ);
                        if (currentIsHole) {
                            float cToOrbX = currentPos.x - orbX;
                            float cToOrbZ = currentPos.z - orbZ;
                            currentIsHole = (cToOrbX * cToOrbX + cToOrbZ * cToOrbZ) < rHole * rHole &&
                                            isHoleAt(currentPos.x, currentPos.z);
                        }

                        if (!currentIsHole) {
                            constrainedPos.x = currentPos.x;
                            constrainedPos.z = currentPos.z;
                        } else if (dToOrb > 1e-4f) {
                            // 直前も穴の中 (テレポート・初期配置等) のときだけ最寄りの
                            // 穴外へ退避する例外リカバリ
                            float nx = toOrbX / dToOrb;
                            float nz = toOrbZ / dToOrb;
                            constrainedPos.x = orbX + nx * (rHole + radius + 0.01f);
                            constrainedPos.z = orbZ + nz * (rHole + radius + 0.01f);
                        } else {
                            constrainedPos.x = currentPos.x;
                            constrainedPos.z = currentPos.z;
                        }
                        if (inoutVelocity) {
                            inoutVelocity->x = 0.0f;
                            inoutVelocity->z = 0.0f;
                        }
                        continue;
                    }

                    // 2. 穴領域 H の境界（円弧境界およびタイル端直線境界）との最近接点 Q を探索
                    float bestDistSq = 1e18f;
                    float bestQx = constrainedPos.x;
                    float bestQz = constrainedPos.z;
                    bool collided = false;

                    // (C) サブセル和集合との距離判定 (事前ベイク済みマスクがある場合)
                    //
                    // マスクがあるタイルでは穴の実体は「点灯サブセルの和集合」であり、
                    // 円弧も、タイル辺も、影の輪郭も、すべてこの 1 つの形状の境界である。
                    // 和集合への最近接点は「各点灯サブセル AABB への最近接点の最小」で
                    // 厳密に求まる (内部の辺も和集合に含まれるため除外は不要)。
                    // これにより、影と光の境界も通常の壁と同じように綺麗に停止し、
                    // 接線方向へはそのまま滑る。
                    const CircularHole* mask = FindPhaseFloorHoleMask(bakedPedestals, orb, obj.InstanceId);
                    if (mask != nullptr) {
                        const float sub = mask->CellSize / (float) PHASE_MASK_DIM;
                        const float reach = radius + sub;
                        int ix0 = (int) std::floor((constrainedPos.x - reach - mask->CellMinXZ.x) / sub);
                        int ix1 = (int) std::floor((constrainedPos.x + reach - mask->CellMinXZ.x) / sub);
                        int iz0 = (int) std::floor((constrainedPos.z - reach - mask->CellMinXZ.y) / sub);
                        int iz1 = (int) std::floor((constrainedPos.z + reach - mask->CellMinXZ.y) / sub);
                        ix0 = std::clamp(ix0, 0, PHASE_MASK_DIM - 1);
                        ix1 = std::clamp(ix1, 0, PHASE_MASK_DIM - 1);
                        iz0 = std::clamp(iz0, 0, PHASE_MASK_DIM - 1);
                        iz1 = std::clamp(iz1, 0, PHASE_MASK_DIM - 1);

                        for (int iz = iz0; iz <= iz1; ++iz) {
                            for (int ix = ix0; ix <= ix1; ++ix) {
                                if (!mask->IsLitCell(ix, iz)) continue;

                                float sx0 = mask->CellMinXZ.x + (float) ix * sub;
                                float sz0 = mask->CellMinXZ.y + (float) iz * sub;
                                float qx = std::clamp(constrainedPos.x, sx0, sx0 + sub);
                                float qz = std::clamp(constrainedPos.z, sz0, sz0 + sub);
                                float dSq = (constrainedPos.x - qx) * (constrainedPos.x - qx) +
                                            (constrainedPos.z - qz) * (constrainedPos.z - qz);
                                if (dSq < bestDistSq) {
                                    bestDistSq = dSq;
                                    bestQx = qx;
                                    bestQz = qz;
                                    collided = true;
                                }
                            }
                        }
                    }

                    // (A) 円弧境界: 反転床上のくり抜き円弧（プレイヤーが円板外側から穴へ接近）
                    // ※マスクがある場合は (C) が円弧も含めて厳密に扱うため使わない
                    if (mask == nullptr && dToOrb >= rHole && dToOrb < rHole + radius) {
                        float dirX = toOrbX / dToOrb;
                        float dirZ = toOrbZ / dToOrb;
                        float qArcX = orbX + dirX * rHole;
                        float qArcZ = orbZ + dirZ * rHole;

                        constexpr float eps = 0.005f;
                        if (qArcX >= minX - eps && qArcX <= maxX + eps &&
                            qArcZ >= minZ - eps && qArcZ <= maxZ + eps &&
                            isHoleAt(qArcX - dirX * probeStep, qArcZ - dirZ * probeStep))
                        {
                            float dSq = (constrainedPos.x - qArcX) * (constrainedPos.x - qArcX) +
                                        (constrainedPos.z - qArcZ) * (constrainedPos.z - qArcZ);
                            if (dSq < bestDistSq) {
                                bestDistSq = dSq;
                                bestQx = qArcX;
                                bestQz = qArcZ;
                                collided = true;
                            }
                        }
                    }

                    // (B) タイル直線境界: 隣接する通常床等からくり抜かれた反転床タイルへ進入しようとする場合
                    // Left: x = minX (プレイヤーが左側から進入)
                    if (mask == nullptr && constrainedPos.x < minX) {
                        float dx = minX - orbX;
                        if (std::abs(dx) <= rHole) {
                            float halfZ = std::sqrt((std::max)(0.0f, rHole * rHole - dx * dx));
                            float zStart = (std::max)(minZ, orbZ - halfZ);
                            float zEnd = (std::min)(maxZ, orbZ + halfZ);
                            if (zStart <= zEnd) {
                                float qz = std::clamp(constrainedPos.z, zStart, zEnd);
                                float dSq = (constrainedPos.x - minX) * (constrainedPos.x - minX) +
                                            (constrainedPos.z - qz) * (constrainedPos.z - qz);
                                if (dSq < bestDistSq && isHoleAt(minX + probeStep, qz)) {
                                    bestDistSq = dSq;
                                    bestQx = minX;
                                    bestQz = qz;
                                    collided = true;
                                }
                            }
                        }
                    }

                    // Right: x = maxX (プレイヤーが右側から進入)
                    if (mask == nullptr && constrainedPos.x > maxX) {
                        float dx = maxX - orbX;
                        if (std::abs(dx) <= rHole) {
                            float halfZ = std::sqrt((std::max)(0.0f, rHole * rHole - dx * dx));
                            float zStart = (std::max)(minZ, orbZ - halfZ);
                            float zEnd = (std::min)(maxZ, orbZ + halfZ);
                            if (zStart <= zEnd) {
                                float qz = std::clamp(constrainedPos.z, zStart, zEnd);
                                float dSq = (constrainedPos.x - maxX) * (constrainedPos.x - maxX) +
                                            (constrainedPos.z - qz) * (constrainedPos.z - qz);
                                if (dSq < bestDistSq && isHoleAt(maxX - probeStep, qz)) {
                                    bestDistSq = dSq;
                                    bestQx = maxX;
                                    bestQz = qz;
                                    collided = true;
                                }
                            }
                        }
                    }

                    // Bottom: z = minZ (プレイヤーが南側から進入)
                    if (mask == nullptr && constrainedPos.z < minZ) {
                        float dz = minZ - orbZ;
                        if (std::abs(dz) <= rHole) {
                            float halfX = std::sqrt((std::max)(0.0f, rHole * rHole - dz * dz));
                            float xStart = (std::max)(minX, orbX - halfX);
                            float xEnd = (std::min)(maxX, orbX + halfX);
                            if (xStart <= xEnd) {
                                float qx = std::clamp(constrainedPos.x, xStart, xEnd);
                                float dSq = (constrainedPos.x - qx) * (constrainedPos.x - qx) +
                                            (constrainedPos.z - minZ) * (constrainedPos.z - minZ);
                                if (dSq < bestDistSq && isHoleAt(qx, minZ + probeStep)) {
                                    bestDistSq = dSq;
                                    bestQx = qx;
                                    bestQz = minZ;
                                    collided = true;
                                }
                            }
                        }
                    }

                    // Top: z = maxZ (プレイヤーが北側から進入)
                    if (mask == nullptr && constrainedPos.z > maxZ) {
                        float dz = maxZ - orbZ;
                        if (std::abs(dz) <= rHole) {
                            float halfX = std::sqrt((std::max)(0.0f, rHole * rHole - dz * dz));
                            float xStart = (std::max)(minX, orbX - halfX);
                            float xEnd = (std::min)(maxX, orbX + halfX);
                            if (xStart <= xEnd) {
                                float qx = std::clamp(constrainedPos.x, xStart, xEnd);
                                float dSq = (constrainedPos.x - qx) * (constrainedPos.x - qx) +
                                            (constrainedPos.z - maxZ) * (constrainedPos.z - maxZ);
                                if (dSq < bestDistSq && isHoleAt(qx, maxZ - probeStep)) {
                                    bestDistSq = dSq;
                                    bestQx = qx;
                                    bestQz = maxZ;
                                    collided = true;
                                }
                            }
                        }
                    }

                    // 3. 非弾性衝突解決（円形に合わせて綺麗に停止・接線スライド・反発力ゼロ）
                    if (collided && bestDistSq < radius * radius) {
                        float dist = std::sqrt(bestDistSq);
                        if (dist > 1e-6f) {
                            float overlap = radius - dist;
                            float nx = (constrainedPos.x - bestQx) / dist;
                            float nz = (constrainedPos.z - bestQz) / dist;

                            constrainedPos.x += nx * overlap;
                            constrainedPos.z += nz * overlap;

                            if (inoutVelocity) {
                                float vDotN = inoutVelocity->x * nx + inoutVelocity->z * nz;
                                if (vDotN < 0.0f) {
                                    inoutVelocity->x -= vDotN * nx;
                                    inoutVelocity->z -= vDotN * nz;
                                }
                            }
                        }
                    }
                }
            }

            return constrainedPos;
        }

        // ================================================================
        // 3. 反転壁復元時の境界斥力 (Anti-Crush / 仕様書 7.1)
        // ※反発力を完全排除（通常の壁コリジョン CollideWithWalls にて完全停止）
        // ================================================================
        static XMFLOAT3 ResolveWallRepulsion(
            const XMFLOAT3& playerPos,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            bool isHoldingOrb,
            bool& outIsBeingRepelled,
            float deltaTime)
        {
            outIsBeingRepelled = false;
            return playerPos; // 反発力を排除（通常の壁コリジョンで完全停止）
        }

        // ================================================================
        // 4. 足元の反転床消失時の安全処理 (仕様書 7.2)
        // ※反発・強制グライドを排除（穴境界コリジョンにて通常壁同様に進入を完全阻止）
        // ================================================================
        static bool CheckFootingLossTrigger(
            const XMFLOAT3& playerPos,
            const std::vector<PlacedObject>& objects,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<PedestalColliderBake>& bakedPedestals,
            bool isHoldingOrb,
            XMFLOAT3& outSafeTargetPos)
        {
            return false; // 反発・強制グライドを排除（通常の壁同様に境界で綺麗に停止）
        }
    };
}
