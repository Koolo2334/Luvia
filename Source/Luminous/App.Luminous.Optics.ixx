module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cassert>
#include <string>
#include <random>

export module App.Luminous.Optics;

import App.Luminous.Types;

export namespace App::Luminous {

    using namespace DirectX;

    // ====================================================================
    // オクルーダー幾何種別
    // ====================================================================
    enum class OccluderType : uint32_t {
        VerticalWall = 0,
        HorizontalFloor = 1,
        HorizontalCeiling = 2
    };

    // ====================================================================
    // 光遮蔽剛体 (Occlusion Geometry) の構造体
    // CPU空間走査およびGPU構造化バッファ双方に対応
    // ====================================================================
    struct OcclusionGeometry {
        float MinX = 0.0f, MaxX = 0.0f;
        float MinY = 0.0f, MaxY = 0.0f;
        float MinZ = 0.0f, MaxZ = 0.0f;
        MaterialPhase Phase = MaterialPhase::Normal;
        bool IsGrate = false; // 格子・柵は光判定では透過
        OccluderType Type = OccluderType::VerticalWall;

        // Möller-Trumbore 判定用 4頂点 (矩形平面)
        XMFLOAT3 V0{ 0, 0, 0 };
        XMFLOAT3 V1{ 0, 0, 0 };
        XMFLOAT3 V2{ 0, 0, 0 };
        XMFLOAT3 V3{ 0, 0, 0 };
        bool HasQuad = false;

        bool BlocksLight() const {
            // 仕様書 5.2: 光を完全に遮蔽するのは通常マテリアルのみ。
            // 反転壁・反転床は状態を問わず光を透過し、格子・柵も光判定を透過する。
            // GPU 側 (OrbOccluderTag / OrbOcclusionPass) の描画対象と完全に一致させること。
            return Phase == MaterialPhase::Normal && !IsGrate;
        }
    };

    // 既存コードとの後方互換エイリアス
    using OcclusionWall = OcclusionGeometry;

    // ====================================================================
    // Möller–Trumbore アルゴリズムによるレイ-三角形交差判定 (仕様書 3.2 準拠)
    // ====================================================================
    inline bool RayIntersectsTriangle_MollerTrumbore(
        const XMFLOAT3& orig, const XMFLOAT3& dir, float maxDist,
        const XMFLOAT3& v0, const XMFLOAT3& v1, const XMFLOAT3& v2,
        float& outT)
    {
        constexpr float EPSILON = 1e-7f;
        XMVECTOR p0 = XMLoadFloat3(&orig);
        XMVECTOR d  = XMLoadFloat3(&dir);
        XMVECTOR a  = XMLoadFloat3(&v0);
        XMVECTOR b  = XMLoadFloat3(&v1);
        XMVECTOR c  = XMLoadFloat3(&v2);

        XMVECTOR edge1 = XMVectorSubtract(b, a);
        XMVECTOR edge2 = XMVectorSubtract(c, a);
        XMVECTOR h = XMVector3Cross(d, edge2);
        float aDot = XMVectorGetX(XMVector3Dot(edge1, h));

        if (std::abs(aDot) < EPSILON) return false;

        float f = 1.0f / aDot;
        XMVECTOR s = XMVectorSubtract(p0, a);
        float u = f * XMVectorGetX(XMVector3Dot(s, h));
        if (u < 0.0f || u > 1.0f) return false;

        XMVECTOR q = XMVector3Cross(s, edge1);
        float v = f * XMVectorGetX(XMVector3Dot(d, q));
        if (v < 0.0f || (u + v) > 1.0f) return false;

        float t = f * XMVectorGetX(XMVector3Dot(edge2, q));
        if (t > EPSILON && t <= maxDist) {
            outT = t;
            return true;
        }
        return false;
    }

    // Möller-Trumbore による線分-矩形クアッド交差判定
    inline bool SegmentIntersectsQuad_MollerTrumbore(
        const XMFLOAT3& p0, const XMFLOAT3& p1,
        const XMFLOAT3& v0, const XMFLOAT3& v1, const XMFLOAT3& v2, const XMFLOAT3& v3,
        float& outT)
    {
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float dz = p1.z - p0.z;
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < 1e-6f) return false;

