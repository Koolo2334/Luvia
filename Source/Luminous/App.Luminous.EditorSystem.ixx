module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXMath.h>
#include <vector>
#include <string>
#include <memory>
#include <algorithm>
#include <cmath>

export module App.Luminous.EditorSystem;

import Engine.Input;
import Engine.Math;
import App.Luminous.Types;
import App.Luminous.StageData;

export namespace App::Luminous {

    using namespace DirectX;
    using namespace Engine::Input;

    // ====================================================================
    // Undo / Redo コマンド履歴定義 (Phase 7 バッチ・スワップ対応)
    // ====================================================================
    enum class EditorActionType : uint8_t {
        Add,
        Remove,
        Modify,
        BatchAdd,
        BatchRemove,
        FloorSwap
    };

    struct EditorCommand {
        EditorActionType Action = EditorActionType::Add;
        PlacedObject ObjectData;
        PlacedObject PreviousData; // Modify用
        std::vector<PlacedObject> BatchObjects; // BatchAdd / BatchRemove用
        int32_t FloorA = 0; // FloorSwap用
        int32_t FloorB = 0;
    };

    // ====================================================================
    // ステージエディタ コアシステム (Phase 7 完全準拠)
    // ====================================================================
    class EditorSystem {
    public:
        LuminousStage Stage;
        int32_t CurrentFloor = 0;
        bool ShowOnionSkin = true; // 下の階層のオニオンスキン半透明表示

        // エディタモード (Phase 7)
        EditorToolMode ToolMode = EditorToolMode::Paint;
        EditorCameraMode CameraMode = EditorCameraMode::TopDown;

        // カメラパラメータ (トップダウン正射影ビュー)
        XMFLOAT3 CameraFocus = { 5.265f, 0.0f, 5.265f };
        float CameraDistance = 16.0f;
        float MinCameraDistance = 4.0f;
        float MaxCameraDistance = 60.0f;

        // 3D 自由飛行カメラパラメータ (Flycam / Noclip)
        XMFLOAT3 FlycamPos = { 5.265f, 5.0f, -2.0f };
        float FlycamYaw = 0.0f;
        float FlycamPitch = 25.0f;
        float FlycamSpeed = 8.0f;
        bool IsFlycamLooking = false;

        // ブラシ設定
        std::string SelectedAssetId = "Floor_1x1";
        AssetCategory SelectedCategory = AssetCategory::Floor;
        PlacementType SelectedPlacement = PlacementType::CellSnap;
        MaterialPhase SelectedPhase = MaterialPhase::Normal;
        float BrushYaw = 0.0f;
        float BrushWallFlipYaw = 0.0f; // 0 or 180 deg for EdgeSnap walls
        bool BrushInitialOrb = false; // 台座用

        // カーソル・ホバー状態
        bool HasFloorHit = false;
        XMFLOAT3 HoverWorldPos = { 0.0f, 0.0f, 0.0f };
        GridCoord HoverCell = { 0, 0, 0 };
        CellEdge HoverEdge = CellEdge::North;
        uint64_t HoveredInstanceId = 0;

        // オブジェクト選択状態 (選択モード用)
        uint64_t SelectedInstanceId = 0;

        // ドラッグ連続塗り・連続消しゴム
        bool IsDragging = false;
        bool IsDragErasing = false;
        std::vector<PlacedObject> CurrentDragBatch;
        std::vector<PlacedObject> CurrentDragRemovedBatch;
        GridCoord LastDragCell = { -9999, -9999, -9999 };
        CellEdge LastDragEdge = CellEdge::North;
        DirectX::XMFLOAT3 LastDragWorldPos = { -9999.0f, -9999.0f, -9999.0f };
        GridCoord LastDragRemoveCell = { -9999, -9999, -9999 };
        CellEdge LastDragRemoveEdge = CellEdge::North;

        // エラー・警告通知
        std::string StatusMessage = "Ready";
        float StatusMessageTimer = 0.0f;
        bool HasPlacementConflict = false;

        // Undo / Redo スタック
        std::vector<EditorCommand> UndoStack;
        std::vector<EditorCommand> RedoStack;

        // ファイルパス
        std::string CurrentFilePath = "Assets/Data/Stages/CustomDungeon.json";

        EditorSystem() {
            Stage = LuminousStage::CreateSampleStage();
        }

        // ================================================================
        // ブラシ選択更新
        // ================================================================
        void SelectAsset(const std::string& assetId, AssetCategory category, PlacementType placement) {
            SelectedAssetId = assetId;
            SelectedCategory = category;
            SelectedPlacement = placement;

            // アセットに応じた初期値
            if (SelectedCategory != AssetCategory::Floor && SelectedCategory != AssetCategory::Wall) {
                SelectedPhase = MaterialPhase::Normal;
            }
            if (SelectedAssetId != "Pedestal") {
                BrushInitialOrb = false;
            }
        }

        void RotateBrush() {
            if (SelectedPlacement == PlacementType::EdgeSnap || SelectedCategory == AssetCategory::Wall) {
                BrushWallFlipYaw = (BrushWallFlipYaw < 90.0f) ? 180.0f : 0.0f;
                SetStatusMessage(std::string("Wall face flip: ") + (BrushWallFlipYaw > 90.0f ? "180 deg" : "0 deg"));
            } else {
                BrushYaw = std::fmod(BrushYaw + 90.0f, 360.0f);
                SetStatusMessage("Brush rotated to " + std::to_string(static_cast<int>(BrushYaw)) + " deg");
            }
        }

        void ToggleCameraMode() {
            CameraMode = (CameraMode == EditorCameraMode::TopDown ? EditorCameraMode::Flycam3D : EditorCameraMode::TopDown);
        }

