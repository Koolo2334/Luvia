module;

#include <string>
#include <cmath>
#include <cstdint>
#include <algorithm>

#include <DirectXMath.h>
export module App.Luminous.InputConfig;

import Engine.Input;
import Engine.Math;
import Engine.UI.EngineInput;

export namespace App::Luminous {

    using namespace Engine::Input;

    // ========================================================================
    // アクション ID
    // ------------------------------------------------------------------------
    // ゲームの入力はすべて InputSystem のアクション経由で読む。
    // GAMEPLAY: プレイ中の操作 / UI: メニュー操作
    // ========================================================================
    namespace LuminousActions {
        // GAMEPLAY
        inline constexpr uint64_t Move     = "Luminous.Move"_hash;      // Axis
        inline constexpr uint64_t Look     = "Luminous.Look"_hash;      // Axis (パッド右スティック。マウスは別途 MouseDelta)
        inline constexpr uint64_t Interact = "Luminous.Interact"_hash;  // Button
        inline constexpr uint64_t Pause    = "Luminous.Pause"_hash;     // Button

        // UI
        inline constexpr uint64_t NavUp    = "Luminous.NavUp"_hash;
        inline constexpr uint64_t NavDown  = "Luminous.NavDown"_hash;
        inline constexpr uint64_t NavLeft  = "Luminous.NavLeft"_hash;
        inline constexpr uint64_t NavRight = "Luminous.NavRight"_hash;
        inline constexpr uint64_t Confirm  = "Luminous.Confirm"_hash;
        inline constexpr uint64_t Cancel   = "Luminous.Cancel"_hash;
        inline constexpr uint64_t Start    = "Luminous.Start"_hash;
    }

    // ========================================================================
    // LuminousInput
    // ------------------------------------------------------------------------
    // 課題レギュレーション準拠の入力割り当て。
    //
    // 使用を許可されているのは
    //   キーボード: Q, W, E, A, S, D, U, I, O, J, K, L, Space
    //   マウス    : 3 ボタン + ホイール + x, y 座標
    // のみ。エディタ画面とデバッグツール (F12) はレギュレーションの対象外。
    //
    // 配置図の対応 (キーボード ⇔ コントローラーの物理位置):
    //   W / A / S / D = 十字キー   → 移動 / メニュー選択
    //   Space         = START      → ポーズ
    //   I (△) J (□) K (×) L (○)   → フェイスボタン
    //
    //   操作          キーボード/マウス     コントローラー
    //   ------------  --------------------  ------------------------------
    //   移動          W A S D               左スティック / 十字キー
    //   視点          マウス移動            右スティック
    //   調べる        E / L (○)             右ボタン (○ / Xbox:B / Switch:A)
    //   決定 (UI)     L (○) / 左クリック    右ボタン
    //   戻る (UI)     K (×)                 下ボタン (× / Xbox:A / Switch:B)
    //   ポーズ        Space (START)         START / OPTIONS / +
    // Q, U, I, O, J は未使用 (使用キーは最低限に留める)。
    // ========================================================================
    struct LuminousInput {
        using Key = KeyCode;
        using Pad = PadCode;
        using Stick = StickCode;

        // --- 移動 (十字キー相当) ---
        static constexpr Key MoveForward = Key::W;
        static constexpr Key MoveBack = Key::S;
        static constexpr Key MoveLeft = Key::A;
        static constexpr Key MoveRight = Key::D;

        // --- フェイスボタン ---
        static constexpr Key Confirm = Key::L;   // ○ : 決定 / 調べる
        static constexpr Key Cancel = Key::K;    // × : 戻る / キャンセル

        // 「調べる」だけはキーボードで押しやすい E も受け付ける。
        static constexpr Key InteractAlt = Key::E;

        // --- START ---
        static constexpr Key Pause = Key::SPACE;

        // --- マウス ---
        static constexpr Key UIClick = Key::MOUSE_LEFT;

