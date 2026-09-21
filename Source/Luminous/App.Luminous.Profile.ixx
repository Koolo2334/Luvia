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

#include <DirectXMath.h>
export module App.Luminous.Profile;

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

        // エディタ作成ステージ一覧を検索
        std::vector<std::pair<std::string, std::string>> GetCustomStages() {
            std::vector<std::pair<std::string, std::string>> stages;

            const char* searchDirs[] = {
                "Assets/Data/Stages/*.json",
                "../TestGame/Assets/Data/Stages/*.json"
            };

            for (const char* pattern : searchDirs) {
                WIN32_FIND_DATAA findData;
                HANDLE hFind = FindFirstFileA(pattern, &findData);
                if (hFind != INVALID_HANDLE_VALUE) {
                    do {
                        if (!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
                            std::string fname = findData.cFileName;
                            std::string fullPath = "Assets/Data/Stages/" + fname;
                            // 重複除外
                            bool exists = false;
                            for (const auto& s : stages) {
                                if (s.first == fname) { exists = true; break; }
                            }
                            if (!exists) {
                                stages.push_back({ fname, fullPath });
                            }
                        }
                    } while (FindNextFileA(hFind, &findData));
                    FindClose(hFind);
                }
                if (!stages.empty()) break;
            }

            return stages;
        }

        bool HasCustomStages() {
            return !GetCustomStages().empty();
        }

        void Load(const std::string& path = "Assets/Data/profile.json") {
            savePath_ = path;
            isLoaded_ = true;

            std::ifstream file(savePath_);
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
            std::string target = path.empty() ? savePath_ : path;
            // フォルダ作成
            CreateDirectoryA("Assets", nullptr);
            CreateDirectoryA("Assets/Data", nullptr);

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
