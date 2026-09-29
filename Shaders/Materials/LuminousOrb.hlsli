// ====================================================================
// LuminousOrb.hlsli - Luminous Shift 宝玉マテリアルシェーダー
// ====================================================================
// 特徴:
// 1. 多面体クリスタル外殻の多層フレネル屈折と色収差（プリズム分散）
// 2. 内部マナコアの呼吸するようなサイン波脈動発光
// 3. 高輝度HDRエミッシブ値によるブルームポストプロセス連携（適正階調保持）
// ====================================================================

#include "../StandardMaterialCommon.hlsli"
#include "../CharacterMaterialCommon.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    float4 CoreColor;        // rgb = マナコア基本発光色 (例: [1.0, 0.85, 0.25] 黄金色), a = 中心不透明度
    float4 RimColor;         // rgb = 外殻クリスタルのリム色 (例: [0.25, 0.85, 1.0] 蒼穹色), a = リム最大不透明度
    float FresnelPower;      // フレネルべき乗 (例: 3.5)
    float PulseSpeed;        // コア脈動速度 (例: 2.5 * 3.14159)
    float Dispersion;        // プリズム色収差のズレ幅 (例: 0.20)
    float EmissiveIntensity; // ブルームHDR強度 (例: 2.0)
};
MaterialCBLayout GetMaterialCB() { return MaterialData.Load<MaterialCBLayout>(g_MaterialOffset); }

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    float3 V = normalize(CameraPosition - input.WorldPos);
    float3 N = normalize(input.WorldNormal);
    float VdotN = abs(dot(V, N));
    
    // 1. プリズム色収差（RGBごとの異なるフレネル屈折率）
    float rimR = pow(saturate(1.0f - VdotN), max(GetMaterialCB().FresnelPower * (1.0f - GetMaterialCB().Dispersion), 0.1f));
    float rimG = pow(saturate(1.0f - VdotN), max(GetMaterialCB().FresnelPower, 0.1f));
    float rimB = pow(saturate(1.0f - VdotN), max(GetMaterialCB().FresnelPower * (1.0f + GetMaterialCB().Dispersion), 0.1f));
    float3 chromaticRim = float3(rimR, rimG, rimB) * GetMaterialCB().RimColor.rgb;
    
    // 2. 内部マナコアの呼吸脈動 (有機的パルス)
    float pulse = 1.0f + 0.15f * sin(input.TotalTime * GetMaterialCB().PulseSpeed);
    
    // 3. 多面体のファセット微小スパークル (Facet Sparkle)
    float sparkle = saturate(sin(dot(input.WorldPos * 10.0f, float3(12.9898f, 78.233f, 37.719f)) + input.TotalTime * 3.0f));
    sparkle = pow(sparkle, 12.0f) * 0.35f;
    
    // 4. ベースカラーとPBR属性
    attr.BaseColor = lerp(GetMaterialCB().CoreColor.rgb * 0.4f, GetMaterialCB().RimColor.rgb * 0.8f, rimG);
    attr.Normal = N;
    attr.Roughness = 0.04f;  // 磨き上げられた高屈折クリスタル
    attr.Metallic = 0.02f;
    attr.AO = 1.0f;
    
    // 5. HDRブルームエミッシブ: 中心コア発光 + 外殻プリズム発光 + ファセット閃光
    float3 coreEmissive = GetMaterialCB().CoreColor.rgb * (pulse * (1.0f - rimG * 0.4f));
    float3 shellEmissive = chromaticRim * 1.2f;
    float3 totalEmissive = (coreEmissive + shellEmissive + sparkle.xxx * GetMaterialCB().CoreColor.rgb) * GetMaterialCB().EmissiveIntensity;
    attr.Emissive = totalEmissive;
    
    // 6. 不透明度: 外殻リムとコアの二重構造
    attr.OpacityMask = saturate(GetMaterialCB().CoreColor.a + (GetMaterialCB().RimColor.a - GetMaterialCB().CoreColor.a) * rimG);
}

void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
}