        // --- ゲームパッド (物理位置で対応させる) ---
        static constexpr Pad PadConfirm = Pad::B;      // 右ボタン = ○
        static constexpr Pad PadCancel = Pad::A;       // 下ボタン = ×
        static constexpr Pad PadPause = Pad::START;
        static constexpr Stick PadMoveStick = Stick::LEFT;   // 移動
        static constexpr Stick PadLookStick = Stick::RIGHT;  // 視点

        static constexpr float PAD_STICK_DEADZONE = 0.20f;
        // 視点感度は実行時に調整できるよう g_LuminousConfig.PadLookSpeedX / PadLookSpeedY に置いている
        static constexpr float MENU_STICK_THRESHOLD = 0.55f;  // メニュー選択とみなすスティックの倒し量

        // --------------------------------------------------------------------
        // バインド登録 (EngineApp::Initialize の後に 1 回呼ぶ)
        // ここでは上の定数だけを使う。LuminousInputRegulation::Validate が
        // 定数を検査するので、範囲外のキーが紛れ込まない。
        // --------------------------------------------------------------------
        static void RegisterBindings() {
            InputManager::Init();

            // --- GAMEPLAY ---
            auto& game = InputManager::GetInputSystem(InputBindType::GAMEPLAY);
            game.CreateAxisAction(LuminousActions::Move);
            game.BindVectorKeys(LuminousActions::Move, MoveForward, MoveBack, MoveLeft, MoveRight);
            game.BindPadStick(LuminousActions::Move, PadMoveStick, PAD_STICK_DEADZONE);
            game.BindPadDirectional(LuminousActions::Move);

            game.CreateAxisAction(LuminousActions::Look);
            game.BindPadStick(LuminousActions::Look, PadLookStick, PAD_STICK_DEADZONE);

            game.CreateButtonAction(LuminousActions::Interact);
            game.BindKey(LuminousActions::Interact, Confirm);
            game.BindKey(LuminousActions::Interact, InteractAlt);
            game.BindPadButton(LuminousActions::Interact, PadConfirm);

            game.CreateButtonAction(LuminousActions::Pause);
            game.BindKey(LuminousActions::Pause, Pause);
            game.BindPadButton(LuminousActions::Pause, PadPause);

            // --- UI ---
            auto& ui = InputManager::GetInputSystem(InputBindType::UI);
            struct NavDef { uint64_t Action; Key K; Pad P; Engine::Math::Vector2 Dir; };
            const NavDef navs[] = {
                { LuminousActions::NavUp,    MoveForward, Pad::UP,    Engine::Math::Vector2(0.0f,  1.0f) },
                { LuminousActions::NavDown,  MoveBack,    Pad::DOWN,  Engine::Math::Vector2(0.0f, -1.0f) },
                { LuminousActions::NavLeft,  MoveLeft,    Pad::LEFT,  Engine::Math::Vector2(-1.0f, 0.0f) },
                { LuminousActions::NavRight, MoveRight,   Pad::RIGHT, Engine::Math::Vector2(1.0f,  0.0f) },
            };
            for (const auto& n : navs) {
                ui.CreateButtonAction(n.Action);
                ui.BindKey(n.Action, n.K);
                ui.BindPadButton(n.Action, n.P);
                ui.BindPadStickDirection(n.Action, PadMoveStick, n.Dir, MENU_STICK_THRESHOLD);
            }

            ui.CreateButtonAction(LuminousActions::Confirm);
            ui.BindKey(LuminousActions::Confirm, Confirm);
            ui.BindPadButton(LuminousActions::Confirm, PadConfirm);

            ui.CreateButtonAction(LuminousActions::Cancel);
            ui.BindKey(LuminousActions::Cancel, Cancel);
            ui.BindPadButton(LuminousActions::Cancel, PadCancel);

            ui.CreateButtonAction(LuminousActions::Start);
            ui.BindKey(LuminousActions::Start, Pause);
            ui.BindPadButton(LuminousActions::Start, PadPause);

            // UI の仕組み (21) も同じメニューのアクションで動かす (決まりで使えるキーだけ。割り当ての変更もそのまま効く)
            Engine::UI::SetUIInputActions({ LuminousActions::NavUp, LuminousActions::NavDown, LuminousActions::NavLeft,
                                            LuminousActions::NavRight, LuminousActions::Confirm, LuminousActions::Cancel });

            InputManager::ChangeBindType(InputBindType::GAMEPLAY);

            // アクションはハッシュで作っているので、保存ファイルを人が読める
            // 形にするために名前を控えておく (P2-16b)
            RegisterActionNames();

            // ここまでが既定値。プレイヤーが変更したぶんだけを読み込む
            Defaults().Capture();
            Engine::Input::LoadBindings(REBIND_FILE_PATH);
        }

