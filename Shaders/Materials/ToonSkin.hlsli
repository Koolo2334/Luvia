// ====================================================================
// SimpleToon.hlsli
// 濃影＆高彩度 / 強発光日向 / 強発光＆スムーズカラーブレンドリム版
// ====================================================================
#include "../CharacterMaterialCommon.hlsli"
#include "../StandardMaterialCommon.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    uint AlbedoMapIndex; // 基本アルベドテクスチャ
};
MaterialCBLayout GetMaterialCB() { return MaterialData.Load<MaterialCBLayout>(g_MaterialOffset); }

// アンチエイリアス処理された滑らかなセル境界関数
float SmoothToonStep(float edge, float x, float smoothness)
{
    return smoothstep(edge - smoothness, edge + smoothness, x);
}

// --------------------------------------------------------------------
// 1. 高彩度・強発光・カラーブレンド対応リムライト (距離適応型)
// --------------------------------------------------------------------
float3 ComputeHighColorSoftGlowRim(
    float3 N, float3 V, float3 L,
    float3 lightColor, float effectiveIntensity,
    float3 albedo, float viewDistance)
{
    if (effectiveIntensity < 1.0f)
    {
        return float3(0, 0, 0);
    }

    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));

    // カメラ距離に応じたリム幅の調整
    float distFactor = saturate((viewDistance - 5.0f) / 20.0f);
    float rimExponent = lerp(3.0f, 1.2f, distFactor);

    float rimRaw = pow(1.0f - NdotV, rimExponent);

    // 境界をマイルドにぼかす
    float rimEdgeThreshold = lerp(0.50f, 0.25f, distFactor);
    float rimShape = SmoothToonStep(rimEdgeThreshold, rimRaw, 0.08f) * saturate(NdotL * 0.8f + 0.2f);

    if (rimShape <= 0.0f)
        return float3(0, 0, 0);

    // 高彩度化処理 (彩度強調倍率を向上)
    float3 saturatedColor = albedo * lightColor;
    float luminance = dot(saturatedColor, float3(0.2126f, 0.7152f, 0.0722f));
    
    float3 highSatRimColor = lerp(float3(luminance, luminance, luminance), saturatedColor, 2.2f);
    highSatRimColor = max(highSatRimColor, float3(0, 0, 0));

    // ★ リムライトの発光をさらに強化 (5.0倍ブースト)
    return highSatRimColor * rimShape * clamp(pow(effectiveIntensity * 0.25f - 2.0f, 3.0f), 0.0f, 200.0f);
}

// --------------------------------------------------------------------
// 2. 1段影（暗め＆高彩度影）＋ 疑似SSS ＋ 強日向エミッシブ
// --------------------------------------------------------------------
float3 Compute1StepToonDiffuse(
    float3 N, float3 L, float3 albedo,
    float3 lightColor, float lightIntensity)
{
    //lightColor = lightColor * 0.5f + 0.5f;
    
    float NdotL = dot(N, L);
    float halfLambert = saturate(NdotL * 0.5f + 0.5f);

    // 1段影の判定
    float shadowThreshold = 0.45f;
    float shadowFactor = SmoothToonStep(shadowThreshold, halfLambert, 0.05f);

    // ★ 影を少し暗く・かつ彩度を高く調整
    float sssMask = smoothstep(0.0f, 0.5f, shadowFactor) * smoothstep(1.0f, 0.5f, shadowFactor);
    shadowFactor = smoothstep(0.1f, 0.9f, shadowFactor);
    
    sssMask = smoothstep(0.0f, 0.5f, sssMask);
    
    float albedoLum = dot(albedo, float3(0.2126f, 0.7152f, 0.0722f));
    float3 baseShadowColor = albedo * 0.3f;
    float3 shadowColor = max(lerp(float3(albedoLum, albedoLum, albedoLum) * 0.35f, baseShadowColor, 2.0f), float3(0, 0, 0));
    float3 toonDiffuse = lerp(shadowColor, albedo, shadowFactor);

    // 疑似SSS (影の境目の血色感)
    float3 sssColor = float3(1.4f, 0.8f, 0.7f);
    toonDiffuse = lerp(toonDiffuse, sssColor * toonDiffuse, sssMask);

    float3 lightContrib = lerp(float3(1, 1, 1), lightColor, 0.5f) * min(lightIntensity, 1.2f);
    toonDiffuse *= lightContrib;
    
    float lum = dot(toonDiffuse, float3(0.2126f, 0.7152f, 0.0722f));
    // 日向の色調を補正し、明るい部分を発光させる
    float emissiveBoost = max(pow(lum + 0.1f, 2.0f), 0.8f);
    
    toonDiffuse *= emissiveBoost;
    
    return toonDiffuse;
}


