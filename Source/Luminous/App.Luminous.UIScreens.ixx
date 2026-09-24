module;

#include <cstdint>
#include <string>
#include <string_view>

export module App.Luminous.UIScreens;

import Engine.UI.Types;
import Engine.UI.Document;
import Engine.UI.Kinds;
import Engine.UI.System;
import Engine.UI.Binding;
import App.Luminous.InputConfig;

// ============================================================================
// Luvia の画面を UI の仕組みで出す (21 の U8)
//
//   画面は .ui.json (Assets/UI/Screens/)。ここには、どの画面でも使う口を置く:
//     ・画面のパスと手前に出す順
//     ・操作の案内 (ControlHint.ui.json を入れ子にした部品) へ、機器ごとのボタンの絵・大きさ・文言を入れる
//       (大きさと送りは、前の DrawControlHint と同じ式)
//   移すときは、前の即時描画と四角を 1 つずつ比べて同じにした (21 の §17-8。比べる道具は U8 の後に消した。
//   エンジンの UIRenderer::BeginCapture / CompareSprites は残っている)
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
        inline constexpr const char* CustomStages = "Assets/UI/Screens/CustomStages.ui.json";
        inline constexpr const char* StageEditor = "Assets/UI/Screens/StageEditor.ui.json";
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
        // 操作の案内 1 つぶんの送り (前の DrawControlHint の戻り値と同じ式。ボタンの絵 + 間 10 + 文字 + 間 36)
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

} // namespace App::Luminous
