module;
#include <cstdint>
#include <cmath>
#include <DirectXMath.h>

export module App.Luminous.OrbOcclusionData;

export namespace App::Luminous {

    // ================================================================
    // Phase 2: 宝玉遮蔽 (Orb Occlusion) 専用軽量パスの共有定義
    //
    // 汎用 ShadowPass (4Kアトラス・カスケード・PCF・バイアス群) は一切流用せず、
    // 宝玉の光が届くかどうかだけを判定する深度専用アトラスを別に持つ。
    //
    // ステージ上の宝玉は「同時に持ち運べる 1 個」以外は全て台座に固定されるため、
    // 各宝玉ごとにキューブ 6 面ぶんのスロットを確保しておき、
    // 座標が変化したスロットだけを描き直す。台座に挿さったままの宝玉の面は
    // ステージロード直後に一度描かれたきり再描画されない。
    //
    // アトラスは 256px/面 のキューブ 6 面 × 最大 8 宝玉 = 48 面を
    // 8 列 × 6 行に敷き詰めた 2048 x 1536 の単一深度テクスチャ。
    // 90 度視野を 256px で割ると 1 テクセル約 0.35 度、光半径 4.2m の距離でも
    // 約 2.6cm 相当の精度があり、遮蔽判定には十分。
    // ================================================================
    // OrbOcclusionPass の描画対象タグ。
    // 宝玉の光を実際に遮るサブメッシュ (通常の壁、床・天井のスラブ) にだけ付ける。
    // 格子壁・鉄格子・反転マテリアル・台座・小物には付けないこと。
    struct OrbOccluderTag {};

    constexpr uint32_t ORB_OCCLUSION_FACE_SIZE  = 256;
    constexpr uint32_t ORB_OCCLUSION_MAX_ORBS   = 8;
    constexpr uint32_t ORB_OCCLUSION_FACE_COUNT = ORB_OCCLUSION_MAX_ORBS * 6;
    constexpr uint32_t ORB_OCCLUSION_ATLAS_COLS = 8;
    constexpr uint32_t ORB_OCCLUSION_ATLAS_ROWS = 6;
    constexpr uint32_t ORB_OCCLUSION_ATLAS_W    = ORB_OCCLUSION_FACE_SIZE * ORB_OCCLUSION_ATLAS_COLS; // 2048
    constexpr uint32_t ORB_OCCLUSION_ATLAS_H    = ORB_OCCLUSION_FACE_SIZE * ORB_OCCLUSION_ATLAS_ROWS; // 1536

    // 深度射影の近接面。宝玉は台座ソケット/手元の空中に浮いており、
    // 5cm 以内に遮蔽体が存在することは無いため十分小さく取れる。
    constexpr float ORB_OCCLUSION_NEAR_Z = 0.05f;

    // ----------------------------------------------------------------
    // シーン側 → OrbOcclusionPass への要求 (registry.ctx() 経由)
    // ----------------------------------------------------------------
    struct OrbOcclusionRequest {
        DirectX::XMFLOAT4 Orbs[ORB_OCCLUSION_MAX_ORBS] = {}; // xyz: 宝玉中心, w: 光半径 R
        uint32_t Count = 0;
        // ステージ再構築で遮蔽ジオメトリが入れ替わった際にキャッシュを捨てるための世代番号
        uint32_t GeometryRevision = 0;
    };

    // ----------------------------------------------------------------
    // OrbOcclusionPass → マテリアル定数バッファ側への結果
    // ----------------------------------------------------------------
    struct OrbOcclusionInfo {
        uint32_t AtlasIndex = 0;   // g_AllTextures[] へのバインドレス添字
        uint32_t Count = 0;        // 実際にベイクされた宝玉数
        float NearZ = ORB_OCCLUSION_NEAR_Z;
        float FarZ = 1.0f;
        DirectX::XMFLOAT4 Orbs[ORB_OCCLUSION_MAX_ORBS] = {};
        bool Valid = false;
    };

    // 1 宝玉ぶんが同一かどうか (スロット単位のキャッシュ判定用)
    inline bool IsSameOrbEntry(const DirectX::XMFLOAT4& a, const DirectX::XMFLOAT4& b) {
        return std::abs(a.x - b.x) <= 1e-4f
            && std::abs(a.y - b.y) <= 1e-4f
            && std::abs(a.z - b.z) <= 1e-4f
            && std::abs(a.w - b.w) <= 1e-4f;
    }

    // 面スロット (orbIndex * 6 + faceIndex) に対応するアトラス矩形 (UV 正規化)
    inline DirectX::XMFLOAT4 GetOrbOcclusionFaceRect(uint32_t slot) {
        const uint32_t col = slot % ORB_OCCLUSION_ATLAS_COLS;
        const uint32_t row = slot / ORB_OCCLUSION_ATLAS_COLS;
        const float w = 1.0f / static_cast<float>(ORB_OCCLUSION_ATLAS_COLS);
        const float h = 1.0f / static_cast<float>(ORB_OCCLUSION_ATLAS_ROWS);
        return DirectX::XMFLOAT4(static_cast<float>(col) * w, static_cast<float>(row) * h, w, h);
    }

} // namespace App::Luminous
