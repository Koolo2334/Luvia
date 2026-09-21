module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <memory>
#include <algorithm>
#include <atomic>
#include <filesystem>

#include <DirectXMath.h>
export module App.Luminous.StageData;

import App.Luminous.Types;

export namespace App::Luminous {

    using json = nlohmann::json;

    class LuminousStage {
    private:
        static inline std::atomic<uint64_t> s_nextInstanceId{ 1 };

    public:
        std::string Name = "New Stage";
        std::vector<FloorConfig> Floors;
        std::vector<PlacedObject> Objects;

        LuminousStage() {
            // デフォルトで 1F を作成
            FloorConfig f1;
            f1.FloorIndex = 0;
            f1.Name = "1F";
            f1.GridWidth = 16;
            f1.GridDepth = 16;
            Floors.push_back(f1);
        }

        uint64_t GenerateId() {
            return s_nextInstanceId.fetch_add(1);
        }

        // ================================================================
        // オブジェクト操作
        // ================================================================
        bool AddObject(PlacedObject obj, std::string& outError) {
            // 1. 重複チェック
            if (obj.Placement == PlacementType::CellSnap) {
                for (const auto& existing : Objects) {
                    if (existing.FloorIndex == obj.FloorIndex &&
                        existing.Placement == PlacementType::CellSnap &&
                        existing.Cell == obj.Cell)
                    {
                        // 床の上に召喚陣(StartDais)を載せるのは許可
                        if (obj.Category == AssetCategory::Special && existing.Category == AssetCategory::Floor) {
                            continue;
                        }
                        if (existing.Category == AssetCategory::Special && obj.Category == AssetCategory::Floor) {
                            continue;
                        }
                        outError = "Cell already occupied on this floor.";
                        return false;
                    }
                }
            } else if (obj.Placement == PlacementType::EdgeSnap) {
                for (const auto& existing : Objects) {
                    if (existing.FloorIndex == obj.FloorIndex &&
                        existing.Placement == PlacementType::EdgeSnap &&
                        existing.Cell == obj.Cell &&
                        existing.Edge == obj.Edge)
                    {
                        // 壁かけプロップ (Prop_Torch等) は既存の壁の上に共存可能
                        if (obj.Category == AssetCategory::Prop && existing.Category == AssetCategory::Wall) {
                            continue;
                        }
                        if (existing.Category == AssetCategory::Prop && obj.Category == AssetCategory::Wall) {
                            continue;
                        }
                        outError = "Edge already occupied on this floor.";
                        return false;
                    }
                }
            }

            // 2. 特殊オブジェクトの唯一性ルール (StartPoint & GoalPoint)
            if (obj.AssetId == "StartDais") {
                // すでに存在していれば古いものを自動削除して最新位置へ移動
                RemoveByAssetId("StartDais");
            } else if (obj.AssetId == "GoalChest") {
                RemoveByAssetId("GoalChest");
            }

            if (obj.InstanceId == 0) {
                obj.InstanceId = GenerateId();
            }

            Objects.push_back(obj);
            return true;
        }

        bool RemoveObject(uint64_t instanceId) {
            auto it = std::remove_if(Objects.begin(), Objects.end(),
                [instanceId](const PlacedObject& o) { return o.InstanceId == instanceId; });
            if (it != Objects.end()) {
                Objects.erase(it, Objects.end());
                return true;
            }
            return false;
        }

        void RemoveByAssetId(const std::string& assetId) {
            auto it = std::remove_if(Objects.begin(), Objects.end(),
                [&assetId](const PlacedObject& o) { return o.AssetId == assetId; });
            if (it != Objects.end()) {
                Objects.erase(it, Objects.end());
            }
        }

        const PlacedObject* FindObject(uint64_t instanceId) const {
            for (const auto& o : Objects) {
                if (o.InstanceId == instanceId) return &o;
            }
            return nullptr;
        }

        PlacedObject* FindObjectMut(uint64_t instanceId) {
            for (auto& o : Objects) {
                if (o.InstanceId == instanceId) return &o;
            }
            return nullptr;
        }

        // ================================================================
        // 動的フロア・レイヤー管理 (Phase 7)
        // ================================================================
        static std::string DefaultFloorName(int32_t floorIndex) {
            if (floorIndex < 0) {
                return "B" + std::to_string(-floorIndex) + "F";
            }
            return std::to_string(floorIndex + 1) + "F";
        }

        bool HasFloor(int32_t floorIndex) const {
            for (const auto& f : Floors) {
                if (f.FloorIndex == floorIndex) return true;
            }
            return false;
        }

        void EnsureFloorExists(int32_t floorIndex) {
            if (!HasFloor(floorIndex)) {
                FloorConfig f;
                f.FloorIndex = floorIndex;
                f.Name = DefaultFloorName(floorIndex);
                f.GridWidth = 16;
                f.GridDepth = 16;
                f.Visible = true;
                Floors.push_back(f);
                std::sort(Floors.begin(), Floors.end(), [](const FloorConfig& a, const FloorConfig& b) {
                    return a.FloorIndex < b.FloorIndex;
                });
            }
        }

