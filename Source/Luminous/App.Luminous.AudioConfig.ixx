module;

#include <string>

#include <DirectXMath.h>
export module App.Luminous.AudioConfig;

export namespace App::Luminous {

    // ========================================================================
    // LuminousAudioConfig
    // ------------------------------------------------------------------------
    // ゲーム内で使用される全サウンド（効果音・BGM）のファイルパス、音量、
    // 3D減衰距離などの定義。後からユーザーやサウンドデザイナーが任意の
    // WAVファイルへ容易に差し替えられるよう、詳細な説明とともに変数として外部公開。
    // ========================================================================
    struct AudioItemConfig {
        std::string FilePath;          // WAVファイルパス (Assets/Audio/...)
        float DefaultVolume = 1.0f;    // デフォルト音量 (0.0 ~ 1.0)
        bool Is3D = false;             // 3D空間定位を行うか (true: 3D, false: 2D)
        float MinDistance = 1.5f;      // 3D音響の最大音量維持半径 (メートル)
        float MaxDistance = 15.0f;     // 3D音響の可聴限界減衰半径 (メートル)
        bool IsLooping = false;        // ループ再生を行うか
        std::string Description;       // 使用箇所・役割の日本語説明
    };

    struct LuminousAudioConfig {
        // --------------------------------------------------------------------
        // BGM (背景音楽)
        // --------------------------------------------------------------------
        // BGM は「導入部 (Start) をフェードインで 1 回再生 → ループ部 (Loop) を継ぎ目なくループ」で流す。
        // 導入部とループ部は同じフォーマット (チャンネル数・サンプルレート・ビット深度) にすること。
        // 導入部のフェードイン時間 (秒)
        static inline float BGM_IntroFadeInSeconds = 2.0f;
        // 宝箱を開けた瞬間にステージ BGM をフェードアウトする時間 (秒)
        static inline float BGM_ChestOpenFadeOutSeconds = 0.6f;

        // タイトル画面 BGM: 導入部
        static inline AudioItemConfig BGM_Title_Start = {
            "Assets/Audio/BGM/BGM_Title_Start.wav",
            0.60f, false, 0.0f, 0.0f, false,
            "【BGM: タイトル画面 導入部】タイトル表示時にフェードインで 1 回だけ流れる。終わるとループ部へ続く。"
        };
        // タイトル画面 BGM: ループ部 (音量は導入部の値を使う)
        static inline AudioItemConfig BGM_Title_Loop = {
            "Assets/Audio/BGM/BGM_Title_Loop.wav",
            0.60f, false, 0.0f, 0.0f, true,
            "【BGM: タイトル画面 ループ部】導入部の後に継ぎ目なくループ再生される。"
        };

        // ステージ BGM: 導入部
        static inline AudioItemConfig BGM_Stage_Start = {
            "Assets/Audio/BGM/BGM_Stage_Start.wav",
            0.55f, false, 0.0f, 0.0f, false,
            "【BGM: ステージ 導入部】ステージ開始時にフェードインで 1 回だけ流れる。終わるとループ部へ続く。"
        };
        // ステージ BGM: ループ部 (音量は導入部の値を使う)
        static inline AudioItemConfig BGM_Stage_Loop = {
            "Assets/Audio/BGM/BGM_Stage_Loop.wav",
            0.55f, false, 0.0f, 0.0f, true,
            "【BGM: ステージ ループ部】導入部の後に継ぎ目なくループ再生される。"
        };

        // --------------------------------------------------------------------
        // プレイヤー移動効果音 (SE)
        // --------------------------------------------------------------------
        // 石畳歩行足音 (左足)
        static inline AudioItemConfig SE_Footstep_L = {
            "Assets/Audio/SE/footstep_stone1.wav",
            0.50f, false, 0.0f, 0.0f, false,
            "【SE: プレイヤー足音(左)】石畳の床を歩行する際のコツコツとした乾いた反響音。0.45秒周期で交互再生。"
        };

