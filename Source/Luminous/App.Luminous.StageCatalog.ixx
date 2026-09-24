module;

#include <string>
#include <vector>
#include <filesystem>

#include <DirectXMath.h>
#include <EngineDebug.h>
export module App.Luminous.StageCatalog;

import App.Luminous.StageData;
import Engine.Common.Config;
import Engine.Debug.Log;

export namespace App::Luminous {

    // ========================================================================
    // LuminousStageCatalog
    // ------------------------------------------------------------------------
    // 「基本ステージ」(ゲームに同梱する公式ステージ) の登録表。
    //
    // エディタでユーザーが作成したステージは Assets/Data/Stages/ に保存されるが、
    // 基本ステージはそれとは別のディレクトリ Assets/Data/BaseStages/ に置き、
    // 下の BaseStages に可変長のリストとして登録する。
    //
    // 1 エントリにつき次の 3 つを指定する:
    //   - StageFilePath : ステージ JSON のパス
    //   - ButtonImage   : ステージセレクト画面に出すボタン画像
    //   - OverlayImage  : ステージ開始時に画面中央へ出すオーバーレイ画像
    //
    // エントリを増減させればステージセレクト画面のレイアウトは自動で追従する
    // (カードの枠は Assets/UI/Screens/StageSelect.ui.json の Card0〜Card11 の 12 枚。増やすなら枠を足す)。
    // ========================================================================
    struct StageCatalogEntry {
        std::string StageFilePath;   // 例: "Assets/Data/BaseStages/stage_01.json"
        std::string ButtonImage;     // 例: "Assets/UI/StageSelect/stage1_card.png"
        std::string OverlayImage;    // 例: "Assets/UI/Overlays/stage_01_title.png"
        std::string DisplayName;     // 画像を用意できない場合のフォールバック表示名
        std::string LockedImage;     // 未開放時に代わりに出す画像 (空なら共通のロック画像)
    };

    // 基本ステージが置かれるディレクトリ (ユーザー作成ステージとは分離)
    inline const std::string BASE_STAGE_DIR = "Assets/Data/BaseStages/";
    // エディタで作ったユーザーステージのディレクトリ (従来どおり)
    inline const std::string USER_STAGE_DIR = "Assets/Data/Stages/";
    // 確認用のステージ (多層・影の境目など) のディレクトリ。
    //   コードの中にしか無かったものを、データのファイルにして置く場所 (計画 18)
    inline const std::string TEST_STAGE_DIR = "Assets/Data/TestStages/";

    struct LuminousStageCatalog {
        // --------------------------------------------------------------------
        // 基本ステージの登録リスト。
        // ここに追記するだけでステージセレクトに並ぶ。順番がステージ番号になる。
        // --------------------------------------------------------------------
        static inline std::vector<StageCatalogEntry> BaseStages = {
            {
                "Assets/Data/BaseStages/stage_01.json",
                "Assets/UI/StageSelect/stage1_card.png",
                "Assets/UI/Overlays/stage_01_title.png",
                "STAGE 1 - INVERTED PATH",
                ""
            },
            {
                "Assets/Data/BaseStages/stage_02.json",
                "Assets/UI/StageSelect/stage2_card.png",
                "Assets/UI/Overlays/stage_02_title.png",
                "STAGE 2 - TWIN SEALS",
                ""
            },
            {
                "Assets/Data/BaseStages/stage_03.json",
                "Assets/UI/StageSelect/stage3_card.png",
                "Assets/UI/Overlays/stage_03_title.png",
                "STAGE 3 - LIGHT WELL",
                ""
            },
            {
                "Assets/Data/BaseStages/stage_04.json",
                "Assets/UI/StageSelect/stage4_card.png",
                "Assets/UI/Overlays/stage_04_title.png",
                "STAGE 4 - SANCTUM OF LIGHT",
                ""
            },
            {
                "Assets/Data/BaseStages/stage_05.json",
                "Assets/UI/StageSelect/stage5_card.png",
                "Assets/UI/Overlays/stage_05_title.png",
                "STAGE 5 - ECHO CLOISTER",
                ""
            },
            {
                "Assets/Data/BaseStages/stage_06.json",
                "Assets/UI/StageSelect/stage6_card.png",
                "Assets/UI/Overlays/stage_06_title.png",
                "STAGE 6 - HANGING GARDEN",
                ""
            },
        };

        static int Count() { return static_cast<int>(BaseStages.size()); }

        static const StageCatalogEntry* Get(int index) {
            if (index < 0 || index >= Count()) return nullptr;
            return &BaseStages[static_cast<size_t>(index)];
        }

