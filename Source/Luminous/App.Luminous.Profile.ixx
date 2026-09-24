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
export module App.Luminous.Profile;

import Engine.Common.Config;   // Paths (プロジェクトの中で読み書きする)

export namespace App::Luminous {

    // ステージクリア実績
    struct StageRecord {
        bool Cleared = false;
        float BestTime = 99999.0f;
        int OrbPickups = 0;
        int PedestalInserts = 0;
    };

    // プレイヤー進行度・セーブデータ管理
    class ProfileManager {
    private:
        int highestClearedStage_ = 0;
        std::unordered_map<int, StageRecord> records_;
        std::string savePath_ = "Assets/Data/profile.json";
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

        // 進み具合を書くファイル (プロジェクトの中の絶対パス)。
        //   相対パスのまま開くと、エディタの Game ビューではエディタの置き場に書いていた
        [[nodiscard]] std::filesystem::path SaveFile() const {
            return Engine::Common::Paths::Resolve(std::filesystem::path(savePath_));
        }

        bool HasCustomStages() {
            return !GetCustomStages().empty();
        }

        void Load(const std::string& path = "Assets/Data/profile.json") {
            savePath_ = path;
            isLoaded_ = true;

            std::ifstream file(SaveFile());
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

        void Save(const std::string& path = "") {
            // プロジェクトの中へ書く (相対パスは Paths で直す。フォルダも作る)
            const std::filesystem::path target = path.empty() ? SaveFile()
                : Engine::Common::Paths::Resolve(std::filesystem::path(path));
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);

            std::ofstream file(target);
            if (!file.is_open()) return;

            file << "{\n";
            file << "  \"highest_cleared_stage\": " << highestClearedStage_ << ",\n";
            file << "  \"stage_1_cleared\": " << (records_[1].Cleared ? "true" : "false") << ",\n";
            file << "  \"stage_2_cleared\": " << (records_[2].Cleared ? "true" : "false") << "\n";
            file << "}\n";
        }
    };

} // namespace App::Luminous
