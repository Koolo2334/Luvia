// ====================================================================
// PhaseWall.hlsli - Luminous Shift 反転壁マテリアルシェーダー
// ====================================================================
// 元のオブジェクト（石レンガ壁）のテクスチャ構造に、
// 多層プロシージャルノイズによる神秘的な反転マテリアルのオーラと、
// レンガ目地や石肌を這うように一部が僅かに発光するアニメーションエネルギー脈を合成。
// 宝玉の光球面内部 (d < R) は幾何学的に完全くり抜き（透過）。
// ====================================================================

#include "../StandardMaterialCommon.hlsli"
#include "../CharacterMaterialCommon.hlsli"
#include "OrbOcclusion.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    float4 DarkColor;      // S=0 時の色 (黒紫 [0.08, 0.02, 0.15, 1.0])
    float4 LitColor;       // S=1 時の色 (光子シアン [0.20, 0.85, 1.0, 0.25])
    float4 OrbPositions[8]; // xyz: 各宝玉の座標, w: 光半径 R
    float ActiveOrbCount;   // アクティブ宝玉数 (0 - 8)
    float DissolveEdge;    // 境界エッジ幅 (0.12m)
    float PulseSpeed;      // 格子明滅速度 (2.5)
    float EmissiveBoost;   // エミッシブ強度 (2.5)

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

