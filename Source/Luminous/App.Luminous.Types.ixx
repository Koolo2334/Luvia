module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <string>
#include <vector>
#include <cstdint>
#include <cmath>

export module App.Luminous.Types;

export namespace App::Luminous {

    using namespace DirectX;

    // ====================================================================
    // グリッド寸法基準定数（FBXアセット実測値に完全準拠）
    // ====================================================================
    constexpr float GRID_CELL_SIZE = 2.106f;       // 1セル幅・奥行き (m)
    constexpr float GRID_FLOOR_HEIGHT = 3.063f;    // 1フロア階層高・壁高 (m)
    constexpr float PLAYER_EYE_HEIGHT = 1.60f;     // FPS視点カメラアイレベル (m)
    constexpr float PLAYER_RADIUS = 0.35f;         // プレイヤー円柱当たり判定半径 (m)
    constexpr float DEFAULT_ORB_RADIUS = GRID_CELL_SIZE * 2.0f;   // 宝玉の標準光線届き半径 (2セル分 = 2.106 * 3)
    constexpr float INTERACT_DISTANCE = 2.50f;     // 台座・ゴールへのインタラクト有効距離 (m)
    // 台座ソケットの宝玉スナップ高さ (m)。
    // 宝玉メッシュの原点は底面にあり、台座上面は 1.017m。浮遊の揺れ (±1.2cm) を含めても
    // 宝玉が台座にめり込まないよう、上面より少し上に置く。
    constexpr float PEDESTAL_SOCKET_HEIGHT = 1.05f;
    constexpr float ORB_HOLD_RADIUS = 0.18f;       // 手持ち宝玉の球コリジョン半径 (m)

    // 1 ステージに配置できる宝玉の上限。
    // 反転判定・遮蔽アトラス (App::Luminous::ORB_OCCLUSION_MAX_ORBS) のスロット数と
    // 一致させること。超過すると描画に反映されない宝玉が生まれてしまう。
    constexpr int32_t MAX_STAGE_ORBS = 8;

    // ====================================================================
    // ゲームプレイ外部設定変数 (仕様書・外部設定可能パラメータ)
    // ====================================================================
    struct LuminousGameplayConfig {
        // 1. 移動速度 (m/s)
        float MoveSpeed = 4.20f;
        // 2. 環境光の強さ
        float AmbientLightIntensity = 0.25f;
        // 3. オーブの光の強さ (PointLight Intensity)
        float OrbLightIntensity = 32.0f;
        // 4. オーブの影響範囲 (光線半径 m)
        float OrbInfluenceRadius = DEFAULT_ORB_RADIUS;
        // 5. 画面の揺れの強さ (ヘッドボブ振幅倍率, 1.0 = 標準)
        float CameraShakeIntensity = 1.0f;
        // 6. 画面の揺れの速さ (ヘッドボブ周波数倍率, 1.0 = 標準)
        float CameraShakeSpeed = 0.3f;
        // 7. オーブの光の影の描写範囲 (NearClip m)
        float OrbLightNearClip = 0.10f;
        // 7b. 壁掛け松明・燭台・宝箱など通常光源の影の描写範囲 (NearClip m)。
        //     大きいと光源のすぐ近くの壁や小物が影を落とさなくなる。
        float PropLightNearClip = 0.05f;
        // 8. インタラクトの距離 (有効範囲 m)
        float InteractionDistance = INTERACT_DISTANCE;
        // 9. コントローラー右スティックの視点感度 (rad/s, スティックを倒し切ったとき)
        float PadLookSpeedX = 3.4f;   // 水平 (左右)
        float PadLookSpeedY = 2.8f;   // 垂直 (上下)
        // 10. 右スティックの応答カーブ (1.0 = 線形。大きいほど小さな倒し量で細かく狙える)
        float PadLookResponseExponent = 1.0f;
    };
    inline LuminousGameplayConfig g_LuminousConfig;

    // ====================================================================
    // 列挙型
    // ====================================================================
    enum class AssetCategory : uint8_t {
        Floor,
        Wall,
        Stairs,
        Pedestal,
        Special,
        Prop
    };

