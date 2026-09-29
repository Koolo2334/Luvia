// ====================================================================
// OrbOcclusion.hlsli - Luminous Shift 宝玉遮蔽判定 (Phase 2)
// ====================================================================
// OrbOcclusionPass が生成した深度専用アトラスを引き、
// 「そのピクセルに宝玉の光が実際に届いているか」を判定する。
//
// 仕様書 5.2/5.3:
//   I_i(X) = 1  <=>  d_i(X) <= R かつ 線分 L(P_i, X) 上に通常壁が存在しない
//
// アトラスは 256px/面 のキューブ 6 面 × 最大 8 宝玉 = 48 面を
// 8 列 × 6 行に敷き詰めた単一深度テクスチャ。
// ここでの定数は Engine.Renderer.OrbOcclusionData.ixx と一致させること。
// ====================================================================
#ifndef ORB_OCCLUSION_HLSLI
#define ORB_OCCLUSION_HLSLI

#include "../SceneGlobals.hlsli"

#define ORB_OCCLUSION_ATLAS_COLS 8.0f
#define ORB_OCCLUSION_ATLAS_ROWS 6.0f

// 方向ベクトルからキューブ面インデックスと面内 UV を算出
// (LightingCore.hlsli の GetCubeFaceUV と同一の規約)
void OrbCubeFaceUV(float3 dir, out int faceIndex, out float2 uv, out float maxVal)
{
    float3 a = abs(dir);
    maxVal = max(a.x, max(a.y, a.z));
    float uc, vc;

    if (maxVal == a.x)
    {
        faceIndex = dir.x > 0.0f ? 0 : 1;
        uc = dir.x > 0.0f ? -dir.z : dir.z;
        vc = -dir.y;
    }
    else if (maxVal == a.y)
    {
        faceIndex = dir.y > 0.0f ? 2 : 3;
        uc = dir.x;
        vc = dir.y > 0.0f ? dir.z : -dir.z;
    }
    else
    {
        faceIndex = dir.z > 0.0f ? 4 : 5;
        uc = dir.z > 0.0f ? dir.x : -dir.x;
        vc = -dir.y;
    }

    uv = float2(uc, vc) / max(maxVal, 1e-6f);
    uv = uv * 0.5f + 0.5f;
}

// 宝玉 orbIndex の光がワールド座標 worldPos に届いているか。
// enabled < 0.5 (= アトラス未生成) のときは遮蔽なしとして扱う。
//
// worldNormal は受光面の法線。深度アトラスは 1 テクセルあたり有限の立体角を
// 持つため、光線が面を舐めるように当たる（斜入射の）ピクセルでは
// 「自分自身のすぐ手前の床/壁」の深度を拾って偽の影が出る。
// これを避けるため、サンプル点を法線方向へずらす Normal Offset を用いる。
// ずらし量は入射角に応じて増やし、正対時はほぼゼロにして影の形を保つ。
bool IsOrbLightReaching(
    float3 worldPos,
    float3 worldNormal,
    float3 orbPos,
    float  orbRadius,
    int    orbIndex,
    uint   atlasIndex,
    float  nearZ,
    float  enabled)
{
    if (enabled < 0.5f)
    {
        return true;
    }

    // OrbOcclusionPass 側と同じ遠方面を使う (宝玉ごとの光半径)
    float farZ = (orbRadius > nearZ * 4.0f) ? orbRadius : 1.0f;

    float3 toOrb = orbPos - worldPos;
    float distToOrb = length(toOrb);
    if (distToOrb < 1e-4f)
    {
        return true;
    }
    float3 L = toOrb / distToOrb;

    // 受光面の法線を光源側へ向け直し、その方向へサンプル点を持ち上げる
    float3 n = normalize(worldNormal);
    n *= (dot(n, L) < 0.0f) ? -1.0f : 1.0f;

    float ndotl = saturate(dot(n, L));
    // 正対 5cm 〜 斜入射 35cm。深度アトラス 256px/面 の角度誤差
    // (光半径 4.2m で約 2.6cm、斜入射で 1/sin 倍に増幅) を吸収する量。
    float offset = 0.05f + 0.30f * (1.0f - ndotl);
    float3 samplePos = worldPos + n * offset;

    float3 dir = samplePos - orbPos;

    int faceIndex;
    float2 faceUV;
    float maxAxis;
    OrbCubeFaceUV(dir, faceIndex, faceUV, maxAxis);

    // 面の縁は隣接面の深度が入っていないため僅かに内側へ寄せる
    faceUV = clamp(faceUV, 0.0015f, 0.9985f);

    int slot = orbIndex * 6 + faceIndex;
    float col = (float) (slot % (int) ORB_OCCLUSION_ATLAS_COLS);
    float row = (float) (slot / (int) ORB_OCCLUSION_ATLAS_COLS);

    float2 atlasUV = float2(
        (col + faceUV.x) / ORB_OCCLUSION_ATLAS_COLS,
        (row + faceUV.y) / ORB_OCCLUSION_ATLAS_ROWS);

    float zNDC = g_AllTextures[atlasIndex].SampleLevel(PointSampler, atlasUV, 0).r;

    // 深度が書かれていない (=遮蔽体なし) 面は 1.0 のままなので必ず届く
    if (zNDC >= 0.99999f)
    {
        return true;
    }

    // NDC 深度 -> 面軸方向のビュー空間距離
    float blockerZ = (nearZ * farZ) / max(farZ - zNDC * (farZ - nearZ), 1e-5f);

    // このピクセル (Normal Offset 適用後) の面軸方向の距離
    float pixelZ = max(maxAxis, nearZ + 1e-4f);

    // 残差ぶんの一定バイアス。Normal Offset で大半は吸収済みなので小さく保つ。
    float bias = max(0.02f, pixelZ * 0.005f);
    return pixelZ <= blockerZ + bias;
}

#endif // ORB_OCCLUSION_HLSLI