void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
    // ----------------------------------------------------------------
    // 1. 基本プロパティの準備
    // ----------------------------------------------------------------
    float4 albedoTex = g_AllTextures[GetMaterialCB().AlbedoMapIndex].Sample(AnisoSampler, input.UV0);
    float3 albedo = albedoTex.rgb;
    albedo *= float3(1.0f, 0.8f, 0.8f); // 赤みを少し強める

    float3 N = normalize(input.WorldNormal);
    float3 viewVec = CameraPosition - input.WorldPos;
    float viewDist = length(viewVec);
    float3 V = viewVec / max(viewDist, 0.0001f);

    // ----------------------------------------------------------------
    // 2. ディレクショナルライト (セル影 ＆ 疑似SSS ＆ 日向発光)
    // ----------------------------------------------------------------
    float3 L_dir = normalize(-DirLightDirection);
    
    float3 mainDiffuse = Compute1StepToonDiffuse(
        N, L_dir, albedo, DirLightColor, DirLightIntensity
    );

    // ----------------------------------------------------------------
    // 3. リムライト用：上位2つの強ライトを検索してスムーズにブレンド
    // ----------------------------------------------------------------
    float3 primaryLightDir = L_dir;
    float3 primaryLightColor = DirLightColor;
    float primaryIntensity = DirLightIntensity;

    float3 secondaryLightDir = L_dir;
    float3 secondaryLightColor = DirLightColor;
    float secondaryIntensity = 0.0f;

    // Point Lights 検索
    for (uint i = 0; i < NumPointLights; ++i)
    {
        PointLight pt = PointLights[i];
        float3 lightVec = pt.Position - input.WorldPos;
        float dist = length(lightVec);
        if (dist >= pt.Radius)
            continue;

        float3 L = lightVec / dist;
        float att = 1.0f / (dist * dist + 0.0001f);
        float falloff = saturate(1.0f - (dist / pt.Radius));
        float intensity = att * (falloff * falloff) * pt.Intensity;

        if (intensity > primaryIntensity)
        {
            secondaryIntensity = primaryIntensity;
            secondaryLightDir = primaryLightDir;
            secondaryLightColor = primaryLightColor;

            primaryIntensity = intensity;
            primaryLightDir = L;
            primaryLightColor = pt.Color;
        }
        else if (intensity > secondaryIntensity)
        {
            secondaryIntensity = intensity;
            secondaryLightDir = L;
            secondaryLightColor = pt.Color;
        }
    }

    // Spot Lights 検索
    for (uint j = 0; j < NumSpotLights; ++j)
    {
        SpotLight sl = SpotLights[j];
        float3 lightVec = sl.Position - input.WorldPos;
        float dist = length(lightVec);
        if (dist >= sl.Radius)
            continue;

        float3 L = lightVec / dist;
        float att = 1.0f / (dist * dist + 0.0001f);
        float falloff = saturate(1.0f - (dist / sl.Radius));
        float thetaLight = dot(-L, sl.Direction);
        float epsilon = sl.InnerConeAngle - sl.OuterConeAngle;
        float coneIntensity = clamp((thetaLight - sl.OuterConeAngle) / epsilon, 0.0f, 1.0f);
        float intensity = att * (falloff * falloff) * coneIntensity * sl.Intensity;

        if (intensity > primaryIntensity)
        {
            secondaryIntensity = primaryIntensity;
            secondaryLightDir = primaryLightDir;
            secondaryLightColor = primaryLightColor;

            primaryIntensity = intensity;
            primaryLightDir = L;
            primaryLightColor = sl.Color;
        }
        else if (intensity > secondaryIntensity)
        {
            secondaryIntensity = intensity;
            secondaryLightDir = L;
            secondaryLightColor = sl.Color;
        }
    }

    // ★ リムライトのカラーと方向の境界切り替わりをスムーズに補間（ブレンド）
    float totalIntensity = primaryIntensity + secondaryIntensity;
    float blendFactor = (totalIntensity > 0.0001f) ? (secondaryIntensity / totalIntensity) : 0.0f;

    // 2つのライトのベクトルと色をブレンド
    float3 blendedLightDir = normalize(lerp(primaryLightDir, secondaryLightDir, blendFactor * 0.5f));
    float3 blendedLightColor = lerp(primaryLightColor, secondaryLightColor, blendFactor * 0.5f);
    float blendedIntensity = max(primaryIntensity, secondaryIntensity);

    // 強発光リムライトの計算
    float3 finalRim = ComputeHighColorSoftGlowRim(
        N, V, blendedLightDir, blendedLightColor,
        blendedIntensity, albedo, viewDist
    );

    // ----------------------------------------------------------------
    // 4. 最終カラーの出力
    // ----------------------------------------------------------------
    float3 finalHDR = mainDiffuse + finalRim;

    attr.HDRColor = float4(finalHDR, albedoTex.a);
}

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    // フォワードパス描画のため空実装
}