        XMFLOAT3 GetPlacementPosition() const {
            if (SelectedPlacement == PlacementType::CellSnap) {
                XMFLOAT3 pos = GridToWorldCenter(HoverCell.X, HoverCell.Z, CurrentFloor);
                if (SelectedCategory == AssetCategory::Stairs || SelectedAssetId == "Stairs_Straight") {
                    // 階段は2マス占有オブジェクト: 選択したマスとその手前のマスにかけて中央に整列スナップ
                    float rad = XMConvertToRadians(BrushYaw);
                    pos.x += 0.5f * GRID_CELL_SIZE * std::sin(rad);
                    pos.z += 0.5f * GRID_CELL_SIZE * std::cos(rad);
                }
                return pos;
            } else if (SelectedPlacement == PlacementType::EdgeSnap) {
                float edgeYaw = 0.0f;
                XMFLOAT3 pos = EdgeToWorld(HoverCell.X, HoverCell.Z, CurrentFloor, HoverEdge, edgeYaw);
                if (SelectedAssetId == "Prop_Torch") {
                    pos.y += 1.8f; // 壁掛けランプは目の高さ1.8mに配置
                    constexpr float wallOffset = 0.323f; // 壁半厚(0.156m) + 松明台座オフセット(0.167m)
                    switch (HoverEdge) {
                    case CellEdge::North: pos.z -= wallOffset; break;
                    case CellEdge::South: pos.z += wallOffset; break;
                    case CellEdge::East:  pos.x -= wallOffset; break;
                    case CellEdge::West:  pos.x += wallOffset; break;
                    }
                }
                return pos;
            } else {
                return HoverWorldPos;
            }
        }

        float GetPlacementYaw() const {
            if (SelectedAssetId == "Prop_Torch" && SelectedPlacement == PlacementType::EdgeSnap) {
                float torchYaw = 0.0f;
                switch (HoverEdge) {
                case CellEdge::North: torchYaw = 0.0f;   break;
                case CellEdge::South: torchYaw = 180.0f; break;
                case CellEdge::East:  torchYaw = 90.0f;  break;
                case CellEdge::West:  torchYaw = 270.0f; break;
                }
                return std::fmod(torchYaw + BrushWallFlipYaw, 360.0f);
            }
            if (SelectedPlacement == PlacementType::EdgeSnap || SelectedCategory == AssetCategory::Wall) {
                float edgeYaw = 0.0f;
                EdgeToWorld(HoverCell.X, HoverCell.Z, CurrentFloor, HoverEdge, edgeYaw);
                return std::fmod(edgeYaw + BrushWallFlipYaw, 360.0f);
            }
            return BrushYaw;
        }

        bool CheckPlacementConflict() const {
            return HasPlacementConflict;
        }

        // ================================================================
        // カメラ操作 (パン & ホイールズーム)
        // ================================================================
        void UpdateCamera(float deltaTime) {
            // ホイールズーム
            float wheel = Input::GetMouseWheel();
            if (wheel != 0.0f) {
                CameraDistance -= wheel * 2.0f;
                CameraDistance = std::clamp(CameraDistance, MinCameraDistance, MaxCameraDistance);
            }

            // 中ボタンドラッグまたは右ボタンドラッグによるパン
            if (Input::GetKeyHold(KeyCode::MOUSE_MIDDLE) || (Input::GetKeyHold(KeyCode::MOUSE_RIGHT) && Input::GetKeyHold(KeyCode::SHIFT))) {
                Vector2 delta = Input::GetMouseDelta();
                float panSpeed = CameraDistance * 0.0018f;
                CameraFocus.x -= delta.x * panSpeed;
                CameraFocus.z += delta.y * panSpeed;
            }

            // キーボードによるパン (矢印キー)
            float keyPanSpeed = CameraDistance * 0.8f * deltaTime;
            if (Input::GetKeyHold(KeyCode::UP))    CameraFocus.z += keyPanSpeed;
            if (Input::GetKeyHold(KeyCode::DOWN))  CameraFocus.z -= keyPanSpeed;
            if (Input::GetKeyHold(KeyCode::LEFT))  CameraFocus.x -= keyPanSpeed;
            if (Input::GetKeyHold(KeyCode::RIGHT)) CameraFocus.x += keyPanSpeed;
        }

        // ================================================================
        // 3D 自由飛行カメラ操作 (WASDQE + 右クリックルック + Noclip)
        // ================================================================
        void UpdateFlycam(float dt) {
            if (Input::GetKeyHold(KeyCode::MOUSE_RIGHT)) {
                IsFlycamLooking = true;
                Vector2 delta = Input::GetMouseDelta();
                FlycamYaw += delta.x * 0.18f;
                FlycamPitch = std::clamp(FlycamPitch + delta.y * 0.18f, -88.0f, 88.0f);
            } else {
                IsFlycamLooking = false;
            }

            float yawRad = XMConvertToRadians(FlycamYaw);
            float pitchRad = XMConvertToRadians(FlycamPitch);
            XMVECTOR forwardVec = XMVectorSet(
                std::sin(yawRad) * std::cos(pitchRad),
                -std::sin(pitchRad),
                std::cos(yawRad) * std::cos(pitchRad),
                0.0f
            );
            XMVECTOR rightVec = XMVectorSet(
                std::cos(yawRad),
                0.0f,
                -std::sin(yawRad),
                0.0f
            );
            XMVECTOR upVec = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

            float currentSpeed = Input::GetKeyHold(KeyCode::SHIFT) ? 20.0f : FlycamSpeed;
            XMVECTOR moveDir = XMVectorZero();
            if (Input::GetKeyHold(KeyCode::W)) moveDir += forwardVec;
            if (Input::GetKeyHold(KeyCode::S)) moveDir -= forwardVec;
            if (Input::GetKeyHold(KeyCode::D)) moveDir += rightVec;
            if (Input::GetKeyHold(KeyCode::A)) moveDir -= rightVec;
            if (Input::GetKeyHold(KeyCode::E) || Input::GetKeyHold(KeyCode::SPACE)) moveDir += upVec;
            if (Input::GetKeyHold(KeyCode::Q) || Input::GetKeyHold(KeyCode::C)) moveDir -= upVec;

            if (XMVectorGetX(XMVector3LengthSq(moveDir)) > 1e-5f) {
                moveDir = XMVector3Normalize(moveDir);
                XMVECTOR deltaPos = moveDir * (currentSpeed * dt);
                XMFLOAT3 d;
                XMStoreFloat3(&d, deltaPos);
                FlycamPos.x += d.x;
                FlycamPos.y += d.y;
                FlycamPos.z += d.z;
            }
        }

