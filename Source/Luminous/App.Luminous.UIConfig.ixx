module;

#include <string>

#include <DirectXMath.h>
export module App.Luminous.UIConfig;

export namespace App::Luminous {

    // ========================================================================
    // LuminousUIConfig
    // ------------------------------------------------------------------------
    // ゲーム内で使用される全UI画像（タイトルロゴ、各種ボタン、ステージカード、
    // HUD、パネル、フォント）のファイルパスと推奨解像度の定義。
    // 後からユーザーやUIデザイナーが任意のPNG画像へ容易に差し替えられるよう、
    // 詳細な説明とともに変数として外部公開。
    //
    // 画像は AgentWorkspace/UI/generate_luvia_ui.py で「Luvia」ロゴの意匠
    // (淡い氷青の細線・リングと六角形のモチーフ・字間を広げた細字) に揃えて生成している。
    // ========================================================================
    struct UIImageConfig {
        std::string FilePath;          // PNG画像ファイルパス (Assets/UI/...)
        float DefaultWidth = 100.0f;   // 推奨表示幅 (ピクセル)
        float DefaultHeight = 100.0f;  // 推奨表示高さ (ピクセル)
        std::string Description;       // 使用箇所・役割の日本語説明
    };

    struct LuminousUIConfig {
        // --------------------------------------------------------------------
        // タイトル画面UI (Title Screen)
        // --------------------------------------------------------------------
        // タイトルロゴ画像 (元画像 1024x594)
        static inline UIImageConfig Image_Title_Logo = {
            "Assets/UI/Title/Luvia_logo.png",
            700.0f, 406.0f,
            "【タイトル画面】ゲームタイトルロゴ「Luvia」。画面左上に配置し、背景の宝玉 (画面右) と重ならないようにする。"
        };

        // 外周ダークビネット
        static inline UIImageConfig Image_Title_Vignette = {
            "Assets/UI/Common/vignette.png",
            1920.0f, 1080.0f,
            "【全画面共通】画面四隅を暗く引き締めるダークグラデーションフレーム。"
        };

        // 画面左側を沈めるグラデーション (ロゴとメニューの背景)
        static inline UIImageConfig Image_Title_Shade = {
            "Assets/UI/Title/title_shade.png",
            1920.0f, 1080.0f,
            "【タイトル画面】左側のロゴとメニューの下に敷く暗いグラデーション。宝玉は画面右側に映る。"
        };

        // GAME START ボタン (通常 / ホバー / 押下)
        static inline UIImageConfig Image_Btn_Start_Normal = {
            "Assets/UI/Buttons/btn_start_normal.png", 420.0f, 68.0f,
            "【タイトル画面】GAME STARTボタン通常時。未クリアの次ステージを直接開始。"
        };
        static inline UIImageConfig Image_Btn_Start_Hover = {
            "Assets/UI/Buttons/btn_start_hover.png", 420.0f, 68.0f,
            "【タイトル画面】GAME STARTボタン選択時 (マウスホバー / パッド・キーボードでのカーソル)。"
        };
        static inline UIImageConfig Image_Btn_Start_Pressed = {
            "Assets/UI/Buttons/btn_start_pressed.png", 420.0f, 68.0f,
            "【タイトル画面】GAME STARTボタンクリック押下時。"
        };

        // STAGE SELECT ボタン (通常 / ホバー / 押下)
        static inline UIImageConfig Image_Btn_StageSelect_Normal = {
            "Assets/UI/Buttons/btn_stage_select_normal.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE SELECTボタン通常時。1ステージ以上クリアまたは自作ステージ存在時のみ出現。"
        };
        static inline UIImageConfig Image_Btn_StageSelect_Hover = {
            "Assets/UI/Buttons/btn_stage_select_hover.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE SELECTボタン選択時。"
        };
        static inline UIImageConfig Image_Btn_StageSelect_Pressed = {
            "Assets/UI/Buttons/btn_stage_select_pressed.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE SELECTボタンクリック押下時。"
        };

        // STAGE EDITOR ボタン (通常 / ホバー / 押下)
        static inline UIImageConfig Image_Btn_Editor_Normal = {
            "Assets/UI/Buttons/btn_editor_normal.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE EDITORボタン通常時。ステージエディタへ遷移。"
        };
        static inline UIImageConfig Image_Btn_Editor_Hover = {
            "Assets/UI/Buttons/btn_editor_hover.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE EDITORボタン選択時。"
        };
        static inline UIImageConfig Image_Btn_Editor_Pressed = {
            "Assets/UI/Buttons/btn_editor_pressed.png", 420.0f, 68.0f,
            "【タイトル画面】STAGE EDITORボタンクリック押下時。"
        };

