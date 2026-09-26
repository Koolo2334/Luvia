module;
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <cassert>
#include <cmath>
#include <format>
#include <memory>
#include <string>
#include <vector>
#include <iterator>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.GameTests;

import Engine.Debug.Log;
import Engine.Core.Reflection;
import App.ReflectionRegistration;
import Engine.Graphics.Components.Camera;
import Engine.Graphics.Components.Light;
import App.Luminous.Types;
import App.Luminous.Optics;
import App.Luminous.StageData;
import App.Luminous.TerrainSystem;
import App.Luminous.PlayerSystem;
import App.Luminous.OrbSystem;
import App.Luminous.InputConfig;
import App.Luminous.Profile;   // 自作ステージの一覧・進み具合のファイル
import App.Luminous.StageCatalog;   // 基本ステージの一覧
import Engine.Common.Config;   // Paths
import Engine.Core.SaveGame;   // 進み具合はエンジンのセーブデータへ (22 の R-10-6)

// ============================================================================
// ゲームの自己テスト
//
//   Luvia (Luminous Shift) の単体テスト。exe ではなくゲームのプロジェクトに置く。
//   exe は RunGameSelfTests() を呼ぶだけで、中身を知らない。
//
//   中身は TestGame から切り出したもので、テスト本体は 1 行も変えていない。
// ============================================================================

export namespace App {
    void RunGameSelfTests();
}

namespace App {