        bool AddFloor(int32_t floorIndex, const std::string& name = "") {
            if (HasFloor(floorIndex)) return false;
            FloorConfig f;
            f.FloorIndex = floorIndex;
            f.Name = name.empty() ? DefaultFloorName(floorIndex) : name;
            f.GridWidth = 16;
            f.GridDepth = 16;
            f.Visible = true;
            Floors.push_back(f);
            std::sort(Floors.begin(), Floors.end(), [](const FloorConfig& a, const FloorConfig& b) {
                return a.FloorIndex < b.FloorIndex;
            });
            return true;
        }

        bool RemoveFloor(int32_t floorIndex, std::vector<PlacedObject>& outRemovedObjects) {
            if (Floors.size() <= 1) return false; // 最低1フロアは維持
            outRemovedObjects.clear();
            for (const auto& o : Objects) {
                if (o.FloorIndex == floorIndex) {
                    outRemovedObjects.push_back(o);
                }
            }
            auto oIt = std::remove_if(Objects.begin(), Objects.end(),
                [floorIndex](const PlacedObject& o) { return o.FloorIndex == floorIndex; });
            Objects.erase(oIt, Objects.end());

            auto fIt = std::remove_if(Floors.begin(), Floors.end(),
                [floorIndex](const FloorConfig& f) { return f.FloorIndex == floorIndex; });
            Floors.erase(fIt, Floors.end());
            return true;
        }

        bool SwapFloorIndices(int32_t floorA, int32_t floorB) {
            if (floorA == floorB) return false;
            if (!HasFloor(floorA) || !HasFloor(floorB)) return false;

            // オブジェクトの FloorIndex および Y 座標をスワップ
            for (auto& o : Objects) {
                if (o.FloorIndex == floorA) {
                    o.FloorIndex = floorB;
                    o.Cell.Floor = floorB;
                    o.Position.y += static_cast<float>(floorB - floorA) * GRID_FLOOR_HEIGHT;
                } else if (o.FloorIndex == floorB) {
                    o.FloorIndex = floorA;
                    o.Cell.Floor = floorA;
                    o.Position.y += static_cast<float>(floorA - floorB) * GRID_FLOOR_HEIGHT;
                }
            }

            // フロア名・設定のスワップ
            FloorConfig* cfgA = FindFloorMut(floorA);
            FloorConfig* cfgB = FindFloorMut(floorB);
            if (cfgA && cfgB) {
                std::swap(cfgA->Name, cfgB->Name);
                std::swap(cfgA->Visible, cfgB->Visible);
            }
            return true;
        }

        const FloorConfig* FindFloor(int32_t floorIndex) const {
            for (const auto& f : Floors) {
                if (f.FloorIndex == floorIndex) return &f;
            }
            return nullptr;
        }

        FloorConfig* FindFloorMut(int32_t floorIndex) {
            for (auto& f : Floors) {
                if (f.FloorIndex == floorIndex) return &f;
            }
            return nullptr;
        }

        int32_t GetMinFloor() const {
            if (Floors.empty()) return 0;
            int32_t m = Floors[0].FloorIndex;
            for (const auto& f : Floors) if (f.FloorIndex < m) m = f.FloorIndex;
            return m;
        }

        int32_t GetMaxFloor() const {
            if (Floors.empty()) return 0;
            int32_t m = Floors[0].FloorIndex;
            for (const auto& f : Floors) if (f.FloorIndex > m) m = f.FloorIndex;
            return m;
        }

        // ================================================================
        // オブジェクトプロパティ更新ヘルパー (Phase 7 インスペクタ用)
        // ================================================================
        bool UpdateObjectPhase(uint64_t id, MaterialPhase newPhase) {
            PlacedObject* o = FindObjectMut(id);
            if (!o) return false;
            o->Phase = newPhase;
            return true;
        }

        bool UpdateObjectInitialOrb(uint64_t id, bool hasOrb) {
            PlacedObject* o = FindObjectMut(id);
            if (!o || o->Category != AssetCategory::Pedestal) return false;
            o->HasInitialOrb = hasOrb;
            return true;
        }

        bool RotateObject(uint64_t id, float deltaDeg) {
            PlacedObject* o = FindObjectMut(id);
            if (!o) return false;
            o->Rotation.y = std::fmod(o->Rotation.y + deltaDeg + 360.0f, 360.0f);
            return true;
        }

        bool MoveObjectToFloor(uint64_t id, int32_t targetFloor) {
            PlacedObject* o = FindObjectMut(id);
            if (!o) return false;
            EnsureFloorExists(targetFloor);
            int32_t diff = targetFloor - o->FloorIndex;
            o->FloorIndex = targetFloor;
            o->Cell.Floor = targetFloor;
            o->Position.y += static_cast<float>(diff) * GRID_FLOOR_HEIGHT;
            return true;
        }