    enum class PlacementType : uint8_t {
        CellSnap,    // セル面配置 (床・階段・召喚陣)
        EdgeSnap,    // セル辺配置 (壁・フェンス・窓)
        FreeAttach   // 接地自由配置 (台座・小物・宝箱)
    };

    enum class MaterialPhase : uint8_t {
        Normal,      // 通常マテリアル (剛体壁 / 歩行可能床)
        Phase        // 反転マテリアル (光で透過壁 / 光で穴化床)
    };

    enum class CellEdge : uint8_t {
        North,       // +Z 辺
        South,       // -Z 辺
        East,        // +X 辺
        West         // -X 辺
    };

    enum class EditorToolMode : uint8_t {
        Paint,       // 編集・ブラシ配置モード
        Select       // 選択・インスペクタモード
    };

    enum class EditorCameraMode : uint8_t {
        TopDown,     // 正射影トップダウンビュー
        Flycam3D     // 3D自由飛行ビュー (WASDQE + Noclip)
    };

    // ====================================================================
    // グリッド座標構造体
    // ====================================================================
    struct GridCoord {
        int32_t X = 0;
        int32_t Z = 0;
        int32_t Floor = 0;

        bool operator==(const GridCoord& other) const {
            return X == other.X && Z == other.Z && Floor == other.Floor;
        }
        bool operator!=(const GridCoord& other) const {
            return !(*this == other);
        }
    };

    struct EdgeCoord {
        int32_t X = 0;
        int32_t Z = 0;
        int32_t Floor = 0;
        CellEdge Edge = CellEdge::North;

        bool operator==(const EdgeCoord& other) const {
            return X == other.X && Z == other.Z && Floor == other.Floor && Edge == other.Edge;
        }
    };

    // ====================================================================
    // 配置オブジェクトデータ (エディタ・シリアライズ・実行時共通)
    // ====================================================================
    // ====================================================================
    // スポーン方角 (StartDais 専用・4 方向)
    // Yaw = 0 が +Z (North) なので、時計回りに 90 度ずつ
    // ====================================================================
    enum class SpawnFacing : int32_t {
        North = 0,  // +Z
        East  = 1,  // +X
        South = 2,  // -Z
        West  = 3   // -X
    };

    inline float SpawnFacingToYawDegrees(SpawnFacing facing) {
        switch (facing) {
        case SpawnFacing::East:  return 90.0f;
        case SpawnFacing::South: return 180.0f;
        case SpawnFacing::West:  return 270.0f;
        case SpawnFacing::North:
        default:                 return 0.0f;
        }
    }

    inline const char* SpawnFacingLabel(SpawnFacing facing) {
        switch (facing) {
        case SpawnFacing::East:  return "East (+X)";
        case SpawnFacing::South: return "South (-Z)";
        case SpawnFacing::West:  return "West (-X)";
        case SpawnFacing::North:
        default:                 return "North (+Z)";
        }
    }

    struct PlacedObject {
        uint64_t InstanceId = 0;
        std::string AssetId;
        AssetCategory Category = AssetCategory::Floor;
        PlacementType Placement = PlacementType::CellSnap;
        int32_t FloorIndex = 0;

        GridCoord Cell = { 0, 0, 0 };
        CellEdge Edge = CellEdge::North;

        XMFLOAT3 Position = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 Rotation = { 0.0f, 0.0f, 0.0f }; // Euler angles in degrees
        XMFLOAT3 Scale = { 1.0f, 1.0f, 1.0f };

        MaterialPhase Phase = MaterialPhase::Normal;
        bool HasInitialOrb = false; // 台座専用: ステージ開始時に宝玉をセットするか

        // StartDais 専用: スポーン時にプレイヤーが向く方角
        SpawnFacing Facing = SpawnFacing::North;

        // 光源設定 (壁掛けランプ・松明等)
        bool HasLight = false;
        XMFLOAT3 LightColor = { 1.0f, 0.70f, 0.30f };
        float LightIntensity = 20.0f;
        float LightRadius = 10.0f;
    };