        // EXIT GAME ボタン (通常 / ホバー / 押下)
        static inline UIImageConfig Image_Btn_Exit_Normal = {
            "Assets/UI/Buttons/btn_exit_normal.png", 420.0f, 68.0f,
            "【タイトル画面】EXIT GAMEボタン通常時。ゲームを終了。"
        };
        static inline UIImageConfig Image_Btn_Exit_Hover = {
            "Assets/UI/Buttons/btn_exit_hover.png", 420.0f, 68.0f,
            "【タイトル画面】EXIT GAMEボタン選択時。"
        };
        static inline UIImageConfig Image_Btn_Exit_Pressed = {
            "Assets/UI/Buttons/btn_exit_pressed.png", 420.0f, 68.0f,
            "【タイトル画面】EXIT GAMEボタンクリック押下時。"
        };

        // --------------------------------------------------------------------
        // ステージ選択画面UI (Stage Select Screen)
        // --------------------------------------------------------------------
        // ステージ選択ヘッダーロゴ画像
        static inline UIImageConfig Image_StageSelect_Header = {
            "Assets/UI/StageSelect/stage_select_header.png", 640.0f, 104.0f,
            "【ステージ選択】画面上部「STAGE SELECT」ヘッダー。Luvia ロゴと同じ細線・六角形の意匠。"
        };
        // ステージ1カード
        static inline UIImageConfig Image_Card_Stage1 = {
            "Assets/UI/StageSelect/stage1_card.png", 400.0f, 260.0f,
            "【ステージ選択】Stage 1: Inverted Path サムネイルカード。"
        };
        // ステージ2カード
        static inline UIImageConfig Image_Card_Stage2 = {
            "Assets/UI/StageSelect/stage2_card.png", 400.0f, 260.0f,
            "【ステージ選択】Stage 2: Twin Seals サムネイルカード。(ステージ3以降のカードは LuminousStageCatalog::BaseStages を参照)"
        };
        // ロック中ステージカード
        static inline UIImageConfig Image_Card_Locked = {
            "Assets/UI/StageSelect/card_locked.png", 400.0f, 260.0f,
            "【ステージ選択】未開放ステージ用ロック表示カード。"
        };
        // カスタムステージ汎用カード
        static inline UIImageConfig Image_Card_Custom = {
            "Assets/UI/StageSelect/custom_card.png", 400.0f, 260.0f,
            "【ステージ選択】自作カスタムステージ用カードサムネイル。"
        };
        // エクストラ自作ステージ一覧ボタン
        static inline UIImageConfig Image_Btn_Extra_Normal = {
            "Assets/UI/Buttons/btn_extra_normal.png", 420.0f, 68.0f,
            "【ステージ選択】CUSTOM STAGES ボタン通常時。自作ステージ存在時のみ出現。"
        };
        static inline UIImageConfig Image_Btn_Extra_Hover = {
            "Assets/UI/Buttons/btn_extra_hover.png", 420.0f, 68.0f,
            "【ステージ選択】CUSTOM STAGES ボタン選択時。"
        };
        // 戻るボタン
        static inline UIImageConfig Image_Btn_Back_Normal = {
            "Assets/UI/Buttons/btn_back_normal.png", 420.0f, 68.0f,
            "【ステージ選択】BACK ボタン通常時。ひとつ前の画面へ戻る。"
        };
        static inline UIImageConfig Image_Btn_Back_Hover = {
            "Assets/UI/Buttons/btn_back_hover.png", 420.0f, 68.0f,
            "【ステージ選択】BACK ボタン選択時。"
        };
        // タイトルへ戻るボタン (ポーズ / リザルト)
        static inline UIImageConfig Image_Btn_Title_Normal = {
            "Assets/UI/Buttons/btn_title_normal.png", 420.0f, 68.0f,
            "【ポーズ / リザルト】TITLE MENU ボタン通常時。タイトル画面へ戻る。"
        };
        static inline UIImageConfig Image_Btn_Title_Hover = {
            "Assets/UI/Buttons/btn_title_hover.png", 420.0f, 68.0f,
            "【ポーズ / リザルト】TITLE MENU ボタン選択時。"
        };