        // 石畳歩行足音 (右足)
        static inline AudioItemConfig SE_Footstep_R = {
            "Assets/Audio/SE/footstep_stone2.wav",
            0.50f, false, 0.0f, 0.0f, false,
            "【SE: プレイヤー足音(右)】石畳の床を歩行する際のコツコツとした乾いた反響音。音程微小変化によりリアリティを向上。"
        };

        // --------------------------------------------------------------------
        // 宝玉・ギミック効果音 (SE)
        // --------------------------------------------------------------------
        // 宝玉の手持ち取得SE
        static inline AudioItemConfig SE_Orb_Pickup = {
            "Assets/Audio/SE/orb_pickup.wav",
            0.80f, false, 0.0f, 0.0f, false,
            "【SE: 宝玉回収】台座や床から宝玉を手元に回収した瞬間の上昇クリスタルチャイム音。"
        };

        // 宝玉の台座ソケット装填SE
        static inline AudioItemConfig SE_Orb_Insert = {
            "Assets/Audio/SE/orb_insert.wav",
            0.85f, true, 1.5f, 12.0f, false,
            "【SE: 台座装填】放物線アニメーション終了時に台座ソケットに宝玉がピタリと嵌まる重厚な石材クリック＆共鳴音。"
        };

        // 反転境界（壁・床）通過SE
        static inline AudioItemConfig SE_Phase_Pass = {
            "Assets/Audio/SE/phase_pass.wav",
            0.70f, false, 0.0f, 0.0f, false,
            "【SE: 反転境界通過】球状にくり抜かれた反転壁や反転床の光子境界をすり抜ける瞬間の神秘的シマー音。"
        };

        // --------------------------------------------------------------------
        // ゴール・クリア効果音 (SE)
        // --------------------------------------------------------------------
        // 宝箱開口SE
        static inline AudioItemConfig SE_Chest_Open = {
            "Assets/Audio/SE/chest_open.wav",
            0.85f, true, 1.5f, 12.0f, false,
            "【SE: 宝箱開口】[E]キーでゴール宝箱の蓋が奥の壁へ跳ね上がる重厚な古代木材・蝶番のきしみ音。"
        };

        // 光粒子噴水バーストSE
        static inline AudioItemConfig SE_Chest_Burst = {
            "Assets/Audio/SE/chest_burst.wav",
            0.90f, true, 1.5f, 15.0f, false,
            "【SE: 光粒子噴水】宝箱開放直後に内部から300個の黄金フォトンが噴水のように噴出する華やかな破裂・煌めき音。"
        };

        // ステージクリア勝利ファンファーレ
        static inline AudioItemConfig Jingle_StageClear = {
            "Assets/Audio/SE/SE_Clear.wav",
            0.85f, false, 0.0f, 0.0f, false,
            "【Jingle: ステージクリア】STAGE CLEARリザルトモーダル表示と同時に再生される荘厳な勝利ジングル。"
        };

        // --------------------------------------------------------------------
        // UI操作音 (SE)
        // --------------------------------------------------------------------
        // ボタンホバー音
        static inline AudioItemConfig SE_UI_Hover = {
            "Assets/Audio/SE/ui_hover.wav",
            0.40f, false, 0.0f, 0.0f, false,
            "【SE: UIホバー】マウスカーソルがメニューボタンに乗った際のソフトな高周波チックスティック音。"
        };

        // ボタン決定・クリック音
        static inline AudioItemConfig SE_UI_Click = {
            "Assets/Audio/SE/ui_click.wav",
            0.75f, false, 0.0f, 0.0f, false,
            "【SE: UI決定】メニューボタンやカードをクリックした際の明瞭な古代石材・クリスタル決定音。"
        };

        // メニュー戻る・キャンセル音
        static inline AudioItemConfig SE_UI_Cancel = {
            "Assets/Audio/SE/ui_cancel.wav",
            0.60f, false, 0.0f, 0.0f, false,
            "【SE: UI戻る】BACKボタン押下時やポーズ解除時のソフトな下降スイープ音。"
        };
    };

} // namespace App::Luminous