    // ====================================================================
    // ステージ定義
    // ====================================================================
    struct FloorConfig {
        int32_t FloorIndex = 0;
        std::string Name = "1F";
        int32_t GridWidth = 16;
        int32_t GridDepth = 16;
        bool Visible = true;
    };

    struct StageValidationResult {
        bool IsValid = true;
        int32_t StartPointCount = 0;
        int32_t GoalPointCount = 0;
        int32_t InitialOrbCount = 0;
        std::vector<std::string> Errors;
        std::vector<std::string> Warnings;
    };

    // ====================================================================
    // 宝玉と台座の実行時状態
    // ====================================================================
    enum class OrbCarrier : uint8_t {
        Player,      // プレイヤーが手持ち中
        Pedestal,    // 台座にスナップ中
        Unbound      // 空中・浮遊
    };

    struct OrbRuntimeState {
        uint64_t OrbId = 0;
        OrbCarrier Carrier = OrbCarrier::Player;
        uint64_t PedestalInstanceId = 0;
        XMFLOAT3 WorldPosition = { 0.0f, 0.0f, 0.0f };
        float Radius = DEFAULT_ORB_RADIUS;
        float ColorRGB[3] = { 1.0f, 0.88f, 0.25f }; // 金色コア

        // 手持ちViewmodel スウェイ慣性遅延 (Phase 4)
        XMFLOAT2 SwayOffset = { 0.0f, 0.0f };
        float LastYaw = 0.0f;
        float LastPitch = 0.0f;
        bool HasPrevRot = false;

        // 台座脱着の0.25秒放物線アニメーション (仕様書 4.2 / Phase 4)
        bool IsAnimating = false;
        float AnimTime = 0.0f;
        float AnimDuration = 0.25f;
        XMFLOAT3 AnimStartPos = { 0.0f, 0.0f, 0.0f };
        XMFLOAT3 AnimEndPos = { 0.0f, 0.0f, 0.0f };
        OrbCarrier TargetCarrier = OrbCarrier::Player;
        uint64_t TargetPedestalId = 0;
    };

    // ====================================================================
    // 座標変換ヘルパー
    // ====================================================================
    inline XMFLOAT3 GridToWorldCenter(int32_t gx, int32_t gz, int32_t floor) {
        return XMFLOAT3(
            (static_cast<float>(gx) + 0.5f) * GRID_CELL_SIZE,
            static_cast<float>(floor) * GRID_FLOOR_HEIGHT,
            (static_cast<float>(gz) + 0.5f) * GRID_CELL_SIZE
        );
    }

    inline GridCoord WorldToGrid(const XMFLOAT3& worldPos) {
        int32_t gx = static_cast<int32_t>(std::floor(worldPos.x / GRID_CELL_SIZE));
        int32_t gz = static_cast<int32_t>(std::floor(worldPos.z / GRID_CELL_SIZE));
        int32_t floor = static_cast<int32_t>(std::floor((worldPos.y + 0.1f) / GRID_FLOOR_HEIGHT));
        return GridCoord{ gx, gz, floor };
    }

    inline XMFLOAT3 EdgeToWorld(int32_t gx, int32_t gz, int32_t floor, CellEdge edge, float& outYawDeg) {
        float cx = (static_cast<float>(gx) + 0.5f) * GRID_CELL_SIZE;
        float cy = static_cast<float>(floor) * GRID_FLOOR_HEIGHT;
        float cz = (static_cast<float>(gz) + 0.5f) * GRID_CELL_SIZE;
        float half = GRID_CELL_SIZE * 0.5f;

        switch (edge) {
        case CellEdge::North:
            outYawDeg = 0.0f;
            return XMFLOAT3(cx, cy, cz + half);
        case CellEdge::South:
            outYawDeg = 0.0f;
            return XMFLOAT3(cx, cy, cz - half);
        case CellEdge::East:
            outYawDeg = 90.0f;
            return XMFLOAT3(cx + half, cy, cz);
        case CellEdge::West:
            outYawDeg = 90.0f;
            return XMFLOAT3(cx - half, cy, cz);
        }
        outYawDeg = 0.0f;
        return XMFLOAT3(cx, cy, cz);
    }