        // ================================================================
        // ステージ整合性検証 (仕様書 11.1)
        // ================================================================
        StageValidationResult Validate() const {
            StageValidationResult res;

            for (const auto& obj : Objects) {
                if (obj.AssetId == "StartDais") {
                    res.StartPointCount++;
                } else if (obj.AssetId == "GoalChest") {
                    res.GoalPointCount++;
                } else if (obj.Category == AssetCategory::Pedestal && obj.HasInitialOrb) {
                    res.InitialOrbCount++;
                }
            }

            // 1. スタート地点が全階層の中で正確に1つ配置されているか
            if (res.StartPointCount == 0) {
                res.IsValid = false;
                res.Errors.push_back("Start Point is missing. Exactly 1 Start Point must be placed.");
            } else if (res.StartPointCount > 1) {
                res.IsValid = false;
                res.Errors.push_back("Multiple Start Points detected. Only 1 Start Point is allowed.");
            }

            // 2. ゴール地点が全階層の中で正確に1つ配置されているか
            if (res.GoalPointCount == 0) {
                res.IsValid = false;
                res.Errors.push_back("Goal Point is missing. Exactly 1 Goal Point must be placed.");
            } else if (res.GoalPointCount > 1) {
                res.IsValid = false;
                res.Errors.push_back("Multiple Goal Points detected. Only 1 Goal Point is allowed.");
            }

            // 3. 初期宝玉がステージ内に少なくとも1個以上存在するか
            if (res.InitialOrbCount == 0) {
                res.IsValid = false;
                res.Errors.push_back("No Luminous Orb found. At least one Pedestal must have 'Initial Orb' enabled.");
            }

            return res;
        }

        StageValidationResult ValidateStage() const {
            return Validate();
        }

        // ================================================================
        // JSON シリアライズ & デシリアライズ
        // ================================================================
        std::string ToJsonString(int indent = 2) const {
            json root;
            root["name"] = Name;

            json floorsArray = json::array();
            for (const auto& f : Floors) {
                json fJson;
                fJson["floor_index"] = f.FloorIndex;
                fJson["name"] = f.Name;
                fJson["grid_width"] = f.GridWidth;
                fJson["grid_depth"] = f.GridDepth;
                fJson["visible"] = f.Visible;
                floorsArray.push_back(fJson);
            }
            root["floors"] = floorsArray;

            json objsArray = json::array();
            for (const auto& o : Objects) {
                json oJson;
                oJson["instance_id"] = o.InstanceId;
                oJson["asset_id"] = o.AssetId;
                oJson["category"] = static_cast<int>(o.Category);
                oJson["placement"] = static_cast<int>(o.Placement);
                oJson["floor"] = o.FloorIndex;
                oJson["cell"] = { o.Cell.X, o.Cell.Z };
                oJson["edge"] = static_cast<int>(o.Edge);
                oJson["pos"] = { o.Position.x, o.Position.y, o.Position.z };
                oJson["rot"] = { o.Rotation.x, o.Rotation.y, o.Rotation.z };
                oJson["phase"] = static_cast<int>(o.Phase);
                oJson["has_initial_orb"] = o.HasInitialOrb;
                oJson["spawn_facing"] = static_cast<int>(o.Facing);
                oJson["has_light"] = o.HasLight;
                oJson["light_color"] = { o.LightColor.x, o.LightColor.y, o.LightColor.z };
                oJson["light_intensity"] = o.LightIntensity;
                oJson["light_radius"] = o.LightRadius;
                objsArray.push_back(oJson);
            }
            root["objects"] = objsArray;

            return root.dump(indent);
        }

        bool FromJsonString(const std::string& jsonStr, std::string& outError) {
            try {
                json root = json::parse(jsonStr);
                Name = root.value("name", "Untitled Stage");

                Floors.clear();
                if (root.contains("floors") && root["floors"].is_array()) {
                    for (const auto& fJson : root["floors"]) {
                        FloorConfig f;
                        f.FloorIndex = fJson.value("floor_index", 0);
                        f.Name = fJson.value("name", "1F");
                        f.GridWidth = fJson.value("grid_width", 16);
                        f.GridDepth = fJson.value("grid_depth", 16);
                        f.Visible = fJson.value("visible", true);
                        Floors.push_back(f);
                    }
                }

                Objects.clear();
                if (root.contains("objects") && root["objects"].is_array()) {
                    for (const auto& oJson : root["objects"]) {
                        PlacedObject o;
                        o.InstanceId = oJson.value("instance_id", 0ULL);
                        o.AssetId = oJson.value("asset_id", "Floor_1x1");
                        o.Category = static_cast<AssetCategory>(oJson.value("category", 0));
                        o.Placement = static_cast<PlacementType>(oJson.value("placement", 0));
                        o.FloorIndex = oJson.value("floor", 0);

                        if (oJson.contains("cell") && oJson["cell"].is_array() && oJson["cell"].size() >= 2) {
                            o.Cell.X = oJson["cell"][0];
                            o.Cell.Z = oJson["cell"][1];
                            o.Cell.Floor = o.FloorIndex;
                        }
                        o.Edge = static_cast<CellEdge>(oJson.value("edge", 0));

                        if (oJson.contains("pos") && oJson["pos"].is_array() && oJson["pos"].size() >= 3) {
                            o.Position.x = oJson["pos"][0];
                            o.Position.y = oJson["pos"][1];
                            o.Position.z = oJson["pos"][2];
                        }
                        if (oJson.contains("rot") && oJson["rot"].is_array() && oJson["rot"].size() >= 3) {
                            o.Rotation.x = oJson["rot"][0];
                            o.Rotation.y = oJson["rot"][1];
                            o.Rotation.z = oJson["rot"][2];
                        }

                        o.Phase = static_cast<MaterialPhase>(oJson.value("phase", 0));
                        o.HasInitialOrb = oJson.value("has_initial_orb", false);
                        o.Facing = static_cast<SpawnFacing>(oJson.value("spawn_facing", 0));

                        o.HasLight = oJson.value("has_light", false);
                        if (oJson.contains("light_color") && oJson["light_color"].is_array() && oJson["light_color"].size() >= 3) {
                            o.LightColor.x = oJson["light_color"][0];
                            o.LightColor.y = oJson["light_color"][1];
                            o.LightColor.z = oJson["light_color"][2];
                        }
                        o.LightIntensity = oJson.value("light_intensity", 20.0f);
                        o.LightRadius = oJson.value("light_radius", 10.0f);

                        Objects.push_back(o);
                    }
                }
                return true;
            } catch (const std::exception& e) {
                outError = std::string("JSON Parse Error: ") + e.what();
                return false;
            }
        }

