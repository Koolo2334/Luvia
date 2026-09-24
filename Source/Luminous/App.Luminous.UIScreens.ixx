module;

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include <EngineDebug.h>
#include <DirectXMath.h>

export module App.Luminous.UIScreens;

import Engine.UI.Types;
import Engine.UI.Document;
import Engine.UI.Kinds;
import Engine.UI.System;
import Engine.UI.Binding;
import Engine.Graphics.UIRenderer;
import Engine.Common.Config;
import Engine.Debug.Log;
import App.Luminous.InputConfig;

// ============================================================================
// Luvia の画面を UI の仕組みで出す (21 の U8)
//
//   画面は .ui.json (Assets/UI/Screens/)。ここには、どの画面でも使う口を置く:
//     ・操作の案内 (ControlHint.ui.json を入れ子にした部品) へ、機器ごとのボタンの絵・大きさ・文言を入れる
//       (大きさと送りは、前の DrawControlHint と同じ式)
//     ・古い描き方と新しい UI の四角を比べる (--set luvia.uiCompare=true。21 の §14-1)
// ============================================================================

export namespace App::Luminous {

    namespace LuminousScreenAssets {
        inline constexpr const char* Fade = "Assets/UI/Screens/Fade.ui.json";
        inline constexpr const char* Hud = "Assets/UI/Screens/Hud.ui.json";
        inline constexpr const char* Pause = "Assets/UI/Screens/Pause.ui.json";
        inline constexpr const char* Clear = "Assets/UI/Screens/Clear.ui.json";
        inline constexpr const char* Title = "Assets/UI/Screens/Title.ui.json";
        inline constexpr const char* StageSelect = "Assets/UI/Screens/StageSelect.ui.json";
        inline constexpr const char* StageIntro = "Assets/UI/Screens/StageIntro.ui.json";
    }

    // 手前に出す順 (大きいほど手前)。暗転はいちばん手前
    namespace LuminousScreenOrder {
        inline constexpr int32_t Hud = 100;
        inline constexpr int32_t Menu = 200;
        inline constexpr int32_t Intro = 300;     // ステージ名はポーズ・クリアより手前 (前の描く順)
        inline constexpr int32_t Fade = 2000000;   // UI の仕組みが出す候補の窓・確認の窓より手前
    }

    // クリアの画面の数字 (Clear.ui.json の {StageName} {Minutes:00} {Seconds:00.00} など)。
    //   結んでいる間は動かさないこと (UIViewModel)。持ち主は unique_ptr で持つ
    struct LuminousClearModel : Engine::UI::UIViewModel {
        Engine::UI::UIProperty<std::string> StageName{ *this, "StageName" };
        Engine::UI::UIProperty<int> Minutes{ *this, "Minutes" };
        Engine::UI::UIProperty<float> Seconds{ *this, "Seconds" };
        Engine::UI::UIProperty<int> OrbPickups{ *this, "OrbPickups" };
        Engine::UI::UIProperty<int> PedestalInserts{ *this, "PedestalInserts" };
    };

    // 操作の案内 1 つ
    struct LuminousHint {
        LuminousButtonGlyph Glyph = LuminousButtonGlyph::Confirm;
        const char* Label = "";
    };

    struct LuminousUIScreens {
        // 操作の案内 1 つぶんの送り (前の DrawControlHint の戻り値と同じ式)
        [[nodiscard]] static float HintAdvance(LuminousButtonGlyph glyph, std::string_view label, float height) {
            const float textSize = height * 0.48f;
            return height * LuminousInputDevice::GlyphAspect(glyph) + 10.0f + static_cast<float>(label.size()) * textSize * 0.56f + 36.0f;
        }

