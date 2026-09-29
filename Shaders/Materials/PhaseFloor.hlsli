// ====================================================================
// PhaseFloor.hlsli - Luminous Shift 反転床マテリアルシェーダー
// ====================================================================
// 元のオブジェクト（石畳舗装床）のテクスチャ構造に、
// 深淵エーテルオーラと、表面を呼吸・漂流しながら一部が僅かに発光する
// アニメーションルーン脈を合成。
// 宝玉の光球面内部 (d < R) は幾何学的にくり抜かれ、深淵の穴が出現。
// ====================================================================

#include "../StandardMaterialCommon.hlsli"
#include "../CharacterMaterialCommon.hlsli"
#include "OrbOcclusion.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    float4 DarkColor;      // S=0 時の色 (青白石畳 [0.45, 0.55, 0.65, 1.0])
    float4 VoidEdgeColor;  // S=1 時の穴の縁発光色 (深淵シアン [0.15, 0.80, 1.0, 1.0])
    float4 OrbPositions[8]; // xyz: 各宝玉の座標, w: 光半径 R
    float ActiveOrbCount;   // アクティブ宝玉数 (0 - 8)
    float DissolveEdge;    // 境界エッジ幅 (0.12m)
    float PulseSpeed;      // 格子明滅速度 (2.5)
    float EmissiveBoost;   // エッジ発光強度 (2.5)

    // Phase 2: 宝玉遮蔽 (OrbOcclusionPass が生成した深度アトラス)
    float OrbOcclusionAtlasIndex; // g_AllTextures[] へのバインドレス添字
    float OrbOcclusionNearZ;      // 深度射影の近接面
    float OrbOcclusionFarZ;       // 予備 (遠方面は宝玉ごとの半径を使用)
    float OrbOcclusionEnabled;    // 0: アトラス未生成につき遮蔽判定を行わない
};
MaterialCBLayout GetMaterialCB() { return MaterialData.Load<MaterialCBLayout>(g_MaterialOffset); }

// 格子を巻き取る周期。
// 時間ドリフトを座標に足し込むと座標が単調に増え続け、float の仮数が
// 足りなくなってハッシュが劣化・周期化し、アニメーションの「ループの継ぎ目」
// として見えてしまう。格子を NOISE_PERIOD で巻き取った周期ノイズにし、
// ドリフト量も同じ周期で巻き取れば、精度を保ったまま継ぎ目なく流し続けられる。
// (ワールド空間での繰り返しは NOISE_PERIOD / 周波数 = 20〜40m でステージより大きい)
#define NOISE_PERIOD 64.0f

float3 WrapLattice(float3 i)
{
    return i - NOISE_PERIOD * floor(i / NOISE_PERIOD);
}

// ドリフト量を周期内に巻き取る。ノイズが NOISE_PERIOD 周期なので巻き戻りは不可視。
float WrapDrift(float v)
{
    return v - NOISE_PERIOD * floor(v / NOISE_PERIOD);
}