        // 1 始まりのステージ番号でのアクセス (プロフィールの解放判定と揃える)
        static const StageCatalogEntry* GetByStageNumber(int stageNumber) {
            return Get(stageNumber - 1);
        }

        // --------------------------------------------------------------------
        // 基本ステージ JSON の実体化。
        //
        // 初回起動時などファイルが存在しない場合にかぎり、コード内の
        // ステージ生成関数から書き出して実体を作る。以後はファイルが正となるため、
        // エディタで開いて編集・保存すればそのまま基本ステージを差し替えられる。
        // --------------------------------------------------------------------
        static void EnsureBaseStageFiles() {
            namespace fs = std::filesystem;
            std::error_code ec;
            // **プロジェクトの中へ書く**。相対パスのまま書くと、動かした場所
            // (ビルドの道具は別のディレクトリで動く) に置いてしまう
            const fs::path root = Engine::Common::Paths::Project();
            fs::create_directories(root / BASE_STAGE_DIR, ec);

            for (int i = 0; i < Count(); ++i) {
                const auto& entry = BaseStages[static_cast<size_t>(i)];
                const std::string path = Engine::Common::Paths::ToUtf8(root / entry.StageFilePath);
                if (fs::exists(path, ec)) continue;

                LuminousStage stage = (i == 1)
                    ? LuminousStage::CreateMultiFloorSampleStage()
                    : LuminousStage::CreateSampleStage();
                if (!entry.DisplayName.empty()) stage.Name = entry.DisplayName;

                std::string err;
                stage.SaveToFile(path, err);
            }
        }

        // --------------------------------------------------------------------
        // 確認用のステージを、データのファイルにしておく (計画 18 の段 2)。
        //
        //   多層・影の境目・影の踏破は、これまで**コードの中にしか無かった**。
        //   そのため多層などの変種は、
        //   「作るときに引数でステージを渡すコードのシーン」でしか開けなかった。
        //
        //   ファイルにしておけば、本編のシーン (Luminous.Play) にステージのパスを
        //   渡すだけで開ける (起動オプション --play2 など。19 の AU-24 で変種ごとの
        //   殻はやめた)。基本ステージ (EnsureBaseStageFiles) と同じやり方。
        //
        //   **既にあるファイルは触らない**。人が手で直したものを壊さないため
        // --------------------------------------------------------------------
        static void EnsureTestStageFiles() {
            namespace fs = std::filesystem;
            std::error_code ec;
            const fs::path root = Engine::Common::Paths::Project();
            fs::create_directories(root / TEST_STAGE_DIR, ec);

            struct Entry {
                const char* File;
                const char* Name;
                LuminousStage (*Make)();
            };
            static const Entry kEntries[] = {
                { "sample.json",      "見本",       [] { return LuminousStage::CreateSampleStage(); } },
                { "multi_floor.json", "多層の見本", [] { return LuminousStage::CreateMultiFloorSampleStage(); } },
                { "shadow_edge.json", "影の境目",   [] { return LuminousStage::CreateShadowEdgeTestStage(); } },
                { "shadow_open.json", "影の踏破 (開けた場所)",
                  [] { return LuminousStage::CreateShadowTraversalTestStage(false); } },
                { "shadow.json",      "影の踏破",
                  [] { return LuminousStage::CreateShadowTraversalTestStage(true); } },
            };

            for (const Entry& entry : kEntries) {
                const std::string path =
                    Engine::Common::Paths::ToUtf8(root / TEST_STAGE_DIR / entry.File);
                if (fs::exists(path, ec)) continue;
                LuminousStage stage = entry.Make();
                stage.Name = entry.Name;
                std::string err;
                if (!stage.SaveToFile(path, err)) {
                    ENGINE_LOG_ERROR("Luminous", "確認用ステージを書けませんでした '{}': {}", path, err);
                }
            }
        }

        // 指定ステージを読み込む。ファイルが無い/壊れている場合は
        // コード内のステージ生成関数へフォールバックする。
        static LuminousStage Load(int index) {
            const StageCatalogEntry* entry = Get(index);
            if (entry != nullptr) {
                LuminousStage stage;
                std::string err;
                if (stage.LoadFromFile(Engine::Common::Paths::ResolveString(entry->StageFilePath), err)) {
                    return stage;
                }
            }
            return (index == 1) ? LuminousStage::CreateMultiFloorSampleStage()
                                : LuminousStage::CreateSampleStage();
        }
    };
}