        XMFLOAT3 dir(dx / dist, dy / dist, dz / dist);
        float t = 0.0f;
        // Quad = Tri(v0, v1, v2) + Tri(v0, v2, v3)
        if (RayIntersectsTriangle_MollerTrumbore(p0, dir, dist, v0, v1, v2, t)) {
            outT = t / dist;
            return true;
        }
        if (RayIntersectsTriangle_MollerTrumbore(p0, dir, dist, v0, v2, v3, t)) {
            outT = t / dist;
            return true;
        }
        return false;
    }

    // ====================================================================
    // 幾何交差判定関数 (Line Segment vs AABB - スラブ法)
    // ====================================================================
    // 線分 p0->p1 が AABB に進入する時刻 t (0..1) を返す。交差しなければ -1。
    // p0 が既に内部にある場合は 0 を返す。
    inline float SegmentAABBEntryT(
        const XMFLOAT3& p0, const XMFLOAT3& p1,
        float minX, float maxX, float minY, float maxY, float minZ, float maxZ)
    {
        const float d[3] = { p1.x - p0.x, p1.y - p0.y, p1.z - p0.z };
        const float o[3] = { p0.x, p0.y, p0.z };
        const float lo[3] = { minX, minY, minZ };
        const float hi[3] = { maxX, maxY, maxZ };

        float tMin = 0.0f;
        float tMax = 1.0f;
        for (int a = 0; a < 3; ++a) {
            if (std::abs(d[a]) > 1e-6f) {
                const float invD = 1.0f / d[a];
                float t0 = (lo[a] - o[a]) * invD;
                float t1 = (hi[a] - o[a]) * invD;
                if (t0 > t1) std::swap(t0, t1);
                tMin = (std::max)(tMin, t0);
                tMax = (std::min)(tMax, t1);
                if (tMin > tMax) return -1.0f;
            } else {
                if (o[a] < lo[a] || o[a] > hi[a]) return -1.0f;
            }
        }
        return tMin;
    }

    inline bool SegmentIntersectsAABB(
        const XMFLOAT3& p0, const XMFLOAT3& p1,
        float minX, float maxX, float minY, float maxY, float minZ, float maxZ)
    {
        float dx = p1.x - p0.x;
        float dy = p1.y - p0.y;
        float dz = p1.z - p0.z;

        float tMin = 0.0f;
        float tMax = 1.0f;

        // X軸スラブ判定
        if (std::abs(dx) > 1e-6f) {
            float invD = 1.0f / dx;
            float t0 = (minX - p0.x) * invD;
            float t1 = (maxX - p0.x) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tMin = (std::max)(tMin, t0);
            tMax = (std::min)(tMax, t1);
            if (tMin > tMax) return false;
        } else {
            if (p0.x < minX || p0.x > maxX) return false;
        }

        // Y軸スラブ判定
        if (std::abs(dy) > 1e-6f) {
            float invD = 1.0f / dy;
            float t0 = (minY - p0.y) * invD;
            float t1 = (maxY - p0.y) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tMin = (std::max)(tMin, t0);
            tMax = (std::min)(tMax, t1);
            if (tMin > tMax) return false;
        } else {
            if (p0.y < minY || p0.y > maxY) return false;
        }

        // Z軸スラブ判定
        if (std::abs(dz) > 1e-6f) {
            float invD = 1.0f / dz;
            float t0 = (minZ - p0.z) * invD;
            float t1 = (maxZ - p0.z) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tMin = (std::max)(tMin, t0);
            tMax = (std::min)(tMax, t1);
            if (tMin > tMax) return false;
        } else {
            if (p0.z < minZ || p0.z > maxZ) return false;
        }

        return true;
    }

    // ====================================================================
    // 均一3D空間ボクセル分割グリッド (Amanatides-Woo Fast Voxel Traversal 3D DDA)
    // 仕様書 3.2 準拠: セルサイズ 2.106m により 毎フレーム < 0.5ms の高速光線判定を保証
    // ====================================================================
    class SpatialGrid {
    public:
        float CellSize = GRID_CELL_SIZE; // 2.106m
        XMFLOAT3 Origin{ -4.0f, -2.0f, -4.0f };
        int DimX = 16;
        int DimY = 8;
        int DimZ = 16;

        std::vector<std::vector<uint32_t>> VoxelOccluders;
        mutable std::vector<uint32_t> VisitedStamp;
        mutable uint32_t CurrentQueryId = 1;

        void Build(const std::vector<OcclusionGeometry>& geoms) {
            float minX = -4.0f, minY = -2.0f, minZ = -4.0f;
            float maxX = 20.0f, maxY = 12.0f, maxZ = 20.0f;

            for (const auto& g : geoms) {
                minX = (std::min)(minX, g.MinX - 1.0f);
                minY = (std::min)(minY, g.MinY - 1.0f);
                minZ = (std::min)(minZ, g.MinZ - 1.0f);
                maxX = (std::max)(maxX, g.MaxX + 1.0f);
                maxY = (std::max)(maxY, g.MaxY + 1.0f);
                maxZ = (std::max)(maxZ, g.MaxZ + 1.0f);
            }

            Origin = XMFLOAT3(minX, minY, minZ);
            DimX = (std::max)(1, static_cast<int>(std::ceil((maxX - minX) / CellSize)));
            DimY = (std::max)(1, static_cast<int>(std::ceil((maxY - minY) / CellSize)));
            DimZ = (std::max)(1, static_cast<int>(std::ceil((maxZ - minZ) / CellSize)));

            int totalCells = DimX * DimY * DimZ;
            VoxelOccluders.assign(totalCells, {});
            VisitedStamp.assign(geoms.size(), 0);
            CurrentQueryId = 1;

            for (uint32_t i = 0; i < static_cast<uint32_t>(geoms.size()); ++i) {
                const auto& g = geoms[i];
                if (!g.BlocksLight()) continue;

                int x0 = (std::max)(0, (std::min)(DimX - 1, static_cast<int>(std::floor((g.MinX - Origin.x) / CellSize))));
                int x1 = (std::max)(0, (std::min)(DimX - 1, static_cast<int>(std::floor((g.MaxX - Origin.x) / CellSize))));
                int y0 = (std::max)(0, (std::min)(DimY - 1, static_cast<int>(std::floor((g.MinY - Origin.y) / CellSize))));
                int y1 = (std::max)(0, (std::min)(DimY - 1, static_cast<int>(std::floor((g.MaxY - Origin.y) / CellSize))));
                int z0 = (std::max)(0, (std::min)(DimZ - 1, static_cast<int>(std::floor((g.MinZ - Origin.z) / CellSize))));
                int z1 = (std::max)(0, (std::min)(DimZ - 1, static_cast<int>(std::floor((g.MaxZ - Origin.z) / CellSize))));

                for (int z = z0; z <= z1; ++z) {
                    for (int y = y0; y <= y1; ++y) {
                        for (int x = x0; x <= x1; ++x) {
                            int idx = x + DimX * (y + DimY * z);
                            VoxelOccluders[idx].push_back(i);
                        }
                    }
                }
            }
        }

        // 光線 P0 -> P1 が遮蔽物に当たるかを 3D DDA で高速判定
        bool IsOccluded(const XMFLOAT3& p0, const XMFLOAT3& p1, const std::vector<OcclusionGeometry>& geoms) const {
            if (geoms.empty()) return false;

            CurrentQueryId++;
            if (CurrentQueryId == 0) {
                std::fill(VisitedStamp.begin(), VisitedStamp.end(), 0);
                CurrentQueryId = 1;
            }

            float dx = p1.x - p0.x;
            float dy = p1.y - p0.y;
            float dz = p1.z - p0.z;
            float rayLen = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (rayLen < 1e-6f) return false;

            int x = static_cast<int>(std::floor((p0.x - Origin.x) / CellSize));
            int y = static_cast<int>(std::floor((p0.y - Origin.y) / CellSize));
            int z = static_cast<int>(std::floor((p0.z - Origin.z) / CellSize));

            int endX = static_cast<int>(std::floor((p1.x - Origin.x) / CellSize));
            int endY = static_cast<int>(std::floor((p1.y - Origin.y) / CellSize));
            int endZ = static_cast<int>(std::floor((p1.z - Origin.z) / CellSize));

            int stepX = (dx > 0.0f) ? 1 : (dx < 0.0f ? -1 : 0);
            int stepY = (dy > 0.0f) ? 1 : (dy < 0.0f ? -1 : 0);
            int stepZ = (dz > 0.0f) ? 1 : (dz < 0.0f ? -1 : 0);

            float invDx = (std::abs(dx) > 1e-6f) ? (1.0f / dx) : 1e9f;
            float invDy = (std::abs(dy) > 1e-6f) ? (1.0f / dy) : 1e9f;
            float invDz = (std::abs(dz) > 1e-6f) ? (1.0f / dz) : 1e9f;

            float tDeltaX = std::abs(CellSize * invDx);
            float tDeltaY = std::abs(CellSize * invDy);
            float tDeltaZ = std::abs(CellSize * invDz);

            float nextVoxelBoundaryX = Origin.x + (x + (stepX > 0 ? 1 : 0)) * CellSize;
            float nextVoxelBoundaryY = Origin.y + (y + (stepY > 0 ? 1 : 0)) * CellSize;
            float nextVoxelBoundaryZ = Origin.z + (z + (stepZ > 0 ? 1 : 0)) * CellSize;

            float tMaxX = (stepX != 0) ? ((nextVoxelBoundaryX - p0.x) * invDx) : 1e9f;
            float tMaxY = (stepY != 0) ? ((nextVoxelBoundaryY - p0.y) * invDy) : 1e9f;
            float tMaxZ = (stepZ != 0) ? ((nextVoxelBoundaryZ - p0.z) * invDz) : 1e9f;

            while (true) {
                if (x >= 0 && x < DimX && y >= 0 && y < DimY && z >= 0 && z < DimZ) {
                    int cellIdx = x + DimX * (y + DimY * z);
                    const auto& occluders = VoxelOccluders[cellIdx];
                    for (uint32_t geomIdx : occluders) {
                        if (VisitedStamp[geomIdx] == CurrentQueryId) continue;
                        VisitedStamp[geomIdx] = CurrentQueryId;

                        const auto& g = geoms[geomIdx];
                        if (SegmentIntersectsAABB(p0, p1, g.MinX, g.MaxX, g.MinY, g.MaxY, g.MinZ, g.MaxZ)) {
                            return true; // 早期脱出: 遮蔽発見
                        }
                    }
                }

                if (x == endX && y == endY && z == endZ) break;

                if (tMaxX < tMaxY) {
                    if (tMaxX < tMaxZ) {
                        x += stepX;
                        tMaxX += tDeltaX;
                    } else {
                        z += stepZ;
                        tMaxZ += tDeltaZ;
                    }
                } else {
                    if (tMaxY < tMaxZ) {
                        y += stepY;
                        tMaxY += tDeltaY;
                    } else {
                        z += stepZ;
                        tMaxZ += tDeltaZ;
                    }
                }

                if (tMaxX > 1.0f && tMaxY > 1.0f && tMaxZ > 1.0f) {
                    // 終点セルを最終確認
                    if (endX >= 0 && endX < DimX && endY >= 0 && endY < DimY && endZ >= 0 && endZ < DimZ) {
                        int cellIdx = endX + DimX * (endY + DimY * endZ);
                        const auto& occluders = VoxelOccluders[cellIdx];
                        for (uint32_t geomIdx : occluders) {
                            if (VisitedStamp[geomIdx] == CurrentQueryId) continue;
                            VisitedStamp[geomIdx] = CurrentQueryId;

                            const auto& g = geoms[geomIdx];
                            if (SegmentIntersectsAABB(p0, p1, g.MinX, g.MaxX, g.MinY, g.MaxY, g.MinZ, g.MaxZ)) {
                                return true;
                            }
                        }
                    }
                    break;
                }
            }

            return false;
        }
    };

    // ====================================================================
    // 光線評価エンジン (仕様書 第5条 完全準拠)
    // ====================================================================
    class OpticsEngine {
    public:
        // 単一光源に対する照度関数 I_i(X)
        // ||X - P_i|| <= R かつ 線分上に通常遮蔽体が存在しない場合に 1 (true)
        static bool EvaluateLight(
            const XMFLOAT3& targetPos,
            const OrbRuntimeState& orb,
            const std::vector<OcclusionGeometry>& occluders,
            const SpatialGrid* grid = nullptr)
        {
            float dx = targetPos.x - orb.WorldPosition.x;
            float dy = targetPos.y - orb.WorldPosition.y;
            float dz = targetPos.z - orb.WorldPosition.z;
            float distSq = dx * dx + dy * dy + dz * dz;

            // 1. 球光半径 R のチェック (仕様書 5.3)
            if (distSq > (orb.Radius * orb.Radius)) {
                return false;
            }

            // 2. 空間グリッドによる高速線分交差判定
            if (grid && !grid->VoxelOccluders.empty()) {
                return !grid->IsOccluded(orb.WorldPosition, targetPos, occluders);
            }

            // 3. グリッド未指定時のフォールバック総当たり判定
            for (const auto& g : occluders) {
                if (!g.BlocksLight()) continue;

                if (SegmentIntersectsAABB(
                    orb.WorldPosition, targetPos,
                    g.MinX, g.MaxX, g.MinY, g.MaxY, g.MinZ, g.MaxZ))
                {
                    return false;
                }
            }

            return true;
        }

        // 空間内の複数宝玉に対する最終反転論理和 S(X) = OR_i I_i(X) (仕様書 5.3)
        static bool EvaluateCompositePhase(
            const XMFLOAT3& targetPos,
            const std::vector<OrbRuntimeState>& orbs,
            const std::vector<OcclusionGeometry>& occluders,
            const SpatialGrid* grid = nullptr)
        {
            for (const auto& orb : orbs) {
                if (EvaluateLight(targetPos, orb, occluders, grid)) {
                    return true; // いずれか1つの宝玉の光が到達していれば反転
                }
            }
            return false;
        }

        // ================================================================
        // アセットごとの光遮蔽形状 (アセットのローカル座標の箱の集合)
        // ----------------------------------------------------------------
        // 壁を一律に「セル幅の板」として扱うと、アーチ門や窓・崩れた壁の
        // 開口部まで光を遮り、手持ち宝玉のめり込み防止も開口部で誤作動する。
        // メッシュ (OBJ) の正面投影を実測し、開口部を避けた箱に分割している。
        // ローカル座標は MeshManager の左手系変換後 (Z 反転済み) の値。
        // ================================================================
        struct LocalOccluderBox {
            float MinX, MaxX, MinY, MaxY, MinZ, MaxZ;
        };

        static void GetAssetLocalOccluders(const PlacedObject& obj, std::vector<LocalOccluderBox>& out) {
            out.clear();
            const std::string& id = obj.AssetId;

            if (obj.Category == AssetCategory::Stairs || id == "Stairs_Straight") {
                // Stairs_Straight: 幅 2.106m、奥行 4.206m、12 段でローカル +Z へ 3.063m 上る。
                // 段の下は詰まっているので、各段を床から段の上面までの箱にする。
                // 上面は 2cm 下げて、段の上に置いた宝玉が自分の足場で遮られないようにする。
                constexpr int steps = 12;
                constexpr float halfW = 1.053f;
                constexpr float length = 4.206f;
                constexpr float rise = GRID_FLOOR_HEIGHT;
                for (int k = 0; k < steps; ++k) {
                    const float z0 = -length * 0.5f + length * static_cast<float>(k) / steps;
                    const float z1 = -length * 0.5f + length * static_cast<float>(k + 1) / steps;
                    const float top = rise * static_cast<float>(k + 1) / steps - 0.02f;
                    out.push_back({ -halfW, halfW, 0.0f, top, z0, z1 });
                }
                return;
            }

            if (id == "Wall_DoorArc") {
                // 幅 3.31m のアーチ門。中央 |x|<1.0m・高さ約 2.4m までは素通し、上部はアーチ状
                constexpr float t = 0.30f;
                out.push_back({ -1.656f, -1.00f, 0.0f, 3.30f, -t, t });
                out.push_back({ 1.00f, 1.656f, 0.0f, 3.30f, -t, t });
                out.push_back({ -1.00f, 1.00f, 2.85f, 3.30f, -t, t });
                out.push_back({ -1.00f, -0.90f, 2.40f, 2.85f, -t, t });
                out.push_back({ 0.90f, 1.00f, 2.40f, 2.85f, -t, t });
                out.push_back({ -0.90f, -0.60f, 2.55f, 2.85f, -t, t });
                out.push_back({ 0.60f, 0.90f, 2.55f, 2.85f, -t, t });
                out.push_back({ -0.60f, -0.25f, 2.72f, 2.85f, -t, t });
                out.push_back({ 0.25f, 0.60f, 2.72f, 2.85f, -t, t });
                return;
            }

            if (id == "Wall_Ruined") {
                // 幅 6.32m の崩れた壁。x ∈ [-0.95, 0.38]・高さ 2.35m までが崩落開口
                constexpr float t = 0.25f;
                out.push_back({ -3.159f, -0.95f, 0.0f, 3.066f, -t, t });
                out.push_back({ 0.38f, 3.159f, 0.0f, 3.066f, -t, t });
                out.push_back({ -0.95f, 0.38f, 2.35f, 3.066f, -t, t });
                return;
            }

            if (id == "Wall_Window") {
                // 窓: |x| < 0.55m、y ∈ [1.02, 2.14] は鉄格子 (光を透過)
                constexpr float t = 0.1565f;
                out.push_back({ -1.053f, -0.55f, 0.0f, GRID_FLOOR_HEIGHT, -t, t });
                out.push_back({ 0.55f, 1.053f, 0.0f, GRID_FLOOR_HEIGHT, -t, t });
                out.push_back({ -0.55f, 0.55f, 0.0f, 1.02f, -t, t });
                out.push_back({ -0.55f, 0.55f, 2.14f, GRID_FLOOR_HEIGHT, -t, t });
                return;
            }

            if (id == "Wall_Door") {
                // 扉は閉じた板。上端 (2.4m〜2.85m) はアーチ状で、その上は開いている
                constexpr float t = 0.185f;
                out.push_back({ -1.062f, 1.062f, 0.0f, 2.37f, -t, t });
                out.push_back({ -0.95f, 0.95f, 2.37f, 2.55f, -t, t });
                out.push_back({ -0.70f, 0.70f, 2.55f, 2.72f, -t, t });
                out.push_back({ -0.35f, 0.35f, 2.72f, 2.85f, -t, t });
                return;
            }

            if (id == "Wall_Railing") {
                out.push_back({ -1.053f, 1.053f, 0.0f, 0.98f, -0.132f, 0.132f });
                return;
            }

            // Wall_Brick / Wall_Smooth / Wall_Grate など、セル幅いっぱいの板
            out.push_back({ -GRID_CELL_SIZE * 0.5f, GRID_CELL_SIZE * 0.5f, 0.0f, GRID_FLOOR_HEIGHT, -0.1565f, 0.1565f });
        }

        // ローカルの箱を、オブジェクトの位置と Yaw でワールドの AABB に変換する
        // (Yaw は 90° 単位で配置されるので AABB は厳密に一致する)
        static OcclusionGeometry LocalBoxToWorld(const PlacedObject& obj, const LocalOccluderBox& b) {
            const float yaw = XMConvertToRadians(obj.Rotation.y);
            const float c = std::cos(yaw);
            const float s = std::sin(yaw);

            OcclusionGeometry g;
            g.MinX = g.MinZ = 1e9f;
            g.MaxX = g.MaxZ = -1e9f;
            const float xs[2] = { b.MinX, b.MaxX };
            const float zs[2] = { b.MinZ, b.MaxZ };
            for (float lx : xs) {
                for (float lz : zs) {
                    const float wx = obj.Position.x + lx * c + lz * s;
                    const float wz = obj.Position.z - lx * s + lz * c;
                    g.MinX = (std::min)(g.MinX, wx); g.MaxX = (std::max)(g.MaxX, wx);
                    g.MinZ = (std::min)(g.MinZ, wz); g.MaxZ = (std::max)(g.MaxZ, wz);
                }
            }
            g.MinY = obj.Position.y + b.MinY;
            g.MaxY = obj.Position.y + b.MaxY;
            return g;
        }

        // 配置オブジェクト群および天井から多層オクルーダーリストを構築
        static std::vector<OcclusionGeometry> BuildOcclusionGeometry(
            const std::vector<PlacedObject>& objects,
            bool includeCeilings = true)
        {
            std::vector<OcclusionGeometry> geoms;

            float halfLen = GRID_CELL_SIZE * 0.5f;
            float halfThick = 0.1565f;
            float height = GRID_FLOOR_HEIGHT;

            // 階段吹き抜けセルの幾何検出 (Scenes.ixx と完全一致)
            auto isStairOpeningCell = [&](int cx, int cz) -> bool {
                for (const auto& sObj : objects) {
                    if (sObj.AssetId == "Stairs_Straight" && sObj.FloorIndex == 0) {
                        float cellX = (static_cast<float>(cx) + 0.5f) * GRID_CELL_SIZE;
                        float cellZ = (static_cast<float>(cz) + 0.5f) * GRID_CELL_SIZE;
                        float dx = cellX - sObj.Position.x;
                        float dz = cellZ - sObj.Position.z;
                        if (std::abs(dx) <= GRID_CELL_SIZE * 0.6f && std::abs(dz) <= GRID_CELL_SIZE * 1.5f) {
                            return true;
                        }
                    }
                }
                return false;
            };

            std::vector<LocalOccluderBox> localBoxes;
            for (const auto& obj : objects) {
                if (obj.Category == AssetCategory::Wall ||
                    obj.Category == AssetCategory::Stairs || obj.AssetId == "Stairs_Straight") {
                    // 1. 壁・階段オクルーダー (アセットごとの形状。開口部は遮らない)
                    const bool isGrate = (obj.AssetId == "Wall_Grate" || obj.AssetId == "Wall_Railing");
                    GetAssetLocalOccluders(obj, localBoxes);
                    const bool singlePanel = (localBoxes.size() == 1 && obj.Category == AssetCategory::Wall);

                    for (const auto& lb : localBoxes) {
                        OcclusionGeometry wall = LocalBoxToWorld(obj, lb);
                        wall.Phase = obj.Phase;
                        wall.IsGrate = isGrate;
                        wall.Type = OccluderType::VerticalWall;

                        // 単純な板の壁にはクアッド平面頂点も登録する
                        if (singlePanel) {
                            const bool isRotated = (std::abs(std::fmod(obj.Rotation.y, 180.0f)) > 45.0f);
                            wall.HasQuad = true;
                            if (isRotated) {
                                wall.V0 = XMFLOAT3(obj.Position.x, wall.MinY, wall.MinZ);
                                wall.V1 = XMFLOAT3(obj.Position.x, wall.MinY, wall.MaxZ);
                                wall.V2 = XMFLOAT3(obj.Position.x, wall.MaxY, wall.MaxZ);
                                wall.V3 = XMFLOAT3(obj.Position.x, wall.MaxY, wall.MinZ);
                            } else {
                                wall.V0 = XMFLOAT3(wall.MinX, wall.MinY, obj.Position.z);
                                wall.V1 = XMFLOAT3(wall.MaxX, wall.MinY, obj.Position.z);
                                wall.V2 = XMFLOAT3(wall.MaxX, wall.MaxY, obj.Position.z);
                                wall.V3 = XMFLOAT3(wall.MinX, wall.MaxY, obj.Position.z);
                            }
                        }

                        geoms.push_back(wall);
                    }

                } else if (obj.Category == AssetCategory::Floor) {
                    // 2. 通常石畳床スラブ (水平剛体)
                    // 反転床(Phase)は光を100%透過するため遮蔽しない
                    OcclusionGeometry floorSlab;
                    floorSlab.Phase = obj.Phase;
                    floorSlab.IsGrate = false;
                    floorSlab.Type = OccluderType::HorizontalFloor;

                    floorSlab.MinX = obj.Position.x - halfLen;
                    floorSlab.MaxX = obj.Position.x + halfLen;
                    floorSlab.MinZ = obj.Position.z - halfLen;
                    floorSlab.MaxZ = obj.Position.z + halfLen;
                    // 床上面のテスト点(Y+0.1m)が自己遮蔽されないよう上面を Y-0.01m に設定
                    floorSlab.MinY = obj.Position.y - 0.20f;
                    floorSlab.MaxY = obj.Position.y - 0.01f;

                    floorSlab.HasQuad = true;
                    floorSlab.V0 = XMFLOAT3(floorSlab.MinX, floorSlab.MaxY, floorSlab.MinZ);
                    floorSlab.V1 = XMFLOAT3(floorSlab.MaxX, floorSlab.MaxY, floorSlab.MinZ);
                    floorSlab.V2 = XMFLOAT3(floorSlab.MaxX, floorSlab.MaxY, floorSlab.MaxZ);
                    floorSlab.V3 = XMFLOAT3(floorSlab.MinX, floorSlab.MaxY, floorSlab.MaxZ);

                    geoms.push_back(floorSlab);
                }
            }

            // 3. 天井スラブ (密閉ダンジョン天井 / 水平剛体)
            if (includeCeilings) {
                for (int cx = 0; cx < 5; ++cx) {
                    for (int cz = 0; cz < 5; ++cz) {
                        // 階段吹き抜けセルは除外 (上下階層への立体光線シャフト)
                        if (isStairOpeningCell(cx, cz)) continue;

                        float centerX = (static_cast<float>(cx) + 0.5f) * GRID_CELL_SIZE;
                        float centerZ = (static_cast<float>(cz) + 0.5f) * GRID_CELL_SIZE;

                        OcclusionGeometry ceilSlab;
                        ceilSlab.Phase = MaterialPhase::Normal;
                        ceilSlab.IsGrate = false;
                        ceilSlab.Type = OccluderType::HorizontalCeiling;
                        ceilSlab.MinX = centerX - halfLen;
                        ceilSlab.MaxX = centerX + halfLen;
                        ceilSlab.MinZ = centerZ - halfLen;
                        ceilSlab.MaxZ = centerZ + halfLen;
                        ceilSlab.MinY = GRID_FLOOR_HEIGHT - 0.05f;
                        ceilSlab.MaxY = GRID_FLOOR_HEIGHT + 0.15f;

                        ceilSlab.HasQuad = true;
                        ceilSlab.V0 = XMFLOAT3(ceilSlab.MinX, ceilSlab.MinY, ceilSlab.MinZ);
                        ceilSlab.V1 = XMFLOAT3(ceilSlab.MaxX, ceilSlab.MinY, ceilSlab.MinZ);
                        ceilSlab.V2 = XMFLOAT3(ceilSlab.MaxX, ceilSlab.MinY, ceilSlab.MaxZ);
                        ceilSlab.V3 = XMFLOAT3(ceilSlab.MinX, ceilSlab.MinY, ceilSlab.MaxZ);

                        geoms.push_back(ceilSlab);
                    }
                }
            }

            return geoms;
        }

        // 後方互換関数
        static std::vector<OcclusionGeometry> BuildOcclusionWalls(const std::vector<PlacedObject>& objects) {
            return BuildOcclusionGeometry(objects, true);
        }

        // ====================================================================
        // 10項目の厳密単体テスト検証ルーチン
        // ====================================================================
        static bool RunUnitTests(std::string& outErrorMsg) {
            OrbRuntimeState orb;
            orb.WorldPosition = XMFLOAT3(0.0f, 1.0f, 0.0f);
            orb.Radius = DEFAULT_ORB_RADIUS; // 6.318m

            std::vector<OcclusionGeometry> geoms;

            // テスト 1: 遮蔽物のない近距離点 -> I = 1
            XMFLOAT3 pClose(2.0f, 1.0f, 0.0f);
            if (!EvaluateLight(pClose, orb, geoms)) {
                outErrorMsg = "Test 1 Failed: Unobstructed point must be illuminated.";
                return false;
            }

            // テスト 2: 半径外の遠距離点 -> I = 0
            XMFLOAT3 pFar(8.0f, 1.0f, 0.0f);
            if (EvaluateLight(pFar, orb, geoms)) {
                outErrorMsg = "Test 2 Failed: Point beyond radius must NOT be illuminated.";
                return false;
            }

            // 通常壁を (1.0, 0.0, 0.0) に設置 (X: 0.9 ~ 1.1, Y: 0 ~ 3, Z: -1 ~ 1)
            OcclusionGeometry normalWall;
            normalWall.MinX = 0.9f; normalWall.MaxX = 1.1f;
            normalWall.MinY = 0.0f; normalWall.MaxY = 3.0f;
            normalWall.MinZ = -1.0f; normalWall.MaxZ = 1.0f;
            normalWall.Phase = MaterialPhase::Normal;
            normalWall.IsGrate = false;
            normalWall.Type = OccluderType::VerticalWall;
            normalWall.HasQuad = true;
            normalWall.V0 = XMFLOAT3(1.0f, 0.0f, -1.0f);
            normalWall.V1 = XMFLOAT3(1.0f, 0.0f, 1.0f);
            normalWall.V2 = XMFLOAT3(1.0f, 3.0f, 1.0f);
            normalWall.V3 = XMFLOAT3(1.0f, 3.0f, -1.0f);
            geoms.push_back(normalWall);

            // テスト 3: 通常壁に遮られた点 -> I = 0
            if (EvaluateLight(pClose, orb, geoms)) {
                outErrorMsg = "Test 3 Failed: Point behind Normal Wall must be occluded.";
                return false;
            }

            // テスト 4: 通常壁を反転壁に変更 -> 反転マテリアルは光を透過する -> I = 1
            geoms[0].Phase = MaterialPhase::Phase;
            if (!EvaluateLight(pClose, orb, geoms)) {
                outErrorMsg = "Test 4 Failed: Phase materials must NOT occlude orb light.";
                return false;
            }

            // テスト 5: 格子・柵に変更 -> I = 1 (格子は光判定を完全透過)
            geoms[0].Phase = MaterialPhase::Normal;
            geoms[0].IsGrate = true;
            if (!EvaluateLight(pClose, orb, geoms)) {
                outErrorMsg = "Test 5 Failed: Grate must NOT occlude light logic.";
                return false;
            }
            geoms[0].IsGrate = false; // 再び遮蔽通常壁に戻す

            // テスト 6: 複数宝玉のOR結合 (宝玉1は遮蔽、宝玉2は非遮蔽)
            OrbRuntimeState orb2;
            orb2.WorldPosition = XMFLOAT3(3.0f, 1.0f, 0.0f);
            orb2.Radius = DEFAULT_ORB_RADIUS;

            std::vector<OrbRuntimeState> multipleOrbs = { orb, orb2 };
            if (!EvaluateCompositePhase(pClose, multipleOrbs, geoms)) {
                outErrorMsg = "Test 6 Failed: Composite phase S(X) should be TRUE via Orb2.";
                return false;
            }

            // テスト 7: Möller-Trumbore 矩形交差判定の精密テスト
            float mtT = 0.0f;
            XMFLOAT3 mtRayStart(0.0f, 1.5f, 0.0f);
            XMFLOAT3 mtRayEnd(2.0f, 1.5f, 0.0f);
            if (!SegmentIntersectsQuad_MollerTrumbore(mtRayStart, mtRayEnd,
                normalWall.V0, normalWall.V1, normalWall.V2, normalWall.V3, mtT))
            {
                outErrorMsg = "Test 7 Failed: Moller-Trumbore ray-quad intersection missed.";
                return false;
            }

            // テスト 8: 多層石畳床スラブによる上下階層遮蔽テスト
            // 2Fの宝玉 (0, 4.0, 0) から 1F (0, 0.5, 0) へ向かう光線が Y=3.0m の床スラブで遮蔽されるか
            OcclusionGeometry floorSlab;
            floorSlab.MinX = -2.0f; floorSlab.MaxX = 2.0f;
            floorSlab.MinZ = -2.0f; floorSlab.MaxZ = 2.0f;
            floorSlab.MinY = 2.90f; floorSlab.MaxY = 3.05f;
            floorSlab.Phase = MaterialPhase::Normal;
            floorSlab.Type = OccluderType::HorizontalFloor;

            std::vector<OcclusionGeometry> multiFloorGeoms = { floorSlab };
            OrbRuntimeState orb2F;
            orb2F.WorldPosition = XMFLOAT3(0.0f, 4.0f, 0.0f);
            orb2F.Radius = DEFAULT_ORB_RADIUS;
            XMFLOAT3 p1F(0.0f, 0.5f, 0.0f);

            if (EvaluateLight(p1F, orb2F, multiFloorGeoms)) {
                outErrorMsg = "Test 8 Failed: Solid floor slab must occlude cross-floor light.";
                return false;
            }

            // テスト 9: 階段吹き抜け開口部による上下階層光伝播テスト
            // 開口部セル (X: 3.0 ~ 5.0) を通る斜め光線 (0, 4.0, 4.0) -> (0, 0.5, 4.0) は床がないため透過する
            XMFLOAT3 p1FOpening(0.0f, 0.5f, 4.0f);
            OrbRuntimeState orb2FOpening;
            orb2FOpening.WorldPosition = XMFLOAT3(0.0f, 4.0f, 4.0f);
            orb2FOpening.Radius = DEFAULT_ORB_RADIUS;
            if (!EvaluateLight(p1FOpening, orb2FOpening, multiFloorGeoms)) {
                outErrorMsg = "Test 9 Failed: Light must pass through stairwell opening between floors.";
                return false;
            }

            // テスト 10: 均一3D空間ボクセルグリッド (SpatialGrid DDA) と総当たり判定の完全一致検証 (100本ランダム光線)
            SpatialGrid testGrid;
            std::vector<OcclusionGeometry> randomScene = { normalWall, floorSlab };
            // 追加のランダム壁
            OcclusionGeometry w2;
            w2.MinX = 3.0f; w2.MaxX = 3.2f; w2.MinY = 0.0f; w2.MaxY = 3.0f; w2.MinZ = 0.0f; w2.MaxZ = 4.0f;
            w2.Phase = MaterialPhase::Normal;
            randomScene.push_back(w2);

            testGrid.Build(randomScene);

            std::mt19937 rng(1337);
            std::uniform_real_distribution<float> distCoord(-3.0f, 5.0f);
            std::uniform_real_distribution<float> distY(0.5f, 5.0f);

            for (int k = 0; k < 100; ++k) {
                XMFLOAT3 rP0(distCoord(rng), distY(rng), distCoord(rng));
                XMFLOAT3 rP1(distCoord(rng), distY(rng), distCoord(rng));

                bool ddaOccluded = testGrid.IsOccluded(rP0, rP1, randomScene);

                bool bruteOccluded = false;
                for (const auto& g : randomScene) {
                    if (g.BlocksLight() && SegmentIntersectsAABB(rP0, rP1, g.MinX, g.MaxX, g.MinY, g.MaxY, g.MinZ, g.MaxZ)) {
                        bruteOccluded = true;
                        break;
                    }
                }

                if (ddaOccluded != bruteOccluded) {
                    outErrorMsg = "Test 10 Failed: SpatialGrid DDA result does not match brute-force at ray " + std::to_string(k);
                    return false;
                }
            }

            // テスト 11: 階段は光を遮る (段の上面より上は通す)
            {
                PlacedObject stairs;
                stairs.AssetId = "Stairs_Straight";
                stairs.Category = AssetCategory::Stairs;
                const auto stairGeoms = BuildOcclusionGeometry({ stairs }, false);

                OrbRuntimeState low;
                low.WorldPosition = XMFLOAT3(-2.0f, 0.8f, 0.5f);
                low.Radius = DEFAULT_ORB_RADIUS;
                if (EvaluateLight(XMFLOAT3(2.0f, 0.8f, 0.5f), low, stairGeoms)) {
                    outErrorMsg = "Test 11 Failed: Stairs must occlude orb light through their body.";
                    return false;
                }

                OrbRuntimeState high;
                high.WorldPosition = XMFLOAT3(-2.0f, 3.3f, 0.5f);
                high.Radius = DEFAULT_ORB_RADIUS;
                if (!EvaluateLight(XMFLOAT3(2.0f, 3.3f, 0.5f), high, stairGeoms)) {
                    outErrorMsg = "Test 11 Failed: Light above the stairs must not be occluded.";
                    return false;
                }
            }

            // テスト 12: アーチ門は開口部で光を通し、柱で遮る
            {
                PlacedObject arch;
                arch.AssetId = "Wall_DoorArc";
                arch.Category = AssetCategory::Wall;
                const auto archGeoms = BuildOcclusionGeometry({ arch }, false);

                OrbRuntimeState center;
                center.WorldPosition = XMFLOAT3(0.0f, 1.2f, -1.5f);
                center.Radius = DEFAULT_ORB_RADIUS;
                if (!EvaluateLight(XMFLOAT3(0.0f, 1.2f, 1.5f), center, archGeoms)) {
                    outErrorMsg = "Test 12 Failed: Light must pass through the archway opening.";
                    return false;
                }

                OrbRuntimeState side;
                side.WorldPosition = XMFLOAT3(1.3f, 1.2f, -1.5f);
                side.Radius = DEFAULT_ORB_RADIUS;
                if (EvaluateLight(XMFLOAT3(1.3f, 1.2f, 1.5f), side, archGeoms)) {
                    outErrorMsg = "Test 12 Failed: Archway pillars must occlude light.";
                    return false;
                }
            }

            return true;
        }
    };
}