    void RunGameSelfTests() {
    		// ====================================================================
    		// Luminous Shift Unit Tests (Optics, Terrain, Validation, Serialization)
    		// ====================================================================
    		{
    			using namespace App::Luminous;
    			Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Starting Luminous Shift Unit Tests...");
    			std::string opticsErr;
    			bool opticsOk = OpticsEngine::RunUnitTests(opticsErr);
    			assert(opticsOk);
    			Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "OpticsEngine Unit Tests PASSED.");

    			// Phase 2: occlusion-aware pedestal collider bake
    			std::string bakeErr;
    			bool bakeOk = App::Luminous::TerrainSystem::RunOcclusionBakeUnitTests(bakeErr);
    			if (!bakeOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "OrbOcclusion Bake Unit Tests FAILED: " + bakeErr);
    			}
    			assert(bakeOk);
    			if (bakeOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "OrbOcclusion Bake Unit Tests PASSED.");
    			}

    			std::string shadowErr;
    			bool shadowOk = App::Luminous::PlayerSystem::RunPhaseFloorShadowCollisionTests(shadowErr);
    			if (!shadowOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "PhaseFloor Shadow Collision Tests FAILED: " + shadowErr);
    			}
    			assert(shadowOk);
    			if (shadowOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "PhaseFloor Shadow Collision Tests PASSED.");
    			}

    			std::string traverseReport;
    			bool traverseOk = App::Luminous::PlayerSystem::RunPhaseFloorTraversalTest(traverseReport);
    			if (!traverseOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "PhaseFloor Traversal Test FAILED: " + traverseReport);
    			}
    			assert(traverseOk);
    			if (traverseOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "PhaseFloor Traversal Test PASSED: " + traverseReport);
    			}

    			std::string stairsReport;
    			bool stairsOk = App::Luminous::PlayerSystem::RunStairsClimbTest(stairsReport);
    			if (!stairsOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Stairs Climb Test FAILED: " + stairsReport);
    			}
    			assert(stairsOk);
    			if (stairsOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Stairs Climb Test PASSED: " + stairsReport);
    			}

    			std::string edgeReport;
    			bool edgeOk = App::Luminous::PlayerSystem::RunPhaseFloorShadowEdgeTest(edgeReport);
    			if (!edgeOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "PhaseFloor Shadow Edge Test FAILED: " + edgeReport);
    			}
    			assert(edgeOk);
    			if (edgeOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "PhaseFloor Shadow Edge Test PASSED: " + edgeReport);
    			}

    			std::string animReport;
    			bool animOk = App::Luminous::PlayerSystem::RunOrbAnimationCollisionTest(animReport);
    			if (!animOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Orb Animation Collision Test FAILED: " + animReport);
    			}
    			assert(animOk);
    			if (animOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Orb Animation Collision Test PASSED: " + animReport);
    			}

    			std::string inputReport;
    			bool inputOk = App::Luminous::LuminousInputRegulation::Validate(inputReport);
    			if (!inputOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Input Regulation Test FAILED: " + inputReport);
    			}
    			assert(inputOk);
    			if (inputOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Input Regulation Test PASSED: " + inputReport);
    			}

    			std::string clipReport;
    			bool clipOk = App::Luminous::OrbSystem::RunHeldOrbWallClipTest(clipReport);
    			if (!clipOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Held Orb Wall Clip Test FAILED: " + clipReport);
    			}
    			assert(clipOk);
    			if (clipOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Held Orb Wall Clip Test PASSED: " + clipReport);
    			}

    			std::string archReport;
    			bool archOk = App::Luminous::OrbSystem::RunHeldOrbArchwayTest(archReport);
    			if (!archOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Held Orb Archway Test FAILED: " + archReport);
    			}
    			assert(archOk);
    			if (archOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Held Orb Archway Test PASSED: " + archReport);
    			}

    			std::string transReport;
    			bool transOk = App::Luminous::PlayerSystem::RunHeldOrbPhaseTransparencyTest(transReport);
    			if (!transOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__, "Held Orb Phase Transparency Test FAILED: " + transReport);
    			}
    			assert(transOk);
    			if (transOk) {
    				Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Held Orb Phase Transparency Test PASSED: " + transReport);
    			}

    			// Stage validation tests
    			auto sampleStage = LuminousStage::CreateSampleStage();
    			auto valRes = sampleStage.ValidateStage();
    			assert(valRes.IsValid);
    			assert(valRes.StartPointCount == 1);
    			assert(valRes.GoalPointCount == 1);
    			assert(valRes.InitialOrbCount >= 1);
    			Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Sample Stage Validation PASSED.");

    			// Invalidation test: missing Start
    			auto invalidStage = sampleStage;
    			invalidStage.RemoveByAssetId("StartDais");
    			auto invalidVal = invalidStage.ValidateStage();
    			assert(!invalidVal.IsValid);
    			assert(invalidVal.StartPointCount == 0);

    			// Serialization test
    			std::string jsonStr = sampleStage.ToJsonString();
    			LuminousStage reloadedStage;
    			std::string parseErr;
    			bool parseOk = reloadedStage.FromJsonString(jsonStr, parseErr);
    			assert(parseOk);
    			assert(reloadedStage.Objects.size() == sampleStage.Objects.size());
    			Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "Stage JSON Serialization PASSED.");

    			Engine::Debug::LogOutput(Engine::Debug::LogLevel::Info, "App", __FILE__, __LINE__, "All Luminous Shift Unit Tests PASSED successfully.");
    		}

        // リフレクションの登録漏れを見張る (S-4 でエンジンのテストから移した)。
        //
        //   登録が落ちると「保存したのに戻らない」という形で、静かに壊れる。
        //   エンジンはゲームの型を知らないので、見張るのはゲームの役目。
        {
            RegisterAppComponents();   // 何度呼んでも同じ結果になる
            auto& reflection = Engine::Core::ComponentRegistry::Get();
            const struct { const char* Type; const char* Field; } expected[] = {
                { "SkyLight",       "Environment" },
                { "LuminousPlayer", "Position" },
            };
            int missing = 0;
            for (const auto& entry : expected) {
                const Engine::Core::ComponentType* type = reflection.Find(entry.Type);
                if (!type) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  component '{}' is not registered", entry.Type));
                    ++missing;
                    continue;
                }
                if (!type->FindField(entry.Field)) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  component '{}' has no field '{}'", entry.Type, entry.Field));
                    ++missing;
                }
            }
            // 名札 (中身を持たない型)。付いているかどうかだけを保存する
            const char* tags[] = { "GBufferPassTag", "ShadowPassTag", "TranslucentPassTag",
                                   "CharacterPassTag", "OrbOccluderTag" };
            for (const char* name : tags) {
                const Engine::Core::ComponentType* type = reflection.Find(name);
                if (!type) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  tag '{}' is not registered", name));
                    ++missing;
                    continue;
                }
                if (!type->IsTag || !type->Fields.empty()) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  tag '{}' should have no fields", name));
                    ++missing;
                }
            }

            // 列挙は名前つきで登録されていること (数値で保存すると、あとで値を
            // 足したときに保存済みのものが別の値に化ける)
            // 列挙は名前つきで登録されていること (数値で保存すると、あとで値を
            // 足したときに保存済みのものが別の値に化ける)。
            //   Luvia には今のところ、保存する列挙が無い
            const struct { const char* Type; const char* Field; } enumFields[1] = {
                { nullptr, nullptr },
            };
            for (const auto& entry : enumFields) {
                if (!entry.Type) continue;
                const Engine::Core::ComponentType* type = reflection.Find(entry.Type);
                const Engine::Core::FieldInfo* field = type ? type->FindField(entry.Field) : nullptr;
                if (!field || field->Type != Engine::Core::FieldType::Enum || !field->Enum) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  '{}::{}' should be an enum with registered names", entry.Type, entry.Field));
                    ++missing;
                }
            }

            // 実行時にしか意味がない欄は保存しないこと
            const struct { const char* Type; const char* Field; } mustNotExist[] = {
                // 視線の先の対象は毎フレーム決まるので保存しない
                { "LuminousPlayer", "LookTarget" },
            };
            for (const auto& entry : mustNotExist) {
                const Engine::Core::ComponentType* type = reflection.Find(entry.Type);
                if (type && type->FindField(entry.Field)) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  '{}::{}' is runtime-only and must not be saved", entry.Type, entry.Field));
                    ++missing;
                }
            }

            // 直接書くと壊れる欄・実行時に決まる欄は、編集させないこと (E-01)
            const struct { const char* Type; const char* Field; } readOnly[] = {
                { "LuminousPlayer",  "Velocity" },
                { "LuminousPlayer",  "IsGrounded" },
                { "LuminousPlayer",  "HeldOrbId" },
            };
            for (const auto& entry : readOnly) {
                const Engine::Core::ComponentType* type = reflection.Find(entry.Type);
                const Engine::Core::FieldInfo* field = type ? type->FindField(entry.Field) : nullptr;
                if (!field) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  '{}::{}' is missing", entry.Type, entry.Field));
                    ++missing;
                    continue;
                }
                if (!field->IsReadOnly()) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  '{}::{}' should be read-only", entry.Type, entry.Field));
                    ++missing;
                }
            }

            Engine::Debug::LogOutput(missing == 0 ? Engine::Debug::LogLevel::Info : Engine::Debug::LogLevel::Error,
                "App", __FILE__, __LINE__,
                std::format("Game reflection coverage: {} components + {} tags checked, {} missing",
                    static_cast<int>(std::size(expected)), static_cast<int>(std::size(tags)), missing));
        }

        // ====================================================================
        // ファイルはプロジェクトの中で読み書きする。
        //   エディタの Game ビューで遊ぶとき、今の作業フォルダはエディタの置き場になる。
        //   以前は相対パスのまま開いていたので、進み具合をエディタの置き場へ書き、
        //   自作ステージの一覧も空だった。作業フォルダを一時フォルダへ移して確かめる
        // ====================================================================
        {
            using namespace App::Luminous;
            char before[MAX_PATH] = {};
            GetCurrentDirectoryA(MAX_PATH, before);
            char temp[MAX_PATH] = {};
            GetTempPathA(MAX_PATH, temp);
            SetCurrentDirectoryA(temp);

            bool foundStage = false;
            bool stageLoads = false;
            for (const auto& [name, path] : ProfileManager::Get().GetCustomStages()) {
                if (name != "CustomDungeon.json") continue;
                foundStage = true;
                LuminousStage stage;
                std::string error;
                stageLoads = stage.LoadFromFile(Engine::Common::Paths::ResolveString(path), error);
            }
            const std::string project = Engine::Common::Paths::ToUtf8(Engine::Common::Paths::Project());
            const std::string profile = Engine::Common::Paths::ToUtf8(ProfileManager::Get().SaveFile());
            const bool profileInProject = profile.rfind(project, 0) == 0;

            SetCurrentDirectoryA(before);

            // 基本ステージのファイルには名前がある (クリア画面に出る。ステージ 1 だけ空で "Custom Stage" と出ていた)
            int unnamed = 0;
            for (const auto& entry : LuminousStageCatalog::BaseStages) {
                LuminousStage stage;
                std::string error;
                if (!stage.LoadFromFile(Engine::Common::Paths::ResolveString(entry.StageFilePath), error) || stage.Name.empty()) {
                    Engine::Debug::LogOutput(Engine::Debug::LogLevel::Error, "App", __FILE__, __LINE__,
                        std::format("  The base stage '{}' has no name (or cannot be read: {})", entry.StageFilePath, error));
                    ++unnamed;
                }
            }
            Engine::Debug::LogOutput(unnamed == 0 ? Engine::Debug::LogLevel::Info : Engine::Debug::LogLevel::Error,
                "App", __FILE__, __LINE__,
                std::format("Base stage names: {} stages, {} without a name", LuminousStageCatalog::Count(), unnamed));

            const bool ok = foundStage && stageLoads && profileInProject;
            Engine::Debug::LogOutput(ok ? Engine::Debug::LogLevel::Info : Engine::Debug::LogLevel::Error,
                "App", __FILE__, __LINE__,
                std::format("Project-relative files (another working directory): custom stage found={} loads={}, "
                            "profile in the project={} ('{}')", foundStage, stageLoads, profileInProject, profile));
        }

        // ====================================================================
        // 進み具合はエンジンのセーブデータ (スロット "Profile") に書く (22 の R-10-6)。
        //   まだ無ければ前のファイル (Assets/Data/profile.json) を初めの進み具合として読み、そのファイルは書き換えない。
        //   本物のセーブを触らないよう、テストの間だけ保存先を一時フォルダにする
        // ====================================================================
        {
            using namespace App::Luminous;
            namespace fs = std::filesystem;
            wchar_t tempBuffer[MAX_PATH] = {};
            GetTempPathW(MAX_PATH, tempBuffer);
            const fs::path root = fs::path(tempBuffer) / std::format(L"RaDX_LuviaProfileTest_{}", GetCurrentProcessId());
            std::error_code ec;
            fs::remove_all(root, ec);
            Engine::Core::SaveGame::SetDirectory(root);

            auto readAll = [](const fs::path& file) {
                std::ifstream in(file, std::ios::binary);
                return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
            };
            const fs::path legacy = Engine::Common::Paths::Resolve(fs::path("Assets/Data/profile.json"));
            const std::string legacyBefore = readAll(legacy);
            int legacyHighest = 0;
            if (const size_t at = legacyBefore.find("\"highest_cleared_stage\""); at != std::string::npos) {
                legacyHighest = std::atoi(legacyBefore.c_str() + legacyBefore.find(':', at) + 1);
            }

            ProfileManager first;
            first.Load();   // スロットはまだ無い → 前のファイル
            const bool fromLegacy = first.GetHighestClearedStage() == legacyHighest;
            first.RecordStageClear(legacyHighest + 1, 42.5f, 3, 2);   // クリアするとスロットへ書く
            const bool slotWritten = Engine::Core::SaveGame::Exists("Profile");

            ProfileManager second;
            second.Load();   // 今度はスロットから
            const StageRecord* record = second.GetRecord(legacyHighest + 1);
            const bool fromSlot = second.GetHighestClearedStage() == legacyHighest + 1 && record && record->Cleared
                                  && record->BestTime == 42.5f && record->OrbPickups == 3 && record->PedestalInserts == 2;
            const bool legacyUntouched = readAll(legacy) == legacyBefore;

            Engine::Core::SaveGame::SetDirectory({});
            fs::remove_all(root, ec);

            const bool ok = fromLegacy && slotWritten && fromSlot && legacyUntouched;
            Engine::Debug::LogOutput(ok ? Engine::Debug::LogLevel::Info : Engine::Debug::LogLevel::Error,
                "App", __FILE__, __LINE__,
                std::format("Profile save: first run reads the old file={} (highest {}), a clear writes the slot={}, "
                            "the next run reads the slot={}, the old file is untouched={}",
                            fromLegacy, legacyHighest, slotWritten, fromSlot, legacyUntouched));
        }
    }

} // namespace App