    // ====================================================================
    // 球状くり抜きコリジョン構造体 (Phase 5)
    // ====================================================================
    // 反転床セルの遮蔽マスク解像度 (16x16 = 1 サブセル約 13cm)
    constexpr int PHASE_MASK_DIM = 16;

    struct CircularHole {
        XMFLOAT2 CenterXZ = { 0.0f, 0.0f };
        float Radius = 0.0f;
        int32_t FloorIndex = 0;

        // Phase 2: 遮蔽体の影を反映したサブセル単位の「光が届いている」ビットマスク。
        // 立っているか落ちるかの判定はビットテスト 1 回 O(1) で完了する。
        uint64_t FloorInstanceId = 0;
        XMFLOAT2 CellMinXZ = { 0.0f, 0.0f };
        float CellSize = GRID_CELL_SIZE;
        uint64_t LitMask[4] = { 0, 0, 0, 0 };   // 16 x 16 = 256 bit
        bool HasMask = false;

        void SetLit(int ix, int iz) {
            const int bit = iz * PHASE_MASK_DIM + ix;
            LitMask[bit >> 6] |= (1ull << (bit & 63));
        }

        // サブセル (ix, iz) に宝玉の光が届いているか。範囲外は「届かない」。
        // 当たり判定はこのビット集合を穴の実体としてそのまま使う。
        bool IsLitCell(int ix, int iz) const {
            if (ix < 0 || ix >= PHASE_MASK_DIM || iz < 0 || iz >= PHASE_MASK_DIM) return false;
            const int bit = iz * PHASE_MASK_DIM + ix;
            return ((LitMask[bit >> 6] >> (bit & 63)) & 1ull) != 0ull;
        }

        // (x, z) のサブセルに宝玉の光が届いているか。マスク未生成なら従来通り「届く」。
        bool IsLitAt(float x, float z) const {
            if (!HasMask) return true;
            const float u = (x - CellMinXZ.x) / CellSize;
            const float v = (z - CellMinXZ.y) / CellSize;
            if (u < 0.0f || u >= 1.0f || v < 0.0f || v >= 1.0f) return true;
            int ix = static_cast<int>(u * PHASE_MASK_DIM);
            int iz = static_cast<int>(v * PHASE_MASK_DIM);
            if (ix < 0) ix = 0; else if (ix >= PHASE_MASK_DIM) ix = PHASE_MASK_DIM - 1;
            if (iz < 0) iz = 0; else if (iz >= PHASE_MASK_DIM) iz = PHASE_MASK_DIM - 1;
            const int bit = iz * PHASE_MASK_DIM + ix;
            return ((LitMask[bit >> 6] >> (bit & 63)) & 1ull) != 0ull;
        }
    };

    struct WallCutoutCollider {
        uint64_t WallInstanceId = 0;
        bool IsFullyOpen = false;
        bool IsFullySolid = false;
        int32_t FloorIndex = 0;

        // Phase 2: 遮蔽体の影を反映した残存剛体区間。
        // 遮蔽が無ければ左右ウィングの 2 区間だが、壁の途中に影が落ちると
        // 開口が分断されるため可変個の区間として保持する。
        static constexpr int MAX_SOLID_SPANS = 8;
        int32_t SolidSpanCount = 0;
        XMFLOAT3 SolidMin[MAX_SOLID_SPANS] = {};
        XMFLOAT3 SolidMax[MAX_SOLID_SPANS] = {};

        void AddSolidSpan(const XMFLOAT3& minB, const XMFLOAT3& maxB) {
            if (SolidSpanCount >= MAX_SOLID_SPANS) return;
            SolidMin[SolidSpanCount] = minB;
            SolidMax[SolidSpanCount] = maxB;
            ++SolidSpanCount;
        }
    };

    struct PedestalColliderBake {
        uint64_t PedestalInstanceId = 0;
        std::vector<CircularHole> Holes;
        std::vector<WallCutoutCollider> Walls;
    };
}