        // ================================================================
        // マウス位置からフロア平面へのレイキャスト計算
        // ================================================================
        void UpdateHoverState(int screenW, int screenH, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix) {
            Vector2 mousePos = Input::GetMousePosition();
            float currentFloorY = static_cast<float>(CurrentFloor) * GRID_FLOOR_HEIGHT;

            // スクリーン座標 -> NDC (-1 ~ +1)
            float ndcX = (2.0f * mousePos.x / static_cast<float>(screenW)) - 1.0f;
            float ndcY = 1.0f - (2.0f * mousePos.y / static_cast<float>(screenH));

            XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewMatrix * projMatrix);

            XMVECTOR nearPoint = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
            XMVECTOR farPoint  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);

            XMFLOAT3 pNear, pFar;
            XMStoreFloat3(&pNear, nearPoint);
            XMStoreFloat3(&pFar, farPoint);

            float rayDirY = pFar.y - pNear.y;
            if (std::abs(rayDirY) > 1e-5f) {
                float t = (currentFloorY - pNear.y) / rayDirY;
                if (t >= 0.0f) {
                    HasFloorHit = true;
                    HoverWorldPos.x = pNear.x + (pFar.x - pNear.x) * t;
                    HoverWorldPos.y = currentFloorY;
                    HoverWorldPos.z = pNear.z + (pFar.z - pNear.z) * t;

                    // セル座標計算
                    HoverCell = WorldToGrid(HoverWorldPos);
                    HoverCell.Floor = CurrentFloor;

                    // 最寄りエッジの計算
                    float cellCenterX = (static_cast<float>(HoverCell.X) + 0.5f) * GRID_CELL_SIZE;
                    float cellCenterZ = (static_cast<float>(HoverCell.Z) + 0.5f) * GRID_CELL_SIZE;
                    float dx = HoverWorldPos.x - cellCenterX;
                    float dz = HoverWorldPos.z - cellCenterZ;

                    if (std::abs(dx) > std::abs(dz)) {
                        HoverEdge = (dx > 0) ? CellEdge::East : CellEdge::West;
                    } else {
                        HoverEdge = (dz > 0) ? CellEdge::North : CellEdge::South;
                    }

                    // ホバーしている既存オブジェクトの探索
                    HoveredInstanceId = 0;
                    for (const auto& obj : Stage.Objects) {
                        if (obj.FloorIndex != CurrentFloor) continue;

                        if (obj.Placement == PlacementType::CellSnap && obj.Cell == HoverCell) {
                            HoveredInstanceId = obj.InstanceId;
                            break;
                        }
                    }

                    // 重複判定のチェック
                    HasPlacementConflict = false;
                    if (SelectedPlacement == PlacementType::CellSnap) {
                        GridCoord secondCell = HoverCell;
                        bool isTwoCell = (SelectedCategory == AssetCategory::Stairs || SelectedAssetId == "Stairs_Straight");
                        if (isTwoCell) {
                            float rad = XMConvertToRadians(BrushYaw);
                            int dx = static_cast<int>(std::round(std::sin(rad)));
                            int dz = static_cast<int>(std::round(std::cos(rad)));
                            secondCell.X += dx;
                            secondCell.Z += dz;
                        }

                        for (const auto& obj : Stage.Objects) {
                            if (obj.FloorIndex == CurrentFloor && obj.Placement == PlacementType::CellSnap &&
                                (obj.Cell == HoverCell || (isTwoCell && obj.Cell == secondCell))) {
                                if (SelectedCategory == AssetCategory::Special && obj.Category == AssetCategory::Floor) continue;
                                if (obj.Category == AssetCategory::Special && SelectedCategory == AssetCategory::Floor) continue;
                                HasPlacementConflict = true;
                                break;
                            }
                        }
                    } else if (SelectedPlacement == PlacementType::EdgeSnap) {
                        for (const auto& obj : Stage.Objects) {
                            if (obj.FloorIndex == CurrentFloor && obj.Placement == PlacementType::EdgeSnap &&
                                obj.Cell == HoverCell && obj.Edge == HoverEdge) {
                                if (SelectedCategory == AssetCategory::Prop && obj.Category == AssetCategory::Wall) continue;
                                if (obj.Category == AssetCategory::Prop && SelectedCategory == AssetCategory::Wall) continue;
                                HasPlacementConflict = true;
                                break;
                            }
                        }
                    }
                    return;
                }
            }
            HasFloorHit = false;
        }

        // ================================================================
        // 3D 空間オブジェクトピッキング (Ray-AABB 交差判定)
        // ================================================================
        static bool IntersectRayAABB(
            const XMFLOAT3& rayOrigin, const XMFLOAT3& rayDir,
            const XMFLOAT3& boxMin, const XMFLOAT3& boxMax,
            float& outT)
        {
            float tMin = -1e9f;
            float tMax = 1e9f;

            auto checkAxis = [&](float origin, float dir, float bMin, float bMax) -> bool {
                if (std::abs(dir) < 1e-6f) {
                    return origin >= bMin && origin <= bMax;
                }
                float t1 = (bMin - origin) / dir;
                float t2 = (bMax - origin) / dir;
                if (t1 > t2) std::swap(t1, t2);
                tMin = (std::max)(tMin, t1);
                tMax = (std::min)(tMax, t2);
                return tMin <= tMax;
            };

            if (!checkAxis(rayOrigin.x, rayDir.x, boxMin.x, boxMax.x)) return false;
            if (!checkAxis(rayOrigin.y, rayDir.y, boxMin.y, boxMax.y)) return false;
            if (!checkAxis(rayOrigin.z, rayDir.z, boxMin.z, boxMax.z)) return false;

            if (tMax < 0.0f) return false;
            outT = (tMin >= 0.0f) ? tMin : tMax;
            return true;
        }

        uint64_t PickObjectAtScreen(int screenW, int screenH, const XMMATRIX& viewMatrix, const XMMATRIX& projMatrix) {
            Vector2 mousePos = Input::GetMousePosition();
            float ndcX = (2.0f * mousePos.x / static_cast<float>(screenW)) - 1.0f;
            float ndcY = 1.0f - (2.0f * mousePos.y / static_cast<float>(screenH));

            XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewMatrix * projMatrix);
            XMVECTOR nearPt = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 0.0f, 1.0f), invViewProj);
            XMVECTOR farPt  = XMVector3TransformCoord(XMVectorSet(ndcX, ndcY, 1.0f, 1.0f), invViewProj);
            XMVECTOR dirVec = XMVector3Normalize(farPt - nearPt);

            XMFLOAT3 ro, rd;
            XMStoreFloat3(&ro, nearPt);
            XMStoreFloat3(&rd, dirVec);

            float bestT = 1e9f;
            uint64_t bestId = 0;

            for (const auto& obj : Stage.Objects) {
                // TopDownビュー時はアクティブフロアのみ対象
                if (CameraMode == EditorCameraMode::TopDown && obj.FloorIndex != CurrentFloor) {
                    continue;
                }
                const auto* flr = Stage.FindFloor(obj.FloorIndex);
                if (flr && !flr->Visible) continue;

                XMFLOAT3 bMin, bMax;
                float halfCell = GRID_CELL_SIZE * 0.5f;

                if (obj.Category == AssetCategory::Wall) {
                    // 壁の向き（Yaw角）に応じた薄型タイトAABB (厚み方向 ±0.20m、幅方向 ±halfCell)
                    bool isRot = (std::abs(std::fmod(obj.Rotation.y, 180.0f)) > 45.0f);
                    if (isRot) {
                        bMin = { obj.Position.x - 0.20f, obj.Position.y, obj.Position.z - halfCell };
                        bMax = { obj.Position.x + 0.20f, obj.Position.y + GRID_FLOOR_HEIGHT, obj.Position.z + halfCell };
                    } else {
                        bMin = { obj.Position.x - halfCell, obj.Position.y, obj.Position.z - 0.20f };
                        bMax = { obj.Position.x + halfCell, obj.Position.y + GRID_FLOOR_HEIGHT, obj.Position.z + 0.20f };
                    }
                } else if (obj.Category == AssetCategory::Floor) {
                    // 床は上面 y + 0.05m の薄板 (床上オブジェクトが手前で優先ヒット)
                    bMin = { obj.Position.x - halfCell, obj.Position.y - 0.20f, obj.Position.z - halfCell };
                    bMax = { obj.Position.x + halfCell, obj.Position.y + 0.05f, obj.Position.z + halfCell };
                } else if (obj.Category == AssetCategory::Pedestal) {
                    bMin = { obj.Position.x - 0.45f, obj.Position.y, obj.Position.z - 0.45f };
                    bMax = { obj.Position.x + 0.45f, obj.Position.y + 1.25f, obj.Position.z + 0.45f };
                } else if (obj.Category == AssetCategory::Special) {
                    if (obj.AssetId == "GoalChest") {
                        bMin = { obj.Position.x - 0.65f, obj.Position.y, obj.Position.z - 0.55f };
                        bMax = { obj.Position.x + 0.65f, obj.Position.y + 1.10f, obj.Position.z + 0.55f };
                    } else {
                        bMin = { obj.Position.x - 0.90f, obj.Position.y, obj.Position.z - 0.90f };
                        bMax = { obj.Position.x + 0.90f, obj.Position.y + 0.35f, obj.Position.z + 0.90f };
                    }
                } else if (obj.Category == AssetCategory::Stairs) {
                    bool isRot = (std::abs(std::fmod(obj.Rotation.y, 180.0f)) > 45.0f);
                    if (isRot) {
                        bMin = { obj.Position.x - GRID_CELL_SIZE, obj.Position.y, obj.Position.z - halfCell };
                        bMax = { obj.Position.x + GRID_CELL_SIZE, obj.Position.y + GRID_FLOOR_HEIGHT, obj.Position.z + halfCell };
                    } else {
                        bMin = { obj.Position.x - halfCell, obj.Position.y, obj.Position.z - GRID_CELL_SIZE };
                        bMax = { obj.Position.x + halfCell, obj.Position.y + GRID_FLOOR_HEIGHT, obj.Position.z + GRID_CELL_SIZE };
                    }
                } else { // Props
                    bMin = { obj.Position.x - 0.45f, obj.Position.y, obj.Position.z - 0.45f };
                    bMax = { obj.Position.x + 0.45f, obj.Position.y + 1.00f, obj.Position.z + 0.45f };
                }

                float t = 0.0f;
                if (IntersectRayAABB(ro, rd, bMin, bMax, t)) {
                    if (t < bestT && t >= 0.0f) {
                        bestT = t;
                        bestId = obj.InstanceId;
                    }
                }
            }
            return bestId;
        }

        // ================================================================
        // オブジェクト配置処理 (単発 & ドラッグ)
        // ================================================================
        bool TryPlaceObject(bool isBatch = false) {
            if (!HasFloorHit) return false;

            PlacedObject newObj;
            newObj.AssetId = SelectedAssetId;
            newObj.Category = SelectedCategory;
            newObj.Placement = SelectedPlacement;
            newObj.FloorIndex = CurrentFloor;
            newObj.Phase = SelectedPhase;
            // 宝玉数の上限を超える配置は宝玉なしの台座として置く
            newObj.HasInitialOrb = BrushInitialOrb && CanAddInitialOrb();

            if (SelectedPlacement == PlacementType::CellSnap) {
                newObj.Cell = HoverCell;
                newObj.Position = GetPlacementPosition();
                newObj.Rotation.y = BrushYaw;
            } else if (SelectedPlacement == PlacementType::EdgeSnap) {
                newObj.Cell = HoverCell;
                newObj.Edge = HoverEdge;
                newObj.Position = GetPlacementPosition();
                newObj.Rotation.y = GetPlacementYaw();
            } else { // FreeAttach
                newObj.Position = HoverWorldPos;
                newObj.Rotation.y = BrushYaw;
            }

            if (newObj.AssetId == "Prop_Torch") {
                newObj.HasLight = true;
                newObj.LightColor = { 1.0f, 0.70f, 0.28f };
                newObj.LightIntensity = 22.0f;
                newObj.LightRadius = 10.0f;
            } else if (newObj.AssetId == "Prop_Candle") {
                newObj.HasLight = true;
                newObj.LightColor = { 1.0f, 0.75f, 0.35f };
                newObj.LightIntensity = 10.0f;
                newObj.LightRadius = 6.0f;
            }

            std::string err;
            if (!Stage.AddObject(newObj, err)) {
                if (!isBatch) SetStatusMessage(err);
                return false;
            }

            if (!isBatch) {
                EditorCommand cmd;
                cmd.Action = EditorActionType::Add;
                cmd.ObjectData = Stage.Objects.back();
                UndoStack.push_back(cmd);
                RedoStack.clear();
                SetStatusMessage("Placed: " + newObj.AssetId);
            }
            return true;
        }

        bool StartDragPlace() {
            if (!HasFloorHit) return false;
            IsDragging = true;
            CurrentDragBatch.clear();
            LastDragCell = { -9999, -9999, -9999 };
            LastDragEdge = CellEdge::North;
            LastDragWorldPos = { -9999.0f, -9999.0f, -9999.0f };
            return ContinueDragPlace();
        }

        bool ContinueDragPlace() {
            if (!IsDragging || !HasFloorHit) return false;

            if (SelectedPlacement == PlacementType::CellSnap) {
                if (HoverCell == LastDragCell) return false;
            } else if (SelectedPlacement == PlacementType::EdgeSnap) {
                if (HoverCell == LastDragCell && HoverEdge == LastDragEdge) return false;
            } else {
                float dx = HoverWorldPos.x - LastDragWorldPos.x;
                float dz = HoverWorldPos.z - LastDragWorldPos.z;
                if ((dx * dx + dz * dz) < 0.25f) return false;
            }

            if (TryPlaceObject(true)) {
                LastDragCell = HoverCell;
                LastDragEdge = HoverEdge;
                LastDragWorldPos = HoverWorldPos;
                CurrentDragBatch.push_back(Stage.Objects.back());
                return true;
            }
            return false;
        }

        void EndDragPlace() {
            if (!IsDragging) return;
            IsDragging = false;
            if (!CurrentDragBatch.empty()) {
                EditorCommand cmd;
                cmd.Action = EditorActionType::BatchAdd;
                cmd.BatchObjects = CurrentDragBatch;
                UndoStack.push_back(cmd);
                RedoStack.clear();
                SetStatusMessage("Placed batch of " + std::to_string(CurrentDragBatch.size()) + " objects");
            }
            CurrentDragBatch.clear();
        }

        // ================================================================
        // オブジェクト削除処理 (単発 & ドラッグ消しゴム)
        // ================================================================
        bool TryRemoveObjectUnderCursor(PlacedObject* outRemoved = nullptr) {
            if (!HasFloorHit) return false;

            float cellCenterX = (static_cast<float>(HoverCell.X) + 0.5f) * GRID_CELL_SIZE;
            float cellCenterZ = (static_cast<float>(HoverCell.Z) + 0.5f) * GRID_CELL_SIZE;
            float dx = HoverWorldPos.x - cellCenterX;
            float dz = HoverWorldPos.z - cellCenterZ;

            uint64_t targetId = 0;
            PlacedObject removedData;

            // 1. エッジ近傍ホバー判定 (セルの境界寄りにマウスがある場合、該当エッジ上の壁を優先探索)
            bool isNearEdge = (std::abs(dx) > GRID_CELL_SIZE * 0.30f || std::abs(dz) > GRID_CELL_SIZE * 0.30f);
            if (isNearEdge) {
                float bestEdgeDistSq = 0.65f * 0.65f;
                for (const auto& obj : Stage.Objects) {
                    if (obj.FloorIndex != CurrentFloor) continue;
                    if (obj.Placement == PlacementType::EdgeSnap) {
                        float distSq = (obj.Position.x - HoverWorldPos.x) * (obj.Position.x - HoverWorldPos.x) +
                                       (obj.Position.z - HoverWorldPos.z) * (obj.Position.z - HoverWorldPos.z);
                        if (distSq < bestEdgeDistSq) {
                            bestEdgeDistSq = distSq;
                            targetId = obj.InstanceId;
                            removedData = obj;
                        }
                    }
                }
            }

            // 2. エッジ壁がヒットしなかった、またはセル内部ホバーの場合: セル内のオブジェクトを探索
            if (targetId == 0) {
                // 2a. まずセル内の非Floorオブジェクト(Pedestal, Special, Stairs, Props)を優先
                for (const auto& obj : Stage.Objects) {
                    if (obj.FloorIndex != CurrentFloor) continue;
                    if (obj.Category != AssetCategory::Floor && obj.Placement == PlacementType::CellSnap && obj.Cell == HoverCell) {
                        targetId = obj.InstanceId;
                        removedData = obj;
                        break;
                    }
                }

                // 2b. 非Floorがなければ、セル内のFloorオブジェクトを探索
                if (targetId == 0) {
                    for (const auto& obj : Stage.Objects) {
                        if (obj.FloorIndex != CurrentFloor) continue;
                        if (obj.Category == AssetCategory::Floor && obj.Placement == PlacementType::CellSnap && obj.Cell == HoverCell) {
                            targetId = obj.InstanceId;
                            removedData = obj;
                            break;
                        }
                    }
                }
            }

            // 3. フォールバック (FreeAttachオブジェクト等、近接0.55m以内)
            if (targetId == 0) {
                float bestFallbackDistSq = 0.55f * 0.55f;
                for (const auto& obj : Stage.Objects) {
                    if (obj.FloorIndex != CurrentFloor) continue;
                    float distSq = (obj.Position.x - HoverWorldPos.x) * (obj.Position.x - HoverWorldPos.x) +
                                   (obj.Position.z - HoverWorldPos.z) * (obj.Position.z - HoverWorldPos.z);
                    if (distSq < bestFallbackDistSq) {
                        bestFallbackDistSq = distSq;
                        targetId = obj.InstanceId;
                        removedData = obj;
                    }
                }
            }

            if (targetId != 0) {
                Stage.RemoveObject(targetId);
                if (SelectedInstanceId == targetId) SelectedInstanceId = 0;

                if (outRemoved) {
                    *outRemoved = removedData;
                } else {
                    EditorCommand cmd;
                    cmd.Action = EditorActionType::Remove;
                    cmd.ObjectData = removedData;
                    UndoStack.push_back(cmd);
                    RedoStack.clear();
                    SetStatusMessage("Removed: " + removedData.AssetId);
                }
                return true;
            }
            return false;
        }

        bool StartDragRemove() {
            if (!HasFloorHit) return false;
            IsDragErasing = true;
            CurrentDragRemovedBatch.clear();
            LastDragRemoveCell = { -9999, -9999, -9999 };
            LastDragRemoveEdge = CellEdge::North;
            return ContinueDragRemove();
        }

        bool ContinueDragRemove() {
            if (!IsDragErasing || !HasFloorHit) return false;
            if (HoverCell == LastDragRemoveCell && HoverEdge == LastDragRemoveEdge) return false;
            PlacedObject removed;
            if (TryRemoveObjectUnderCursor(&removed)) {
                LastDragRemoveCell = HoverCell;
                LastDragRemoveEdge = HoverEdge;
                CurrentDragRemovedBatch.push_back(removed);
                return true;
            }
            return false;
        }

        void EndDragRemove() {
            if (!IsDragErasing) return;
            IsDragErasing = false;
            if (!CurrentDragRemovedBatch.empty()) {
                EditorCommand cmd;
                cmd.Action = EditorActionType::BatchRemove;
                cmd.BatchObjects = CurrentDragRemovedBatch;
                UndoStack.push_back(cmd);
                RedoStack.clear();
                SetStatusMessage("Removed batch of " + std::to_string(CurrentDragRemovedBatch.size()) + " objects");
            }
            CurrentDragRemovedBatch.clear();
            LastDragRemoveCell = { -9999, -9999, -9999 };
        }

        // ================================================================
        // オブジェクト選択＆インスペクタ操作 (選択モード)
        // ================================================================
        void SelectObject(uint64_t id) {
            SelectedInstanceId = id;
        }

        void Deselect() {
            SelectedInstanceId = 0;
        }

        const PlacedObject* GetSelectedObject() const {
            if (SelectedInstanceId == 0) return nullptr;
            return Stage.FindObject(SelectedInstanceId);
        }

        PlacedObject* GetSelectedObjectMut() {
            if (SelectedInstanceId == 0) return nullptr;
            return Stage.FindObjectMut(SelectedInstanceId);
        }

        bool DeleteSelectedObject() {
            if (SelectedInstanceId == 0) return false;
            const PlacedObject* o = Stage.FindObject(SelectedInstanceId);
            if (!o) { SelectedInstanceId = 0; return false; }

            PlacedObject removedData = *o;
            Stage.RemoveObject(SelectedInstanceId);
            SelectedInstanceId = 0;

            EditorCommand cmd;
            cmd.Action = EditorActionType::Remove;
            cmd.ObjectData = removedData;
            UndoStack.push_back(cmd);
            RedoStack.clear();
            SetStatusMessage("Deleted: " + removedData.AssetId);
            return true;
        }

        bool ToggleSelectedPhase() {
            PlacedObject* o = GetSelectedObjectMut();
            if (!o) return false;
            PlacedObject prev = *o;
            o->Phase = (o->Phase == MaterialPhase::Phase) ? MaterialPhase::Normal : MaterialPhase::Phase;

            EditorCommand cmd;
            cmd.Action = EditorActionType::Modify;
            cmd.ObjectData = *o;
            cmd.PreviousData = prev;
            UndoStack.push_back(cmd);
            RedoStack.clear();
            SetStatusMessage("Toggled Phase for " + o->AssetId);
            return true;
        }

        // 現在ステージに配置済みの宝玉数
        int32_t CountInitialOrbs() const {
            int32_t n = 0;
            for (const auto& o : Stage.Objects) {
                if (o.Category == AssetCategory::Pedestal && o.HasInitialOrb) ++n;
            }
            return n;
        }

        bool CanAddInitialOrb() const { return CountInitialOrbs() < MAX_STAGE_ORBS; }

        bool ToggleSelectedInitialOrb() {
            PlacedObject* o = GetSelectedObjectMut();
            if (!o || o->Category != AssetCategory::Pedestal) return false;
            // 上限に達している状態で新たに宝玉を持たせることはできない
            if (!o->HasInitialOrb && !CanAddInitialOrb()) return false;
            PlacedObject prev = *o;
            o->HasInitialOrb = !o->HasInitialOrb;

            EditorCommand cmd;
            cmd.Action = EditorActionType::Modify;
            cmd.ObjectData = *o;
            cmd.PreviousData = prev;
            UndoStack.push_back(cmd);
            RedoStack.clear();
            SetStatusMessage("Toggled Initial Orb for Pedestal");
            return true;
        }

        bool RotateSelectedObject(float deltaDeg) {
            PlacedObject* o = GetSelectedObjectMut();
            if (!o) return false;
            PlacedObject prev = *o;
            o->Rotation.y = std::fmod(o->Rotation.y + deltaDeg + 360.0f, 360.0f);

            EditorCommand cmd;
            cmd.Action = EditorActionType::Modify;
            cmd.ObjectData = *o;
            cmd.PreviousData = prev;
            UndoStack.push_back(cmd);
            RedoStack.clear();
            return true;
        }

        bool MoveSelectedObjectToFloor(int32_t targetFloor) {
            PlacedObject* o = GetSelectedObjectMut();
            if (!o) return false;
            PlacedObject prev = *o;
            Stage.MoveObjectToFloor(SelectedInstanceId, targetFloor);

            EditorCommand cmd;
            cmd.Action = EditorActionType::Modify;
            cmd.ObjectData = *o;
            cmd.PreviousData = prev;
            UndoStack.push_back(cmd);
            RedoStack.clear();
            SetStatusMessage("Moved " + o->AssetId + " to Floor " + std::to_string(targetFloor + 1));
            return true;
        }

        // ================================================================
        // 動的フロア・レイヤー操作
        // ================================================================
        bool AddFloorAbove() {
            int32_t newFloor = CurrentFloor + 1;
            while (Stage.HasFloor(newFloor)) {
                newFloor++;
            }
            if (Stage.AddFloor(newFloor)) {
                CurrentFloor = newFloor;
                SetStatusMessage("Added new floor above: " + LuminousStage::DefaultFloorName(newFloor));
                return true;
            }
            return false;
        }

        bool AddFloorBelow() {
            int32_t newFloor = CurrentFloor - 1;
            while (Stage.HasFloor(newFloor)) {
                newFloor--;
            }
            if (Stage.AddFloor(newFloor)) {
                CurrentFloor = newFloor;
                SetStatusMessage("Added new floor below: " + LuminousStage::DefaultFloorName(newFloor));
                return true;
            }
            return false;
        }

        bool DeleteCurrentFloor() {
            if (Stage.Floors.size() <= 1) {
                SetStatusMessage("Cannot delete the only remaining floor.");
                return false;
            }
            std::vector<PlacedObject> removedObjs;
            int32_t targetFloor = CurrentFloor;
            if (Stage.RemoveFloor(targetFloor, removedObjs)) {
                if (!removedObjs.empty()) {
                    EditorCommand cmd;
                    cmd.Action = EditorActionType::BatchRemove;
                    cmd.BatchObjects = removedObjs;
                    UndoStack.push_back(cmd);
                    RedoStack.clear();
                }
                CurrentFloor = Stage.Floors.front().FloorIndex;
                SetStatusMessage("Deleted floor: " + LuminousStage::DefaultFloorName(targetFloor));
                return true;
            }
            return false;
        }

        bool MoveCurrentFloorUp() {
            int32_t higherFloor = 9999;
            for (const auto& f : Stage.Floors) {
                if (f.FloorIndex > CurrentFloor && f.FloorIndex < higherFloor) {
                    higherFloor = f.FloorIndex;
                }
            }
            if (higherFloor == 9999) {
                SetStatusMessage("No higher floor to swap with.");
                return false;
            }
            if (Stage.SwapFloorIndices(CurrentFloor, higherFloor)) {
                EditorCommand cmd;
                cmd.Action = EditorActionType::FloorSwap;
                cmd.FloorA = CurrentFloor;
                cmd.FloorB = higherFloor;
                UndoStack.push_back(cmd);
                RedoStack.clear();

                int32_t oldFloor = CurrentFloor;
                CurrentFloor = higherFloor;
                SetStatusMessage("Swapped floors: " + LuminousStage::DefaultFloorName(oldFloor) + " <-> " + LuminousStage::DefaultFloorName(higherFloor));
                return true;
            }
            return false;
        }

        bool MoveCurrentFloorDown() {
            int32_t lowerFloor = -9999;
            for (const auto& f : Stage.Floors) {
                if (f.FloorIndex < CurrentFloor && f.FloorIndex > lowerFloor) {
                    lowerFloor = f.FloorIndex;
                }
            }
            if (lowerFloor == -9999) {
                SetStatusMessage("No lower floor to swap with.");
                return false;
            }
            if (Stage.SwapFloorIndices(CurrentFloor, lowerFloor)) {
                EditorCommand cmd;
                cmd.Action = EditorActionType::FloorSwap;
                cmd.FloorA = CurrentFloor;
                cmd.FloorB = lowerFloor;
                UndoStack.push_back(cmd);
                RedoStack.clear();

                int32_t oldFloor = CurrentFloor;
                CurrentFloor = lowerFloor;
                SetStatusMessage("Swapped floors: " + LuminousStage::DefaultFloorName(oldFloor) + " <-> " + LuminousStage::DefaultFloorName(lowerFloor));
                return true;
            }
            return false;
        }

        // ================================================================
        // Undo / Redo (Phase 7 完全対応)
        // ================================================================
        bool Undo() {
            if (UndoStack.empty()) return false;

            EditorCommand cmd = UndoStack.back();
            UndoStack.pop_back();

            if (cmd.Action == EditorActionType::Add) {
                Stage.RemoveObject(cmd.ObjectData.InstanceId);
                RedoStack.push_back(cmd);
                SetStatusMessage("Undo: Removed " + cmd.ObjectData.AssetId);
            } else if (cmd.Action == EditorActionType::Remove) {
                std::string err;
                Stage.AddObject(cmd.ObjectData, err);
                RedoStack.push_back(cmd);
                SetStatusMessage("Undo: Restored " + cmd.ObjectData.AssetId);
            } else if (cmd.Action == EditorActionType::Modify) {
                PlacedObject* o = Stage.FindObjectMut(cmd.ObjectData.InstanceId);
                if (o) {
                    PlacedObject cur = *o;
                    *o = cmd.PreviousData;
                    cmd.PreviousData = cur;
                    RedoStack.push_back(cmd);
                    SetStatusMessage("Undo: Reverted properties of " + o->AssetId);
                }
            } else if (cmd.Action == EditorActionType::BatchAdd) {
                for (const auto& obj : cmd.BatchObjects) {
                    Stage.RemoveObject(obj.InstanceId);
                }
                RedoStack.push_back(cmd);
                SetStatusMessage("Undo: Removed batch (" + std::to_string(cmd.BatchObjects.size()) + ")");
            } else if (cmd.Action == EditorActionType::BatchRemove) {
                for (const auto& obj : cmd.BatchObjects) {
                    std::string err;
                    Stage.AddObject(obj, err);
                }
                RedoStack.push_back(cmd);
                SetStatusMessage("Undo: Restored batch (" + std::to_string(cmd.BatchObjects.size()) + ")");
            } else if (cmd.Action == EditorActionType::FloorSwap) {
                Stage.SwapFloorIndices(cmd.FloorA, cmd.FloorB);
                RedoStack.push_back(cmd);
                SetStatusMessage("Undo: Swapped floors " + LuminousStage::DefaultFloorName(cmd.FloorA) + " <-> " + LuminousStage::DefaultFloorName(cmd.FloorB));
            }
            return true;
        }

        bool Redo() {
            if (RedoStack.empty()) return false;

            EditorCommand cmd = RedoStack.back();
            RedoStack.pop_back();

            if (cmd.Action == EditorActionType::Add) {
                std::string err;
                Stage.AddObject(cmd.ObjectData, err);
                UndoStack.push_back(cmd);
                SetStatusMessage("Redo: Placed " + cmd.ObjectData.AssetId);
            } else if (cmd.Action == EditorActionType::Remove) {
                Stage.RemoveObject(cmd.ObjectData.InstanceId);
                UndoStack.push_back(cmd);
                SetStatusMessage("Redo: Removed " + cmd.ObjectData.AssetId);
            } else if (cmd.Action == EditorActionType::Modify) {
                PlacedObject* o = Stage.FindObjectMut(cmd.ObjectData.InstanceId);
                if (o) {
                    PlacedObject cur = *o;
                    *o = cmd.PreviousData;
                    cmd.PreviousData = cur;
                    UndoStack.push_back(cmd);
                    SetStatusMessage("Redo: Applied properties of " + o->AssetId);
                }
            } else if (cmd.Action == EditorActionType::BatchAdd) {
                for (const auto& obj : cmd.BatchObjects) {
                    std::string err;
                    Stage.AddObject(obj, err);
                }
                UndoStack.push_back(cmd);
                SetStatusMessage("Redo: Placed batch (" + std::to_string(cmd.BatchObjects.size()) + ")");
            } else if (cmd.Action == EditorActionType::BatchRemove) {
                for (const auto& obj : cmd.BatchObjects) {
                    Stage.RemoveObject(obj.InstanceId);
                }
                UndoStack.push_back(cmd);
                SetStatusMessage("Redo: Removed batch (" + std::to_string(cmd.BatchObjects.size()) + ")");
            } else if (cmd.Action == EditorActionType::FloorSwap) {
                Stage.SwapFloorIndices(cmd.FloorA, cmd.FloorB);
                UndoStack.push_back(cmd);
                SetStatusMessage("Redo: Swapped floors " + LuminousStage::DefaultFloorName(cmd.FloorA) + " <-> " + LuminousStage::DefaultFloorName(cmd.FloorB));
            }
            return true;
        }

        // ================================================================
        // ステージ整合性検証 (仕様書 第8条 8.4)
        // ================================================================
        StageValidationResult Validate() const {
            return Stage.ValidateStage();
        }

        void SetStatusMessage(const std::string& msg) {
            StatusMessage = msg;
            StatusMessageTimer = 3.5f;
        }

        void UpdateStatusTimer(float dt) {
            if (StatusMessageTimer > 0.0f) {
                StatusMessageTimer -= dt;
            }
        }

    };
}