        // ------------------------------------------------------------------
        // リバインド (P2-16b)
        // ------------------------------------------------------------------

        // 変更されたぶんの保存先。変更が無ければ作られない
        static constexpr const char* REBIND_FILE_PATH = "Assets/Data/input_bindings.json";

        // RegisterBindings を呼んだ直後の割り当て (初期設定に戻すときの元)
        static Engine::Input::BindingDefaults& Defaults() {
            static Engine::Input::BindingDefaults defaults;
            return defaults;
        }

        // 今の割り当てを保存する (既定値と同じものは書かない)
        static bool SaveRebinds() {
            return Engine::Input::SaveBindings(Defaults(), REBIND_FILE_PATH);
        }

        // 初期設定に戻して、保存ファイルを消す
        static void ResetRebinds() {
            Defaults().RestoreAll();
            SaveRebinds();
        }

    private:
        static void RegisterActionNames() {
            auto& game = InputManager::GetInputSystem(InputBindType::GAMEPLAY);
            game.SetActionName(LuminousActions::Move, "Luminous.Move");
            game.SetActionName(LuminousActions::Look, "Luminous.Look");
            game.SetActionName(LuminousActions::Interact, "Luminous.Interact");
            game.SetActionName(LuminousActions::Pause, "Luminous.Pause");

            auto& ui = InputManager::GetInputSystem(InputBindType::UI);
            ui.SetActionName(LuminousActions::NavUp, "Luminous.NavUp");
            ui.SetActionName(LuminousActions::NavDown, "Luminous.NavDown");
            ui.SetActionName(LuminousActions::NavLeft, "Luminous.NavLeft");
            ui.SetActionName(LuminousActions::NavRight, "Luminous.NavRight");
            ui.SetActionName(LuminousActions::Confirm, "Luminous.Confirm");
            ui.SetActionName(LuminousActions::Cancel, "Luminous.Cancel");
            ui.SetActionName(LuminousActions::Start, "Luminous.Start");
        }

    public:

        static const InputSystem& Game() { return InputManager::GetInputSystem(InputBindType::GAMEPLAY); }
        static const InputSystem& UI() { return InputManager::GetInputSystem(InputBindType::UI); }

        // --- GAMEPLAY ---
        static Engine::Math::Vector2 MoveAxis() { return Game().GetAxis(LuminousActions::Move); }
        static Engine::Math::Vector2 LookAxis() { return Game().GetAxis(LuminousActions::Look); }
        static bool InteractPressed() { return Game().GetButtonDown(LuminousActions::Interact); }
        static bool PausePressed() { return Game().GetButtonDown(LuminousActions::Pause); }

        // --- UI ---
        static bool ConfirmPressed() { return UI().GetButtonDown(LuminousActions::Confirm); }
        static bool CancelPressed() { return UI().GetButtonDown(LuminousActions::Cancel); }
        static bool StartPressed() { return UI().GetButtonDown(LuminousActions::Start); }
    };

    // ========================================================================
    // 直近に操作された入力デバイスの追跡と、操作説明のボタン画像
    // ------------------------------------------------------------------------
    // 画面下の操作説明 (宝玉を取る等) やポーズ画面の操作一覧に出すボタン画像を、
    // キーボードマウス / Xbox / PlayStation / Switch Pro / 汎用コントローラーで切り替える。
    // 画像は Assets/UI/Glyphs/<セット>/<ボタン>.png。
    // ========================================================================
    enum class LuminousGlyphSet { KeyboardMouse, Xbox, PlayStation, Nintendo, Generic };
    enum class LuminousButtonGlyph { Interact, Confirm, Cancel, Pause, Move, Look, Click };

