// ====================================================================
// MysticParticle.hlsli - Ancient Ruins Floating Mana & Spell Particles
// ====================================================================
#include "../StandardMaterialCommon.hlsli"
#include "../CharacterMaterialCommon.hlsli"

#include "../MaterialParams.hlsli"

// マテリアルのパラメータ (1 本のバッファから読む。GetMaterialCB() で取得する)
struct MaterialCBLayout
{
    float4 ParticleColor;      // rgb = particle tint, a = base opacity
    float  SparkleSpeed;       // Twinkle pulsation speed
    float  SparkleScale;       // Spatial frequency
    float  EmissiveIntensity;  // Bloom emission strength
    float  Padding;
};
MaterialCBLayout GetMaterialCB() { return MaterialData.Load<MaterialCBLayout>(g_MaterialOffset); }

void WriteGBuffer(StandardMaterialInput input, inout StandardMaterialAttributes attr)
{
    // Soft radial falloff from center (0.5, 0.5)
    float2 centerDist = input.UV0 - float2(0.5f, 0.5f);
    float distSq = dot(centerDist, centerDist);
    float radial = saturate(1.0f - distSq * 4.0f);
    radial = pow(radial, 2.2f);

    // Dynamic shimmer based on world position and time
    float twinkle = 0.75f + 0.25f * sin(input.TotalTime * max(GetMaterialCB().SparkleSpeed, 0.5f) + 
                                       dot(input.WorldPos, float3(1.7f, 2.9f, 3.3f)) * max(GetMaterialCB().SparkleScale, 0.1f));

    attr.BaseColor = GetMaterialCB().ParticleColor.rgb;
    attr.Normal = float3(0.0f, 1.0f, 0.0f);
    attr.AO = 1.0f;
    attr.Roughness = 0.8f;
    attr.Metallic = 0.0f;
    
    // High emissive core for bloom aura
    attr.Emissive = GetMaterialCB().ParticleColor.rgb * (GetMaterialCB().EmissiveIntensity * radial * twinkle);
    attr.OpacityMask = saturate(GetMaterialCB().ParticleColor.a * radial * twinkle);
}

void ShadeCharacter(CharacterMaterialInput input, inout CharacterMaterialAttributes attr)
{
}