float CalcInversionAuraNoise(float3 p, float t)
{
    float3 drift1 = float3(0.0f, WrapDrift(t * 0.35f), 0.0f);
    float3 drift2 = float3(WrapDrift(t * 0.20f), WrapDrift(-t * 0.15f), WrapDrift(t * 0.18f));
    float n1 = ValueNoise3D(p * 1.6f + drift1);
    float n2 = ValueNoise3D(p * 3.2f + drift2 + n1 * 0.6f);
    return n1 * 0.60f + n2 * 0.40f;
}

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    float3 V = normalize(CameraPosition - input.WorldPos);
    float3 N = normalize(input.WorldNormal);
    float VdotN = abs(dot(V, N));
    float rim = pow(saturate(1.0f - VdotN), 3.0f);
    
    // ------------------------------------------------------------
    // 1. 宝玉群からの球面距離判定 (Spherical Carve-Out)
    // ------------------------------------------------------------
    bool isInsideCutout = false;
    float minEdgeDist = 9999.0f;
    
    int numOrbs = (int)GetMaterialCB().ActiveOrbCount;
    for (int i = 0; i < numOrbs; ++i)
    {
        float4 orb = GetMaterialCB().OrbPositions[i];
        float dist = distance(input.WorldPos, orb.xyz);
        float edgeDist = dist - orb.w;

        // 球面内でも、宝玉との間に通常壁があるピクセルには光が届かないため
        // くり抜かれない (遮蔽体の影がそのままの形で壁として残る)。
        if (!IsOrbLightReaching(input.WorldPos, input.WorldNormal, orb.xyz, orb.w, i,
                                (uint) GetMaterialCB().OrbOcclusionAtlasIndex,
                                GetMaterialCB().OrbOcclusionNearZ, GetMaterialCB().OrbOcclusionEnabled))
        {
            continue;
        }
        
        if (edgeDist < 0.0f)
        {
            isInsideCutout = true;
            break;
        }
        minEdgeDist = min(minEdgeDist, edgeDist);
    }
    
    // 球面内部: 完全くり抜き・ディスカード (奥をそのまま透過視認)
    if (isInsideCutout)
    {
        attr.OpacityMask = 0.0f;
        discard;
        return;
    }
    
    // 球面境界リング発光ファクター (R <= dist <= R + GetMaterialCB().DissolveEdge)
    float edgeFactor = 0.0f;
    if (minEdgeDist < GetMaterialCB().DissolveEdge)
    {
        float t = 1.0f - saturate(minEdgeDist / GetMaterialCB().DissolveEdge);
        edgeFactor = t * t;
    }

    // ------------------------------------------------------------
    // 2. 元のオブジェクトのテクスチャ（石積みレンガ構造・目地・石肌）
    // ------------------------------------------------------------
    float2 brickCoord = float2(input.WorldPos.x + input.WorldPos.z, input.WorldPos.y) * float2(1.8f, 3.2f);
    float brickRow = floor(brickCoord.y);
    if (fmod(abs(brickRow), 2.0f) >= 0.5f) {
        brickCoord.x += 0.5f;
    }
    float2 brickCell = frac(brickCoord);
    float2 mortarDist = min(brickCell, 1.0f - brickCell);
    float mortar = smoothstep(0.04f, 0.12f, min(mortarDist.x * 1.8f, mortarDist.y));
    
    float brickHash = frac(sin(dot(floor(brickCoord), float2(12.9898f, 78.233f))) * 43758.5453f);
    float stoneGrain = frac(sin(dot(input.WorldPos * 28.0f, float3(23.14f, 65.43f, 41.29f))) * 43758.5453f) * 0.06f;
    
    // 元のオブジェクトのカラー (GetMaterialCB().DarkColor) を100%主軸として鮮明に反映した石材ベース
    float3 mortarBase = GetMaterialCB().DarkColor.rgb * 0.65f;
    float3 brickStoneBase = GetMaterialCB().DarkColor.rgb * (1.0f + brickHash * 0.20f + stoneGrain);
    float3 originalStone = lerp(mortarBase, brickStoneBase, mortar);

    // ------------------------------------------------------------
    // 3. 反転マテリアルのオーラ（紫〜シアンの優美なエーテル霧）の合成
    // ------------------------------------------------------------
    float auraNoise = CalcInversionAuraNoise(input.WorldPos, input.TotalTime);
    
    // 神秘的な反転オーラ色（深淵アメジスト〜エーテルシアン）
    float3 auraAmethyst = float3(0.28f, 0.08f, 0.42f);
    float3 auraCyan     = float3(0.08f, 0.40f, 0.58f);
    float3 auraColor    = lerp(auraAmethyst, auraCyan, auraNoise);
    
    // 元のオブジェクトの固有色 (赤アーチ・木製扉・レンガ等) を主軸として保つ。
    //
    // オーラ色を素の加算で乗せると、暗い素材ほど加算分の比重が大きくなって
    // 元の色が紫〜シアンに塗り潰されてしまう (石材の濃紺 0.16 に対し
    // 加算分が 0.08 前後になり、色相ごと持っていかれる)。
    // そこで色味は「元の色に対する乗算による変調」で与え、加算はもとの明度に
    // 比例させて、暗い素材が染まらないようにする。
    float auraRatio = 0.12f + auraNoise * 0.08f;
    float3 auraTint = lerp(float3(0.86f, 0.72f, 1.06f), float3(0.74f, 1.02f, 1.10f), auraNoise);
    float originalLum = dot(originalStone, float3(0.299f, 0.587f, 0.114f));
    float3 compositeColor =
        originalStone * lerp(float3(1.0f, 1.0f, 1.0f), auraTint, saturate(auraRatio * 3.0f))
        + auraColor * (auraRatio * originalLum * 1.1f);
    attr.BaseColor = compositeColor + (GetMaterialCB().LitColor.rgb * edgeFactor * 0.5f);

    // ------------------------------------------------------------
    // 4. 一部が僅かに発光したアニメーション（レンガ間を這うエネルギー脈）
    // ------------------------------------------------------------
    // ノイズから細い神秘的なルーンエネルギー脈を抽出
    float veinNoise = ValueNoise3D(input.WorldPos * 3.5f + float3(0.0f, WrapDrift(-input.TotalTime * 0.45f), 0.0f));
    float veinFilament = pow(saturate(1.0f - abs(veinNoise - 0.5f) * 3.6f), 2.2f);
    
    // ゆっくり呼吸・明滅するアニメーション脈動
    float pulse1 = sin(input.TotalTime * 2.4f + input.WorldPos.y * 2.2f) * 0.5f + 0.5f;
    float pulse2 = cos(input.TotalTime * 1.5f + (input.WorldPos.x + input.WorldPos.z) * 1.6f) * 0.5f + 0.5f;
    float veinBreathing = pulse1 * 0.65f + pulse2 * 0.35f;
    
    // 一部が僅かに発光するシアン〜紫の微光脈。
    //
    // 以前は固定のシアン/紫を強度 1.6 で全面に乗せていたため、
    // 発光がベースカラーを完全に覆い、どの素材の反転壁も同じ青紫に見えていた。
    // 強度を落としたうえで、脈の色そのものに元の素材色を混ぜ、
    // 「その素材が光っている」ように見せる。
    float3 mysticVein = lerp(float3(0.40f, 0.10f, 0.70f), float3(0.15f, 0.85f, 1.20f), pulse1);
    float3 veinColor = lerp(mysticVein, mysticVein * 0.35f + originalStone * 1.6f, 0.5f);
    float3 subtleVeinEmissive = veinColor * veinFilament * (veinBreathing * 0.7f + 0.3f) * 0.55f;
    
    // オーラ全体の微細な発光底光り
    float3 auraAmbientEmissive = float3(0.04f, 0.015f, 0.08f) * auraNoise;
    
    // 輪郭フレネル発光
    float3 rimEmissive = float3(0.10f, 0.45f, 0.70f) * rim * (sin(input.TotalTime * 2.2f) * 0.15f + 0.85f) * 0.35f;
    
    // 境界リングの光子発光 (Spherical Dissolve Edge)
    float pulseEdge = sin(input.TotalTime * GetMaterialCB().PulseSpeed) * 0.15f + 0.85f;
    float3 ringEmissive = GetMaterialCB().LitColor.rgb * (edgeFactor * 4.0f + rim * edgeFactor * 2.5f) * pulseEdge * GetMaterialCB().EmissiveBoost;
    
    attr.Emissive = subtleVeinEmissive + auraAmbientEmissive + rimEmissive + ringEmissive;

    // ------------------------------------------------------------
    // 5. PBR マテリアル属性
    // ------------------------------------------------------------
    attr.Normal = N;
    attr.Roughness = lerp(0.55f, 0.15f, saturate(auraNoise * 0.6f + edgeFactor * 0.4f));
    attr.Metallic = lerp(0.06f, 0.28f, auraNoise);
    attr.AO = lerp(0.60f, 1.0f, mortar);
    attr.OpacityMask = 1.0f;
}

void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
}