        bool SaveToFile(const std::string& filepath, std::string& outError) const {
            try {
                std::filesystem::path p(filepath);
                if (p.has_parent_path()) {
                    std::filesystem::create_directories(p.parent_path());
                }
                std::ofstream ofs(filepath);
                if (!ofs.is_open()) {
                    outError = "Failed to open file for writing: " + filepath;
                    return false;
                }
                ofs << ToJsonString();
                return true;
            } catch (const std::exception& e) {
                outError = e.what();
                return false;
            }
        }

        bool LoadFromFile(const std::string& filepath, std::string& outError) {
            try {
                std::ifstream ifs(filepath);
                if (!ifs.is_open()) {
                    outError = "Failed to open file for reading: " + filepath;
                    return false;
                }
                std::stringstream ss;
                ss << ifs.rdbuf();
                return FromJsonString(ss.str(), outError);
            } catch (const std::exception& e) {
                outError = e.what();
                return false;
            }
        }

        // ================================================================
        // サンプルパズルステージ生成 (仕様書のギミック実証用)
        // ================================================================
        static LuminousStage CreateSampleStage() {
            LuminousStage stage;
            stage.Name = "Stage 1: Inverted Path";

            std::string err;

            // 1. 5x5 の石畳床フロアを生成 (Floor 0)
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject floorObj;
                    floorObj.AssetId = "Floor_1x1";
                    floorObj.Category = AssetCategory::Floor;
                    floorObj.Placement = PlacementType::CellSnap;
                    floorObj.FloorIndex = 0;
                    floorObj.Cell = GridCoord{ x, z, 0 };
                    floorObj.Position = GridToWorldCenter(x, z, 0);

                    // 中央セル (2, 2) を「反転床」にする (光が当たると穴になり通れない)
                    if (x == 2 && z == 2) {
                        floorObj.Phase = MaterialPhase::Phase;
                    } else {
                        floorObj.Phase = MaterialPhase::Normal;
                    }
                    stage.AddObject(floorObj, err);
                }
            }

            // 2. 外周の通常壁を配置
            for (int x = 0; x < 5; ++x) {
                // North 辺 (z = 4, edge = North)
                PlacedObject wNorth;
                wNorth.AssetId = "Wall_Brick";
                wNorth.Category = AssetCategory::Wall;
                wNorth.Placement = PlacementType::EdgeSnap;
                wNorth.FloorIndex = 0;
                wNorth.Cell = GridCoord{ x, 4, 0 };
                wNorth.Edge = CellEdge::North;
                float yaw;
                wNorth.Position = EdgeToWorld(x, 4, 0, CellEdge::North, yaw);
                wNorth.Rotation.y = yaw;
                stage.AddObject(wNorth, err);

                // South 辺 (z = 0, edge = South)
                PlacedObject wSouth;
                wSouth.AssetId = "Wall_Brick";
                wSouth.Category = AssetCategory::Wall;
                wSouth.Placement = PlacementType::EdgeSnap;
                wSouth.FloorIndex = 0;
                wSouth.Cell = GridCoord{ x, 0, 0 };
                wSouth.Edge = CellEdge::South;
                wSouth.Position = EdgeToWorld(x, 0, 0, CellEdge::South, yaw);
                wSouth.Rotation.y = yaw;
                stage.AddObject(wSouth, err);
            }

            for (int z = 0; z < 5; ++z) {
                // East 辺 (x = 4, edge = East)
                PlacedObject wEast;
                wEast.AssetId = "Wall_Brick";
                wEast.Category = AssetCategory::Wall;
                wEast.Placement = PlacementType::EdgeSnap;
                wEast.FloorIndex = 0;
                wEast.Cell = GridCoord{ 4, z, 0 };
                wEast.Edge = CellEdge::East;
                float yaw;
                wEast.Position = EdgeToWorld(4, z, 0, CellEdge::East, yaw);
                wEast.Rotation.y = yaw;
                stage.AddObject(wEast, err);

                // West 辺 (x = 0, edge = West)
                PlacedObject wWest;
                wWest.AssetId = "Wall_Brick";
                wWest.Category = AssetCategory::Wall;
                wWest.Placement = PlacementType::EdgeSnap;
                wWest.FloorIndex = 0;
                wWest.Cell = GridCoord{ 0, z, 0 };
                wWest.Edge = CellEdge::West;
                wWest.Position = EdgeToWorld(0, z, 0, CellEdge::West, yaw);
                wWest.Rotation.y = yaw;
                stage.AddObject(wWest, err);
            }

            // 2-2. ダンジョン装飾プロップ (石柱・木箱・樽)
            PlacedObject pillar1;
            pillar1.AssetId = "Prop_Pillar";
            pillar1.Category = AssetCategory::Prop;
            pillar1.Placement = PlacementType::FreeAttach;
            pillar1.FloorIndex = 0;
            pillar1.Position = XMFLOAT3(0.5f, 0.0f, 0.5f);
            stage.AddObject(pillar1, err);