        // 入れ子の操作の案内 (name はその入れ子の名前) に、今の機器のボタンの絵・大きさ・文言を入れる。
        //   文字の大きさ (高さ x 0.48) は .ui.json の上書きで入れておく
        static void ApplyHint(Engine::UI::UISystem& ui, Engine::UI::UIInstanceId id, std::string_view name,
                              const LuminousHint& hint, float height) {
            using namespace Engine::UI;
            const UIElement root = ui.Find(id, name);
            if (!root.IsValid()) return;
            ui.SetSize(root, { HintAdvance(hint.Glyph, hint.Label, height), height });
            const UIElement glyph = ui.FindChild(root, "Glyph");
            ui.SetImage(glyph, LuminousInputDevice::GlyphPath(hint.Glyph));
            ui.SetSize(glyph, { height * LuminousInputDevice::GlyphAspect(hint.Glyph), height });
            ui.SetText(ui.FindChild(root, "Label"), hint.Label);
        }
    };

    // ------------------------------------------------------------------------
    // 古い描き方と新しい UI の四角を比べる (21 の §14-1)。--set luvia.uiCompare=true のときだけ動く。
    //   このフレームの古い描き方 (draw) を横取りしておき、次に呼ばれたときに、新しい UI の今の命令
    //   (= そのフレームの UI の Update の結果) と比べる。結果が変わったときだけログへ出す。
    //   古い描き方はゲームの更新の中 (UI の Update より前) で、その時の選択を見て描くので、UI の Update で
    //   選択が変わったフレームだけは 1 フレーム違って見える。違いは 2 フレーム続いたときだけ WARN にし、
    //   1 フレームで戻ったものは INFO で「1 フレームだけ」と出す (見た目のちらつきなら、ここで分かる)
    // ------------------------------------------------------------------------
    class LuminousUICompare {
    public:
        [[nodiscard]] static bool Enabled() {
            static const bool enabled = Engine::Common::Settings::Get().GetBool("luvia.uiCompare", false);
            return enabled;
        }

        static void Check(const char* screen, Engine::UI::UISystem& ui, Engine::UI::UIInstanceId id,
                          const std::function<void()>& draw) {
            if (!Enabled()) return;
            Entry& entry = Entries()[screen];
            if (entry.HasPending) {
                std::string report;
                const bool same = Engine::Graphics::UIRenderer::CompareSprites(entry.Pending, ui.GetCommands(id), &report);
                if (same) {
                    if (entry.MismatchFrames == 1) {
                        ENGINE_LOG_INFO("LuviaUI", "UICompare {}: 1 frame only (selection changed in that frame's UI update): {}",
                                        screen, entry.FirstMismatch);
                    }
                    entry.MismatchFrames = 0;
                    if (report != entry.LastReport) {
                        entry.LastReport = report;
                        ENGINE_LOG_INFO("LuviaUI", "UICompare {}: {}", screen, report);
                    }
                } else {
                    if (++entry.MismatchFrames == 1) entry.FirstMismatch = report;
                    else if (report != entry.LastReport) {
                        entry.LastReport = report;
                        ENGINE_LOG_WARN("LuviaUI", "UICompare {}: {} ({} frames)", screen, report, entry.MismatchFrames);
                    }
                }
            }
            entry.Pending.clear();
            auto& renderer = Engine::Graphics::UIRenderer::Get();
            renderer.BeginCapture(&entry.Pending);
            draw();
            renderer.EndCapture();
            entry.HasPending = true;
        }

        // 画面を閉じた・切り替えた (次の比べは新しく始める)
        static void Reset(const char* screen) { Entries().erase(screen); }

    private:
        struct Entry {
            std::vector<Engine::Graphics::UISpriteInstance> Pending;
            bool HasPending = false;
            std::string LastReport;
            int MismatchFrames = 0;       // 続けて違ったフレームの数
            std::string FirstMismatch;    // 違い始めたフレームの結果
        };
        static std::unordered_map<std::string, Entry>& Entries() {
            static std::unordered_map<std::string, Entry> entries;
            return entries;
        }
    };

} // namespace App::Luminous
