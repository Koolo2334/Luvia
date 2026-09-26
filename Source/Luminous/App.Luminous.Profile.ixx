module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <system_error>

#include <DirectXMath.h>
#include <RaDXReflect.h>   // セーブデータの形を宣言のすぐ下で登録する (22 の R-10-1)
export module App.Luminous.Profile;

import Engine.Common.Config;   // Paths (プロジェクトの中で読み書きする)
import Engine.Core.Reflection;
import Engine.Core.SaveGame;   // セーブデータ (22 の R-10-6)

namespace App::Luminous {

    // セーブデータの形 (エンジンの SaveGame が JSON にする。欄は名前で書くので、足しても古いものが読める)
    struct ProfileStageSave {
        int Stage = 0;
        bool Cleared = false;
        float BestTime = 99999.0f;
        int OrbPickups = 0;
        int PedestalInserts = 0;
    };
    RADX_STRUCT(ProfileStageSave, "Luvia.ProfileStage", RADX_FIELD(Stage), RADX_FIELD(Cleared), RADX_FIELD(BestTime),
                RADX_FIELD(OrbPickups), RADX_FIELD(PedestalInserts));

    struct ProfileSave {
        int HighestClearedStage = 0;
        std::vector<ProfileStageSave> Stages;
    };
    RADX_STRUCT(ProfileSave, "Luvia.Profile", RADX_FIELD(HighestClearedStage), RADX_FIELD(Stages));

    constexpr const char* kProfileSlot = "Profile";
    constexpr int kProfileVersion = 1;
    // 前の版が進み具合を書いていたファイル。まだセーブが無いときの「初めの進み具合」として読む (消さない)
    constexpr const char* kLegacyProfile = "Assets/Data/profile.json";

} // namespace App::Luminous

export namespace App::Luminous {

    // ステージクリア実績
    struct StageRecord {
        bool Cleared = false;
        float BestTime = 99999.0f;
        int OrbPickups = 0;
        int PedestalInserts = 0;
    };

    // プレイヤー進行度・セーブデータ管理。
    //   **エンジンのセーブデータ (SaveGame) のスロット "Profile" に書く** (22 の R-10-6)。
    //   前は Assets/Data/profile.json (アセット) へ 1 行ずつ手で書いていたので、書いている途中で落ちると壊れ、
    //   配布したゲームが書けない場所 (Program Files) に置かれると保存できなかった。
    //   まだセーブが無ければ、そのファイルを初めの進み具合として読む (見た目・進み方は前と同じ)
    class ProfileManager {
    private:
        int highestClearedStage_ = 0;
        std::unordered_map<int, StageRecord> records_;
        bool isLoaded_ = false;

    public:
        static ProfileManager& Get() {
            static ProfileManager instance;
            if (!instance.isLoaded_) {
                instance.Load();
            }
            return instance;
        }

        int GetHighestClearedStage() const {
            return highestClearedStage_;
        }

        // 次にプレイすべき未クリアステージ番号を取得。
        // totalStages は登録済み基本ステージ数 (可変)。全クリア時は最終ステージを返す。
        int GetNextUncompletedStageIndex(int totalStages = 2) const {
            if (totalStages < 1) totalStages = 1;
            const int next = highestClearedStage_ + 1;
            return (next > totalStages) ? totalStages : next;
        }

        // ステージが開放されているか (クリア済み + 未クリアの次のステージ)
        bool IsStageUnlocked(int stageIndex) const {
            if (stageIndex <= 1) return true;
            return stageIndex <= (highestClearedStage_ + 1);
        }

        // STAGE SELECT ボタンが表示可能か (1ステージ以上クリア または エディタ作成ステージあり)
        bool IsStageSelectAvailable() {
            if (highestClearedStage_ >= 1) return true;
            return HasCustomStages();
        }

        // ステージクリア実績を記録
        void RecordStageClear(int stageIndex, float clearTime, int orbPickups, int pedestalInserts) {
            auto& rec = records_[stageIndex];
            rec.Cleared = true;
            if (clearTime < rec.BestTime) rec.BestTime = clearTime;
            rec.OrbPickups = orbPickups;
            rec.PedestalInserts = pedestalInserts;

            if (stageIndex > highestClearedStage_) {
                highestClearedStage_ = stageIndex;
            }

            Save();
        }