            PlacedObject pillar2;
            pillar2.AssetId = "Prop_Pillar";
            pillar2.Category = AssetCategory::Prop;
            pillar2.Placement = PlacementType::FreeAttach;
            pillar2.FloorIndex = 0;
            pillar2.Position = XMFLOAT3(9.98f, 0.0f, 0.5f);
            stage.AddObject(pillar2, err);

            PlacedObject barrel;
            barrel.AssetId = "Prop_Barrel";
            barrel.Category = AssetCategory::Prop;
            barrel.Placement = PlacementType::FreeAttach;
            barrel.FloorIndex = 0;
            barrel.Position = XMFLOAT3(1.2f, 0.0f, 9.2f);
            stage.AddObject(barrel, err);

            PlacedObject crate;
            crate.AssetId = "Prop_WoodenBox";
            crate.Category = AssetCategory::Prop;
            crate.Placement = PlacementType::FreeAttach;
            crate.FloorIndex = 0;
            crate.Position = XMFLOAT3(9.3f, 0.0f, 1.2f);
            stage.AddObject(crate, err);

            // 3. 部屋の中央を仕切る「反転壁」を配置 (光照射ですり抜け可能)
            PlacedObject phaseWall;
            phaseWall.AssetId = "Wall_Brick";
            phaseWall.Category = AssetCategory::Wall;
            phaseWall.Placement = PlacementType::EdgeSnap;
            phaseWall.FloorIndex = 0;
            phaseWall.Cell = GridCoord{ 2, 3, 0 };
            phaseWall.Edge = CellEdge::South; // セル (2,3) と (2,2) の境界
            float pYaw;
            phaseWall.Position = EdgeToWorld(2, 3, 0, CellEdge::South, pYaw);
            phaseWall.Rotation.y = pYaw;
            phaseWall.Phase = MaterialPhase::Phase; // 反転壁1 (開口部中央)
            stage.AddObject(phaseWall, err);

            // 反転壁2: (3, 3) South (光球面の弧が横切り、リングと結晶壁が見える)
            PlacedObject phaseWall2;
            phaseWall2.AssetId = "Wall_Brick";
            phaseWall2.Category = AssetCategory::Wall;
            phaseWall2.Placement = PlacementType::EdgeSnap;
            phaseWall2.FloorIndex = 0;
            phaseWall2.Cell = GridCoord{ 3, 3, 0 };
            phaseWall2.Edge = CellEdge::South;
            phaseWall2.Position = EdgeToWorld(3, 3, 0, CellEdge::South, pYaw);
            phaseWall2.Rotation.y = pYaw;
            phaseWall2.Phase = MaterialPhase::Phase;
            stage.AddObject(phaseWall2, err);

            // 4. 鉄格子（Grate）を配置 (光は透過するがプレイヤーは通れない)
            PlacedObject grate;
            grate.AssetId = "Wall_Grate";
            grate.Category = AssetCategory::Wall;
            grate.Placement = PlacementType::EdgeSnap;
            grate.FloorIndex = 0;
            grate.Cell = GridCoord{ 1, 3, 0 };
            grate.Edge = CellEdge::South;
            grate.Position = EdgeToWorld(1, 3, 0, CellEdge::South, pYaw);
            grate.Rotation.y = pYaw;
            stage.AddObject(grate, err);

            // 部屋の左右を仕切る剛体石壁 (0, 3) & (4, 3)
            PlacedObject wall0;
            wall0.AssetId = "Wall_Brick";
            wall0.Category = AssetCategory::Wall;
            wall0.Placement = PlacementType::EdgeSnap;
            wall0.FloorIndex = 0;
            wall0.Cell = GridCoord{ 0, 3, 0 };
            wall0.Edge = CellEdge::South;
            wall0.Position = EdgeToWorld(0, 3, 0, CellEdge::South, pYaw);
            wall0.Rotation.y = pYaw;
            stage.AddObject(wall0, err);

            PlacedObject wall4;
            wall4.AssetId = "Wall_Brick";
            wall4.Category = AssetCategory::Wall;
            wall4.Placement = PlacementType::EdgeSnap;
            wall4.FloorIndex = 0;
            wall4.Cell = GridCoord{ 4, 3, 0 };
            wall4.Edge = CellEdge::South;
            wall4.Position = EdgeToWorld(4, 3, 0, CellEdge::South, pYaw);
            wall4.Rotation.y = pYaw;
            stage.AddObject(wall4, err);

            // 5. スタート地点召喚陣を (0, 0) に配置
            PlacedObject startDais;
            startDais.AssetId = "StartDais";
            startDais.Category = AssetCategory::Special;
            startDais.Placement = PlacementType::CellSnap;
            startDais.FloorIndex = 0;
            startDais.Cell = GridCoord{ 0, 0, 0 };
            startDais.Position = GridToWorldCenter(0, 0, 0);
            startDais.Rotation.y = 45.0f; // 部屋の中心方向（斜め奥）を向く
            stage.AddObject(startDais, err);

