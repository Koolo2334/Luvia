// ====================================================================
// FresnelAura.hlsli
// ====================================================================
#include "../StandardMaterialCommon.hlsli"
#include "../CharacterMaterialCommon.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    float4 BaseColor;        // rgb = tint, a = max rim opacity
    float3 Emissive;         // Emissive tint & strength at rim
    float FresnelPower;      // Rim power (e.g. 3.0)
    float InnerOpacity;      // Center opacity (e.g. 0.02 - crystal clear interior)
    float PulsateSpeed;      // Subtle pulsation speed
    float2 Padding;
};
MaterialCBLayout GetMaterialCB() { return MaterialData.Load<MaterialCBLayout>(g_MaterialOffset); }

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    float3 V = normalize(CameraPosition - input.WorldPos);
    float3 N = normalize(input.WorldNormal);
    float VdotN = abs(dot(V, N));
    float rim = pow(saturate(1.0f - VdotN), max(GetMaterialCB().FresnelPower, 0.1f));
    
    float pulse = 1.0f + 0.10f * sin(input.TotalTime * max(GetMaterialCB().PulsateSpeed, 0.1f));
    
    attr.BaseColor = GetMaterialCB().BaseColor.rgb;
    attr.Normal = N;
    attr.AO = 1.0f;
    attr.Roughness = 0.1f;
    attr.Metallic = 0.0f;
    
    // High rim emissive to trigger bloom at the perimeter, smoothly fading to zero at the interior
    attr.Emissive = GetMaterialCB().Emissive * (pow(rim, 2.0f) * pulse);
    
    // Opacity: perimeter forms glowing shell, smoothly fading to crystal clear interior
    attr.OpacityMask = saturate(GetMaterialCB().BaseColor.a * pow(rim, 1.5f) * pulse);
}

void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
}