        const StageRecord* GetRecord(int stageIndex) const {
            auto it = records_.find(stageIndex);
            if (it != records_.end()) return &it->second;
            return nullptr;
        }

        // エディタ作成ステージ一覧を検索 (名前と、プロジェクト基準のパス)。
        //   **プロジェクトの中を探す**。以前は今の作業フォルダから探していたので、エディタの Game ビュー
        //   (作業フォルダはエディタの置き場) では 1 つも見つからなかった。
        //   TestGame の中を探す道 (Luvia が TestGame にいた頃の名残) も外した
        std::vector<std::pair<std::string, std::string>> GetCustomStages() {
            namespace fs = std::filesystem;
            std::vector<std::pair<std::string, std::string>> stages;
            std::error_code ec;
            const fs::path dir = Engine::Common::Paths::Resolve(fs::path("Assets/Data/Stages"));
            for (fs::directory_iterator it(dir, ec), end; it != end && !ec; it.increment(ec)) {
                if (!it->is_regular_file(ec) || it->path().extension() != ".json") continue;
                const std::string fname = Engine::Common::Paths::ToUtf8(it->path().filename());
                stages.push_back({ fname, "Assets/Data/Stages/" + fname });
            }
            std::sort(stages.begin(), stages.end());
            return stages;
        }

        // 進み具合を書くファイル (作っている間は <プロジェクト>/Saved/SaveGames/Profile.save.json、
        //   配布したゲームでは %LOCALAPPDATA%/Luvia/SaveGames/Profile.save.json)
        [[nodiscard]] std::filesystem::path SaveFile() const {
            return Engine::Core::SaveGame::SlotFile(kProfileSlot);
        }

        bool HasCustomStages() {
            return !GetCustomStages().empty();
        }

        void Load() {
            isLoaded_ = true;
            highestClearedStage_ = 0;
            records_.clear();

            ProfileSave save;
            if (Engine::Core::SaveGame::Load(kProfileSlot, save).Ok()) {
                highestClearedStage_ = save.HighestClearedStage;
                for (const ProfileStageSave& stage : save.Stages) {
                    records_[stage.Stage] = StageRecord{ stage.Cleared, stage.BestTime, stage.OrbPickups, stage.PedestalInserts };
                }
                return;
            }
            // まだセーブが無い (か読めない): 前の版のファイルを初めの進み具合として読む
            LoadLegacy();
        }

    private:
        // 前の版の形 ("highest_cleared_stage" / "stage_1_cleared" / "stage_2_cleared" を 1 行ずつ)
        void LoadLegacy() {
            std::ifstream file(Engine::Common::Paths::Resolve(std::filesystem::path(kLegacyProfile)));
            if (!file.is_open()) return;

            std::string line;
            while (std::getline(file, line)) {
                if (line.find("\"highest_cleared_stage\"") != std::string::npos) {
                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        highestClearedStage_ = std::stoi(line.substr(colon + 1));
                    }
                } else if (line.find("\"stage_1_cleared\"") != std::string::npos) {
                    if (line.find("true") != std::string::npos) {
                        records_[1].Cleared = true;
                        if (highestClearedStage_ < 1) highestClearedStage_ = 1;
                    }
                } else if (line.find("\"stage_2_cleared\"") != std::string::npos) {
                    if (line.find("true") != std::string::npos) {
                        records_[2].Cleared = true;
                        if (highestClearedStage_ < 2) highestClearedStage_ = 2;
                    }
                }
            }
        }

    public:
        // セーブデータへ書く (途中で落ちても前のものは残る。エンジンの SaveGame)
        void Save() {
            ProfileSave save;
            save.HighestClearedStage = highestClearedStage_;
            for (const auto& [stage, record] : records_) {
                save.Stages.push_back(ProfileStageSave{ stage, record.Cleared, record.BestTime, record.OrbPickups, record.PedestalInserts });
            }
            std::sort(save.Stages.begin(), save.Stages.end(),
                      [](const ProfileStageSave& a, const ProfileStageSave& b) { return a.Stage < b.Stage; });
            Engine::Core::SaveGame::Save(kProfileSlot, save, kProfileVersion);
        }
    };

} // namespace App::Luminous