        // --------------------------------------------------------------------
        // ゲーム本編HUD (In-Game HUD)
        // --------------------------------------------------------------------
        // 画面中央レティクル照準
        static inline UIImageConfig Image_HUD_Crosshair = {
            "Assets/UI/HUD/crosshair.png", 64.0f, 64.0f,
            "【ゲーム本編HUD】画面中央の細いリングと点の照準。"
        };
        // --------------------------------------------------------------------
        // インタラクト操作説明 (画面下)
        // ------------------------------------------------------------------
        // 枠 + ボタン画像 + 行動名 を組み合わせて表示する。
        // ボタン画像は操作中のデバイス (キーボードマウス / Xbox / PlayStation /
        // Switch Pro / 汎用) に応じて Assets/UI/Glyphs/<セット>/ から自動で選ばれる。
        // --------------------------------------------------------------------
        static inline UIImageConfig Image_HUD_PromptFrame = {
            "Assets/UI/HUD/prompt_frame.png", 560.0f, 76.0f,
            "【ゲーム本編HUD】操作説明の背景枠 (角丸の細線フレーム)。"
        };
        static inline UIImageConfig Image_HUD_Label_TakeOrb = {
            "Assets/UI/HUD/label_take_orb.png", 400.0f, 56.0f,
            "【ゲーム本編HUD】行動名「TAKE ORB」。宝玉が載った台座を見ているとき。"
        };
        static inline UIImageConfig Image_HUD_Label_PlaceOrb = {
            "Assets/UI/HUD/label_place_orb.png", 400.0f, 56.0f,
            "【ゲーム本編HUD】行動名「PLACE ORB」。宝玉を持って空の台座を見ているとき。"
        };
        static inline UIImageConfig Image_HUD_Label_OpenChest = {
            "Assets/UI/HUD/label_open_chest.png", 400.0f, 56.0f,
            "【ゲーム本編HUD】行動名「OPEN CHEST」。ゴールの宝箱を見ているとき。"
        };
        // 宝玉所持インジケーターアイコン
        static inline UIImageConfig Image_HUD_OrbIcon = {
            "Assets/UI/HUD/orb_icon.png", 64.0f, 64.0f,
            "【ゲーム本編HUD】宝玉所持中に画面右下に表示されるリングと六角形のアイコン (白。描画時に色を乗算)。"
        };
        // タイマー・ステータス枠フレーム
        static inline UIImageConfig Image_HUD_TimerFrame = {
            "Assets/UI/HUD/timer_frame.png", 280.0f, 64.0f,
            "【ゲーム本編HUD】ステータス表示枠。"
        };

        // --------------------------------------------------------------------
        // ポーズメニューUI (Pause Menu)
        // --------------------------------------------------------------------
        // ポーズメニュー背景パネル
        static inline UIImageConfig Image_Pause_Panel = {
            "Assets/UI/Pause/pause_panel.png", 540.0f, 620.0f,
            "【ポーズメニュー】START(Space)押下時に画面中央に表示される半透明パネル。"
        };
        // RESUME ボタン
        static inline UIImageConfig Image_Btn_Resume_Normal = {
            "Assets/UI/Buttons/btn_resume_normal.png", 420.0f, 68.0f,
            "【ポーズメニュー】RESUME ボタン通常時。ゲームへ復帰。"
        };
        static inline UIImageConfig Image_Btn_Resume_Hover = {
            "Assets/UI/Buttons/btn_resume_hover.png", 420.0f, 68.0f,
            "【ポーズメニュー】RESUME ボタン選択時。"
        };
        // RESTART ボタン
        static inline UIImageConfig Image_Btn_Restart_Normal = {
            "Assets/UI/Buttons/btn_restart_normal.png", 420.0f, 68.0f,
            "【ポーズメニュー】RESTART ボタン通常時。ステージを初期位置から再開。"
        };
        static inline UIImageConfig Image_Btn_Restart_Hover = {
            "Assets/UI/Buttons/btn_restart_hover.png", 420.0f, 68.0f,
            "【ポーズメニュー】RESTART ボタン選択時。"
        };

        // --------------------------------------------------------------------
        // ステージクリア画面UI (Stage Clear Screen)
        // --------------------------------------------------------------------
        // STAGE CLEAR バナー
        static inline UIImageConfig Image_Clear_Banner = {
            "Assets/UI/Clear/clear_banner.png", 800.0f, 160.0f,
            "【ステージクリア】宝箱開放時に画面上部に表示される「STAGE CLEAR」バナー。"
        };
        // 成績ボードパネル
        static inline UIImageConfig Image_Clear_StatsPanel = {
            "Assets/UI/Clear/stats_panel.png", 640.0f, 500.0f,
            "【ステージクリア】クリアタイム、宝玉所持回数、台座脱着回数を表示するパネル。"
        };
        // NEXT STAGE ボタン
        static inline UIImageConfig Image_Btn_Next_Normal = {
            "Assets/UI/Buttons/btn_next_normal.png", 420.0f, 68.0f,
            "【ステージクリア】NEXT STAGE ボタン通常時。次のステージへ進む。"
        };
        static inline UIImageConfig Image_Btn_Next_Hover = {
            "Assets/UI/Buttons/btn_next_hover.png", 420.0f, 68.0f,
            "【ステージクリア】NEXT STAGE ボタン選択時。"
        };
        // RETRY ボタン
        static inline UIImageConfig Image_Btn_Retry_Normal = {
            "Assets/UI/Buttons/btn_retry_normal.png", 420.0f, 68.0f,
            "【ステージクリア】RETRY ボタン通常時。"
        };
        static inline UIImageConfig Image_Btn_Retry_Hover = {
            "Assets/UI/Buttons/btn_retry_hover.png", 420.0f, 68.0f,
            "【ステージクリア】RETRY ボタン選択時。"
        };

        // --------------------------------------------------------------------
        // フォントアトラス (Font Atlas)
        // --------------------------------------------------------------------
        // ASCIIビットマップフォント
        static inline UIImageConfig Image_Font_Atlas = {
            "Assets/UI/Fonts/font_atlas.png", 512.0f, 256.0f,
            "【フォント】ASCII文字 32..127（英数記号）を16x8グリッドで収録したプロシージャルフォントアトラス。"
        };
    };

} // namespace App::Luminous