    struct LuminousInputDevice {
        static inline LuminousGlyphSet Current = LuminousGlyphSet::KeyboardMouse;

        static bool IsGamepad() { return Current != LuminousGlyphSet::KeyboardMouse; }

        // 毎フレーム 1 回呼ぶ
        static void Update() {
            const LastUsedDevice last = Input::GetLastUsedDevice();
            // コントローラー操作中はマウスカーソルを隠す (マウスを動かすとキーボードマウスに戻り再表示)
            Input::SetCursorHidden(last.Kind == InputDeviceKind::Gamepad);
            if (last.Kind == InputDeviceKind::KeyboardMouse) {
                Current = LuminousGlyphSet::KeyboardMouse;
                return;
            }
            switch (last.Vendor) {
            case ControllerVendor::Xbox:             Current = LuminousGlyphSet::Xbox; break;
            case ControllerVendor::Sony_PlayStation: Current = LuminousGlyphSet::PlayStation; break;
            case ControllerVendor::Nintendo_Switch:  Current = LuminousGlyphSet::Nintendo; break;
            default:                                 Current = LuminousGlyphSet::Generic; break;
            }
        }

        static const char* SetFolder(LuminousGlyphSet set) {
            switch (set) {
            case LuminousGlyphSet::Xbox:        return "xbox";
            case LuminousGlyphSet::PlayStation: return "playstation";
            case LuminousGlyphSet::Nintendo:    return "nintendo";
            case LuminousGlyphSet::Generic:     return "generic";
            case LuminousGlyphSet::KeyboardMouse:
            default:                            return "kbm";
            }
        }

        static const char* SetLabel(LuminousGlyphSet set) {
            switch (set) {
            case LuminousGlyphSet::Xbox:        return "Xbox Controller";
            case LuminousGlyphSet::PlayStation: return "PlayStation Controller";
            case LuminousGlyphSet::Nintendo:    return "Nintendo Switch Pro Controller";
            case LuminousGlyphSet::Generic:     return "Generic Controller";
            case LuminousGlyphSet::KeyboardMouse:
            default:                            return "Keyboard & Mouse";
            }
        }

        static const char* GlyphName(LuminousButtonGlyph glyph) {
            switch (glyph) {
            case LuminousButtonGlyph::Interact: return "interact";
            case LuminousButtonGlyph::Confirm:  return "confirm";
            case LuminousButtonGlyph::Cancel:   return "cancel";
            case LuminousButtonGlyph::Pause:    return "pause";
            case LuminousButtonGlyph::Move:     return "move";
            case LuminousButtonGlyph::Look:     return "look";
            case LuminousButtonGlyph::Click:    return "click";
            }
            return "confirm";
        }

        // 現在のデバイスでのボタン画像パス
        static std::string GlyphPath(LuminousButtonGlyph glyph) {
            // コントローラーには "click" が無いので決定ボタンで代用する
            if (glyph == LuminousButtonGlyph::Click && IsGamepad()) glyph = LuminousButtonGlyph::Confirm;
            return std::string("Assets/UI/Glyphs/") + SetFolder(Current) + "/" + GlyphName(glyph) + ".png";
        }

        // 画像の横幅 / 高さ (横長の画像は 2.0)
        static float GlyphAspect(LuminousButtonGlyph glyph) {
            if (Current == LuminousGlyphSet::KeyboardMouse) {
                return (glyph == LuminousButtonGlyph::Pause || glyph == LuminousButtonGlyph::Move) ? 2.0f : 1.0f;
            }
            if (glyph == LuminousButtonGlyph::Pause &&
                (Current == LuminousGlyphSet::PlayStation || Current == LuminousGlyphSet::Generic)) {
                return 2.0f;
            }
            return 1.0f;
        }
    };