            // 6. 台座 (Pedestal) を (1, 1) に配置、初期宝玉あり
            PlacedObject ped;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 1, 0);
            ped.HasInitialOrb = true; // 初期宝玉セット！
            stage.AddObject(ped, err);

            // 7. ゴール宝箱 (GoalChest) を最奥 (4, 4) に配置
            PlacedObject goal;
            goal.AssetId = "GoalChest";
            goal.Category = AssetCategory::Special;
            goal.Placement = PlacementType::FreeAttach;
            goal.FloorIndex = 0;
            goal.Position = GridToWorldCenter(4, 4, 0);
            goal.Rotation.y = 0.0f; // 鍵穴を手前(-Z)のプレイヤー側へ向け、蓋は奥(+Z)の壁側へ開く
            stage.AddObject(goal, err);

            // 8. 装飾ランプ: 壁かけ松明 (Prop_Torch) と 床置きろうそく (Prop_Candle)
            float tYaw = 0.0f;
            PlacedObject torch1;
            torch1.AssetId = "Prop_Torch";
            torch1.Category = AssetCategory::Prop;
            torch1.Placement = PlacementType::EdgeSnap;
            torch1.FloorIndex = 0;
            torch1.Cell = GridCoord{ 0, 2, 0 };
            torch1.Edge = CellEdge::West;
            torch1.Position = EdgeToWorld(0, 2, 0, CellEdge::West, tYaw);
            torch1.Position.x += 0.323f;
            torch1.Position.y = 1.8f;
            torch1.Rotation.y = 270.0f; // 部屋の内側(+X)を向く (West壁なので270度)
            torch1.HasLight = true;
            torch1.LightColor = { 1.0f, 0.70f, 0.28f };
            torch1.LightIntensity = 22.0f;
            torch1.LightRadius = 10.0f;
            stage.AddObject(torch1, err);

            PlacedObject candle1;
            candle1.AssetId = "Prop_Candle";
            candle1.Category = AssetCategory::Floor;
            candle1.Placement = PlacementType::FreeAttach;
            candle1.FloorIndex = 0;
            candle1.Cell = GridCoord{ 0, 1, 0 };
            candle1.Position = GridToWorldCenter(0, 1, 0);
            candle1.Position.x -= 0.5f; // 壁際に寄せる
            candle1.HasLight = true;
            candle1.LightColor = { 1.0f, 0.75f, 0.35f };
            candle1.LightIntensity = 10.0f;
            candle1.LightRadius = 6.0f;
            stage.AddObject(candle1, err);

            return stage;
        }

        // 多層階段探索ステージ (Floor 0 & Floor 1 吹き抜け・バルコニー構成)
        // ================================================================
        // Phase 2 検証用ステージ: 壁で四方を囲んだ台座に宝玉を 1 個だけ置く。
        // 光は水平方向へ漏れないため隣の反転床マス (2,2) は全面が影となり、
        // 「影＝床が残る」ので通常床 (2,1) から (2,3) へ通り抜けられること。
        // 宝玉はステージ中この 1 個のみ。
        // ================================================================
        static LuminousStage CreateShadowTraversalTestStage(bool encloseOrb = true) {
            LuminousStage stage;
            stage.Name = "Shadow Traversal Test";

            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject f;
                    f.InstanceId = stage.GenerateId();
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    if (x == 2 && z == 2) {
                        f.AssetId = "Floor_1x1";
                        f.Phase = MaterialPhase::Phase;
                    } else {
                        f.AssetId = "Floor_1x1";
                        f.Phase = MaterialPhase::Normal;
                    }
                    stage.Objects.push_back(f);
                }
            }

            // 台座 (1,2) を四方の通常壁で完全に囲う (encloseOrb=false は対照実験用)
            const CellEdge edges[4] = { CellEdge::North, CellEdge::South, CellEdge::East, CellEdge::West };
            for (int i = 0; encloseOrb && i < 4; ++i) {
                PlacedObject w;
                w.InstanceId = stage.GenerateId();
                w.AssetId = "Wall_Brick";
                w.Category = AssetCategory::Wall;
                w.Placement = PlacementType::EdgeSnap;
                w.FloorIndex = 0;
                w.Cell = GridCoord{ 1, 2, 0 };
                w.Edge = edges[i];
                float yaw = 0.0f;
                w.Position = EdgeToWorld(1, 2, 0, edges[i], yaw);
                w.Rotation.y = yaw;
                stage.Objects.push_back(w);
            }

            // 唯一の宝玉
            PlacedObject ped;
            ped.InstanceId = stage.GenerateId();
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 2, 0);
            ped.HasInitialOrb = true;
            stage.Objects.push_back(ped);

            // スポーン地点 (2,0)
            PlacedObject dais;
            dais.InstanceId = stage.GenerateId();
            dais.AssetId = "StartDais";
            dais.Category = AssetCategory::Special;
            dais.Placement = PlacementType::CellSnap;
            dais.FloorIndex = 0;
            dais.Cell = GridCoord{ 2, 0, 0 };
            dais.Position = GridToWorldCenter(2, 0, 0);
            stage.Objects.push_back(dais);

            return stage;
        }

        // ================================================================
        // Phase 2 検証用ステージ: 反転床タイルの内部に「影 <-> 光」の境界を作る。
        // 台座 (1,1) の宝玉に対し反転床 (2,2) の南辺へ通常壁を置くと、
        // 壁の東端を回り込む光でタイルが斜めに二分される。
        // 宝玉はステージ中この 1 個のみ。
        // ================================================================
        static LuminousStage CreateShadowEdgeTestStage() {
            LuminousStage stage;
            stage.Name = "Shadow Edge Test";

            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject f;
                    f.InstanceId = stage.GenerateId();
                    f.AssetId = "Floor_1x1";
                    f.Category = AssetCategory::Floor;
                    f.Placement = PlacementType::CellSnap;
                    f.FloorIndex = 0;
                    f.Cell = GridCoord{ x, z, 0 };
                    f.Position = GridToWorldCenter(x, z, 0);
                    f.Phase = (x == 2 && z == 2) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    stage.Objects.push_back(f);
                }
            }

            PlacedObject w;
            w.InstanceId = stage.GenerateId();
            w.AssetId = "Wall_Brick";
            w.Category = AssetCategory::Wall;
            w.Placement = PlacementType::EdgeSnap;
            w.FloorIndex = 0;
            w.Cell = GridCoord{ 2, 2, 0 };
            w.Edge = CellEdge::South;
            float wYaw = 0.0f;
            w.Position = EdgeToWorld(2, 2, 0, CellEdge::South, wYaw);
            w.Rotation.y = wYaw;
            stage.Objects.push_back(w);

            PlacedObject ped;
            ped.InstanceId = stage.GenerateId();
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 1, 0);
            ped.HasInitialOrb = true;
            stage.Objects.push_back(ped);

            PlacedObject dais;
            dais.InstanceId = stage.GenerateId();
            dais.AssetId = "StartDais";
            dais.Category = AssetCategory::Special;
            dais.Placement = PlacementType::CellSnap;
            dais.FloorIndex = 0;
            dais.Cell = GridCoord{ 4, 2, 0 };
            dais.Position = GridToWorldCenter(4, 2, 0);
            stage.Objects.push_back(dais);

            return stage;
        }

        static LuminousStage CreateMultiFloorSampleStage() {
            LuminousStage stage;
            stage.Name = "Stage 2: Vertical Ascension";
            std::string err;

            // 1. Floor 0 (5x5) 石畳
            for (int x = 0; x < 5; ++x) {
                for (int z = 0; z < 5; ++z) {
                    PlacedObject floorObj;
                    floorObj.AssetId = "Floor_1x1";
                    floorObj.Category = AssetCategory::Floor;
                    floorObj.Placement = PlacementType::CellSnap;
                    floorObj.FloorIndex = 0;
                    floorObj.Cell = GridCoord{ x, z, 0 };
                    floorObj.Position = GridToWorldCenter(x, z, 0);
                    floorObj.Phase = ((x == 2 || x == 3) && z == 2) ? MaterialPhase::Phase : MaterialPhase::Normal;
                    stage.AddObject(floorObj, err);
                }
            }

            // 2. Floor 0 外周壁
            for (int x = 0; x < 5; ++x) {
                PlacedObject wNorth;
                wNorth.AssetId = "Wall_Brick";
                wNorth.Category = AssetCategory::Wall;
                wNorth.Placement = PlacementType::EdgeSnap;
                wNorth.FloorIndex = 0;
                wNorth.Cell = GridCoord{ x, 4, 0 };
                wNorth.Edge = CellEdge::North;
                float yaw;
                wNorth.Position = EdgeToWorld(x, 4, 0, CellEdge::North, yaw);
                wNorth.Rotation.y = yaw;
                stage.AddObject(wNorth, err);

                PlacedObject wSouth;
                wSouth.AssetId = "Wall_Brick";
                wSouth.Category = AssetCategory::Wall;
                wSouth.Placement = PlacementType::EdgeSnap;
                wSouth.FloorIndex = 0;
                wSouth.Cell = GridCoord{ x, 0, 0 };
                wSouth.Edge = CellEdge::South;
                wSouth.Position = EdgeToWorld(x, 0, 0, CellEdge::South, yaw);
                wSouth.Rotation.y = yaw;
                stage.AddObject(wSouth, err);
            }

            for (int z = 0; z < 5; ++z) {
                PlacedObject wEast;
                wEast.AssetId = "Wall_Brick";
                wEast.Category = AssetCategory::Wall;
                wEast.Placement = PlacementType::EdgeSnap;
                wEast.FloorIndex = 0;
                wEast.Cell = GridCoord{ 4, z, 0 };
                wEast.Edge = CellEdge::East;
                float yaw;
                wEast.Position = EdgeToWorld(4, z, 0, CellEdge::East, yaw);
                wEast.Rotation.y = yaw;
                stage.AddObject(wEast, err);

                PlacedObject wWest;
                wWest.AssetId = "Wall_Brick";
                wWest.Category = AssetCategory::Wall;
                wWest.Placement = PlacementType::EdgeSnap;
                wWest.FloorIndex = 0;
                wWest.Cell = GridCoord{ 0, z, 0 };
                wWest.Edge = CellEdge::West;
                wWest.Position = EdgeToWorld(0, z, 0, CellEdge::West, yaw);
                wWest.Rotation.y = yaw;
                stage.AddObject(wWest, err);
            }

            // 3. 直線階段 (Stairs_Straight): Cell (3, 1) から (3, 2) にかけて配置
            PlacedObject stairs;
            stairs.AssetId = "Stairs_Straight";
            stairs.Category = AssetCategory::Stairs;
            stairs.Placement = PlacementType::FreeAttach;
            stairs.FloorIndex = 0;
            stairs.Position = XMFLOAT3(
                (3.0f + 0.5f) * GRID_CELL_SIZE,
                0.0f,
                2.0f * GRID_CELL_SIZE
            );
            stairs.Rotation.y = 0.0f; // 南(z=1)から北(z=3)へ向かって登る (モデル180度整合済)
            stage.AddObject(stairs, err);

            // 4. Floor 1 (2F) バルコニー床: Cell (3, 3), (3, 4), (4, 3), (4, 4)
            for (int x = 3; x <= 4; ++x) {
                for (int z = 3; z <= 4; ++z) {
                    PlacedObject f1;
                    f1.AssetId = "Floor_1x1";
                    f1.Category = AssetCategory::Floor;
                    f1.Placement = PlacementType::CellSnap;
                    f1.FloorIndex = 1;
                    f1.Cell = GridCoord{ x, z, 1 };
                    f1.Position = GridToWorldCenter(x, z, 1);
                    stage.AddObject(f1, err);
                }
            }

            // 5. Floor 1 手すり (Wall_Railing): バルコニーの南側・西側
            PlacedObject railS;
            railS.AssetId = "Wall_Railing";
            railS.Category = AssetCategory::Wall;
            railS.Placement = PlacementType::EdgeSnap;
            railS.FloorIndex = 1;
            railS.Cell = GridCoord{ 4, 3, 1 };
            railS.Edge = CellEdge::South;
            float rYaw;
            railS.Position = EdgeToWorld(4, 3, 1, CellEdge::South, rYaw);
            railS.Rotation.y = rYaw;
            stage.AddObject(railS, err);

            PlacedObject railW;
            railW.AssetId = "Wall_Railing";
            railW.Category = AssetCategory::Wall;
            railW.Placement = PlacementType::EdgeSnap;
            railW.FloorIndex = 1;
            railW.Cell = GridCoord{ 3, 4, 1 };
            railW.Edge = CellEdge::West;
            railW.Position = EdgeToWorld(3, 4, 1, CellEdge::West, rYaw);
            railW.Rotation.y = rYaw;
            stage.AddObject(railW, err);

            // 6. 召喚陣 (StartDais) を 1F (0, 0, 0) に配置
            PlacedObject startDais;
            startDais.AssetId = "StartDais";
            startDais.Category = AssetCategory::Special;
            startDais.Placement = PlacementType::CellSnap;
            startDais.FloorIndex = 0;
            startDais.Cell = GridCoord{ 0, 0, 0 };
            startDais.Position = GridToWorldCenter(0, 0, 0);
            startDais.Rotation.y = 45.0f;
            stage.AddObject(startDais, err);

            // 7. 台座 (Pedestal) を 1F (1, 1, 0) に配置
            PlacedObject ped;
            ped.AssetId = "Pedestal";
            ped.Category = AssetCategory::Pedestal;
            ped.Placement = PlacementType::FreeAttach;
            ped.FloorIndex = 0;
            ped.Position = GridToWorldCenter(1, 1, 0);
            ped.HasInitialOrb = true;
            stage.AddObject(ped, err);

            // 8. ゴール宝箱 (GoalChest) を 2F バルコニー最奥 (4, 4, 1) に配置！
            PlacedObject goal;
            goal.AssetId = "GoalChest";
            goal.Category = AssetCategory::Special;
            goal.Placement = PlacementType::FreeAttach;
            goal.FloorIndex = 1;
            goal.Position = GridToWorldCenter(4, 4, 1);
            goal.Rotation.y = 0.0f; // 鍵穴を手前(-Z)の進入路側へ向け、蓋は奥(+Z)の壁側へ開く
            stage.AddObject(goal, err);

            // 9. 装飾ランプ: 壁かけ松明 (Prop_Torch) と 床置きろうそく (Prop_Candle)
            float tYaw2 = 0.0f;
            PlacedObject torch2;
            torch2.AssetId = "Prop_Torch";
            torch2.Category = AssetCategory::Prop;
            torch2.Placement = PlacementType::EdgeSnap;
            torch2.FloorIndex = 0;
            torch2.Cell = GridCoord{ 0, 2, 0 };
            torch2.Edge = CellEdge::West;
            torch2.Position = EdgeToWorld(0, 2, 0, CellEdge::West, tYaw2);
            torch2.Position.x += 0.323f;
            torch2.Position.y = 1.8f;
            torch2.Rotation.y = 270.0f; // 部屋の内側(+X)を向く (West壁なので270度)
            torch2.HasLight = true;
            torch2.LightColor = { 1.0f, 0.70f, 0.28f };
            torch2.LightIntensity = 22.0f;
            torch2.LightRadius = 10.0f;
            stage.AddObject(torch2, err);

            PlacedObject candle2;
            candle2.AssetId = "Prop_Candle";
            candle2.Category = AssetCategory::Floor;
            candle2.Placement = PlacementType::FreeAttach;
            candle2.FloorIndex = 0;
            candle2.Cell = GridCoord{ 0, 1, 0 };
            candle2.Position = GridToWorldCenter(0, 1, 0);
            candle2.Position.x -= 0.5f; // 壁際に寄せる
            candle2.HasLight = true;
            candle2.LightColor = { 1.0f, 0.75f, 0.35f };
            candle2.LightIntensity = 10.0f;
            candle2.LightRadius = 6.0f;
            stage.AddObject(candle2, err);

            return stage;
        }
    };
}