float Hash3D(float3 p)
{
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float ValueNoise3D(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    float3 u = f * f * (3.0f - 2.0f * f);
    float c000 = Hash3D(WrapLattice(i + float3(0, 0, 0)));
    float c100 = Hash3D(WrapLattice(i + float3(1, 0, 0)));
    float c010 = Hash3D(WrapLattice(i + float3(0, 1, 0)));
    float c110 = Hash3D(WrapLattice(i + float3(1, 1, 0)));
    float c001 = Hash3D(WrapLattice(i + float3(0, 0, 1)));
    float c101 = Hash3D(WrapLattice(i + float3(1, 0, 1)));
    float c011 = Hash3D(WrapLattice(i + float3(0, 1, 1)));
    float c111 = Hash3D(WrapLattice(i + float3(1, 1, 1)));
    return lerp(
        lerp(lerp(c000, c100, u.x), lerp(c010, c110, u.x), u.y),
        lerp(lerp(c001, c101, u.x), lerp(c011, c111, u.x), u.y),
        u.z
    );
}

float CalcAuraNoiseFloor(float3 p, float t)
{
    float3 drift1 = float3(WrapDrift(t * 0.25f), 0.0f, WrapDrift(t * 0.20f));
    float3 drift2 = float3(WrapDrift(-t * 0.15f), 0.0f, WrapDrift(t * 0.30f));
    float n1 = ValueNoise3D(p * 1.5f + drift1);
    float n2 = ValueNoise3D(p * 3.0f + drift2 + n1 * 0.5f);
    return n1 * 0.62f + n2 * 0.38f;
}

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    float3 V = normalize(CameraPosition - input.WorldPos);
    float3 N = normalize(input.WorldNormal);
    
    // ------------------------------------------------------------
    // 1. 宝玉群からの球面距離判定 (Spherical Hole Cutout)
    // ------------------------------------------------------------
    bool isInsideHole = false;
    float minEdgeDist = 9999.0f;
    
    int numOrbs = (int)GetMaterialCB().ActiveOrbCount;
    for (int i = 0; i < numOrbs; ++i)
    {
        float4 orb = GetMaterialCB().OrbPositions[i];
        float dist = distance(input.WorldPos, orb.xyz);
        float edgeDist = dist - orb.w;

        // 球面内でも、宝玉との間に通常壁があるピクセルには光が届かないため
        // 床は消えない (遮蔽体の影がそのままの形で足場として残る)。
        if (!IsOrbLightReaching(input.WorldPos, input.WorldNormal, orb.xyz, orb.w, i,
                                (uint) GetMaterialCB().OrbOcclusionAtlasIndex,
                                GetMaterialCB().OrbOcclusionNearZ, GetMaterialCB().OrbOcclusionEnabled))
        {
            continue;
        }
        
        if (edgeDist < 0.0f)
        {
            isInsideHole = true;
            break;
        }
        minEdgeDist = min(minEdgeDist, edgeDist);
    }
    
    // 球面内部: 完全くり抜き・ディスカード (床が消えて深淵の穴になる)
    if (isInsideHole)
    {
        attr.OpacityMask = 0.0f;
        discard;
        return;
    }
    
    // 球面境界の円形ピット警告発光リング
    float edgeFactor = 0.0f;
    if (minEdgeDist < GetMaterialCB().DissolveEdge)
    {
        float t = 1.0f - saturate(minEdgeDist / GetMaterialCB().DissolveEdge);
        edgeFactor = t * t;
    }

    // ------------------------------------------------------------
    // 2. 元のオブジェクトのテクスチャ（石畳スラブ舗装・目地・石肌）
    // ------------------------------------------------------------
    float2 floorUV = input.WorldPos.xz / (2.106f * 0.5f);
    float2 slabCell = frac(floorUV);
    float2 slabDist = min(slabCell, 1.0f - slabCell);
    float slabBorder = smoothstep(0.03f, 0.09f, min(slabDist.x, slabDist.y));
    
    float slabHash = frac(sin(dot(floor(floorUV), float2(41.13f, 79.51f))) * 43758.5453f);
    float stoneGrain = frac(sin(dot(input.WorldPos * 33.0f, float3(31.41f, 15.92f, 65.35f))) * 43758.5453f) * 0.05f;
    
    // 元のオブジェクトのカラー (GetMaterialCB().DarkColor) を100%主軸として鮮明に反映した石畳ベース
    float3 seamColor = GetMaterialCB().DarkColor.rgb * 0.60f;
    float3 slabStoneColor = GetMaterialCB().DarkColor.rgb * (1.0f + slabHash * 0.20f + stoneGrain);
    float3 originalFloor = lerp(seamColor, slabStoneColor, slabBorder);

    // ------------------------------------------------------------
    // 3. 反転マテリアルオーラの合成
    // ------------------------------------------------------------
    float auraNoise = CalcAuraNoiseFloor(input.WorldPos, input.TotalTime);
    
    float3 auraVoid = float3(0.10f, 0.14f, 0.28f);
    float3 auraCyan = float3(0.08f, 0.42f, 0.60f);
    float3 auraCompositeColor = lerp(auraVoid, auraCyan, auraNoise);
    
    // 元の床材の固有色を主軸として保つ。
    // 加算合成だと暗い床材ほどオーラ色に塗り潰されて元の色が出ないため、
    // 色味は乗算による変調で与え、加算はもとの明度に比例させる。
    float auraRatio = 0.12f + auraNoise * 0.08f;
    float3 auraTint = lerp(float3(0.84f, 0.80f, 1.04f), float3(0.76f, 1.02f, 1.10f), auraNoise);
    float originalLum = dot(originalFloor, float3(0.299f, 0.587f, 0.114f));
    float3 synthesizedColor =
        originalFloor * lerp(float3(1.0f, 1.0f, 1.0f), auraTint, saturate(auraRatio * 3.0f))
        + auraCompositeColor * (auraRatio * originalLum * 1.1f);
    attr.BaseColor = synthesizedColor + (GetMaterialCB().VoidEdgeColor.rgb * edgeFactor * 0.45f);

    // ------------------------------------------------------------
    // 4. 一部が僅かに発光したアニメーション（床面を這う微光ルーン脈）
    // ------------------------------------------------------------
    float veinNoise = ValueNoise3D(input.WorldPos * 3.8f + float3(WrapDrift(input.TotalTime * 0.4f), 0.0f, WrapDrift(input.TotalTime * 0.35f)));
    float veinFilament = pow(saturate(1.0f - abs(veinNoise - 0.5f) * 3.6f), 2.2f);
    
    float pulse1 = sin(input.TotalTime * 2.2f + input.WorldPos.x * 2.0f) * 0.5f + 0.5f;
    float pulse2 = cos(input.TotalTime * 1.4f + input.WorldPos.z * 2.2f) * 0.5f + 0.5f;
    float veinPulse = pulse1 * 0.65f + pulse2 * 0.35f;
    
    // 一部が僅かに発光するシアンルーン発光。
    // 発光がベースカラーを覆って元の床材の色が判別できなくなっていたため、
    // 強度を落とし、脈の色に元の床材色を混ぜる。
    float3 mysticVein = lerp(float3(0.12f, 0.45f, 0.80f), float3(0.20f, 0.90f, 1.25f), pulse1);
    float3 veinColor = lerp(mysticVein, mysticVein * 0.35f + originalFloor * 1.6f, 0.5f);
    float3 subtleVeinEmissive = veinColor * veinFilament * (veinPulse * 0.7f + 0.3f) * 0.55f;
    float3 subtleAuraEmissive = float3(0.04f, 0.12f, 0.20f) * (auraNoise * 0.30f);
    
    // 穴の縁の警告発光リング
    float pulseEdge = sin(input.TotalTime * GetMaterialCB().PulseSpeed) * 0.12f + 0.88f;
    float3 ringEmissive = GetMaterialCB().VoidEdgeColor.rgb * (edgeFactor * 4.0f) * pulseEdge * GetMaterialCB().EmissiveBoost;
    
    attr.Emissive = subtleVeinEmissive + subtleAuraEmissive + ringEmissive;

    // ------------------------------------------------------------
    // 5. PBR マテリアル属性
    // ------------------------------------------------------------
    attr.Normal = N;
    attr.Roughness = lerp(0.58f, 0.12f, saturate(auraNoise * 0.6f + edgeFactor * 0.4f));
    attr.Metallic = lerp(0.06f, 0.24f, auraNoise);
    attr.AO = slabBorder * 0.35f + 0.65f;
    attr.OpacityMask = 1.0f;
}

void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
}