    // ========================================================================
    // レギュレーション適合チェック
    // ------------------------------------------------------------------------
    // 使用してよいキーは Q, W, E, A, S, D, U, I, O, J, K, L, Space と
    // マウス (3 ボタン + ホイール + 座標) のみ。
    // RegisterBindings はこのファイルの定数だけでバインドを組むため、
    // 定数を検査すればゲーム中の全割り当てを検査したことになる。
    // ========================================================================
    struct LuminousInputRegulation {
        using Key = KeyCode;

        static bool IsAllowed(Key k) {
            switch (k) {
            case Key::Q: case Key::W: case Key::E: case Key::A:
            case Key::S: case Key::D: case Key::U: case Key::I:
            case Key::O: case Key::J: case Key::K: case Key::L:
            case Key::SPACE:
            case Key::MOUSE_LEFT: case Key::MOUSE_RIGHT: case Key::MOUSE_MIDDLE:
                return true;
            default:
                return false;
            }
        }

        static bool Validate(std::string& outReport) {
            struct Binding { const char* Name; Key K; };
            const Binding bindings[] = {
                { "MoveForward", LuminousInput::MoveForward },
                { "MoveBack",    LuminousInput::MoveBack },
                { "MoveLeft",    LuminousInput::MoveLeft },
                { "MoveRight",   LuminousInput::MoveRight },
                { "Confirm",     LuminousInput::Confirm },
                { "InteractAlt", LuminousInput::InteractAlt },
                { "Cancel",      LuminousInput::Cancel },
                { "Pause",       LuminousInput::Pause },
                { "UIClick",     LuminousInput::UIClick },
            };

            outReport.clear();
            bool ok = true;
            for (const auto& b : bindings) {
                if (!IsAllowed(b.K)) {
                    ok = false;
                    outReport += std::string(b.Name) + "=<not allowed> ";
                }
            }

            // 配置図どおりの割り当てになっているか
            if (LuminousInput::MoveForward != Key::W ||
                LuminousInput::MoveBack != Key::S ||
                LuminousInput::MoveLeft != Key::A ||
                LuminousInput::MoveRight != Key::D) {
                ok = false;
                outReport += "move-keys-must-be-WASD ";
            }
            if (LuminousInput::Pause != Key::SPACE) {
                ok = false;
                outReport += "pause-must-be-START(Space) ";
            }
            if (LuminousInput::Confirm != Key::L) {   // (o)
                ok = false;
                outReport += "confirm-must-be-L ";
            }
            if (LuminousInput::Cancel != Key::K) {    // (x)
                ok = false;
                outReport += "cancel-must-be-K ";
            }
            if (LuminousInput::InteractAlt != Key::E) {
                ok = false;
                outReport += "interact-alt-must-be-E ";
            }
            // コントローラーはキーボードと同じ物理位置 (○ = 右, × = 下)
            if (LuminousInput::PadConfirm != PadCode::B || LuminousInput::PadCancel != PadCode::A) {
                ok = false;
                outReport += "pad-face-buttons-must-match-keyboard-layout ";
            }

            if (ok) outReport = "all bindings within Q,W,E,A,S,D,U,I,O,J,K,L,Space + mouse; pad mirrors the diagram";
            return ok;
        }
    };

    // ========================================================================
    // デバッグ用テレポートキー (F1〜F11) の有効フラグ。
    // レギュレーション対象外の入力なので、既定では無効。
    // 開発時のみコマンドライン引数でシーンを直接指定した場合に有効化する。
    // ========================================================================
    struct LuminousDebugKeys {
#if defined(SHIPPING)
        static constexpr bool Enabled = false;   // Shipping には作らない (20 の §3-2)
#else
        static inline bool Enabled = false;
#endif
    };

    // ========================================================================
    // F12 デバッグツール (ImGui) の有効フラグ。
    // Shipping には作らない (常に偽。ツールのコードは使われずに消える。20 の §3-2)
    // ========================================================================
    struct LuminousDebugTools {
#if defined(SHIPPING)
        static constexpr bool Enabled = false;
#else
        static inline bool Enabled = true;
#endif
        static constexpr KeyCode ToggleKey = KeyCode::F12;
    };
}
