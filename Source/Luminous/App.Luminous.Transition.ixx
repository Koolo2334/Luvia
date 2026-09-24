module;

#include <memory>
#include <string>
#include <algorithm>
#include <cmath>
#include <entt/entt.hpp>

#include <DirectXMath.h>
export module App.Luminous.Transition;

import Engine.Core.Scene;
import Engine.Core.SceneFile;   // DataScene (シーンのファイルへ切り替える)
import Engine.Core.SystemContext;
import Engine.Graphics.UIRenderer;
import Engine.Audio.Core;
import Engine.Graphics.MaterialManager;
import Engine.Graphics.Assets.Material;
import Engine.Graphics.Components.Material;
import Engine.Core.Services;
import Engine.UI.Types;
import Engine.UI.System;
import App.Luminous.UIScreens;

export namespace App::Luminous {

    // ========================================================================
    // シーンのファイル (計画 18)
    //
    //   遷移の行き先は**ファイルのパス**。コードのシーンを作って渡していた頃は、
    //   同じ場面がコードとデータの 2 か所にあり、片方を直すともう片方とずれた。
    //   「どのステージを遊ぶか」のような**その場で決まる値**は
    //   LuminousSession に置く (シーンをまたいで残る持ち物)
    // ========================================================================
    namespace LuminousScenes {
        inline constexpr const char* Title       = "Assets/Scenes/Luminous.Title.scene.json";
        inline constexpr const char* Play        = "Assets/Scenes/Luminous.Play.scene.json";
        inline constexpr const char* Editor      = "Assets/Scenes/Luminous.Editor.scene.json";
    }

    // ========================================================================
    // LuminousTransition
    // ------------------------------------------------------------------------
    // シーン切り替えの黒フェード。
    //
    //   FadeOut : 現在のシーンを暗転させる。暗転しきったところで実際の
    //             RequestSceneLoad を発行する。
    //   FadeIn  : 新しいシーンは暗転した状態から始まり、
    //             「シーン内の全マテリアルのコンパイルが完了する」まで
    //             暗転を保持してから明転する。
    //             (マテリアル PSO は非同期コンパイルのため、待たずに明転すると
    //              一部のメッシュが描画されないまま画面に出てしまう)
    //
    // シーン側は Update() で Tick / TakePendingScene を、
    // 描画の最後で Draw() を呼ぶだけでよい。
    // ========================================================================
    class LuminousTransition {
    public:
        static LuminousTransition& Get() {
            static LuminousTransition s;
            return s;
        }

        static constexpr float FADE_OUT_DURATION = 0.35f;
        static constexpr float FADE_IN_DURATION = 0.45f;
        // マテリアル待ちの上限。何らかの理由で Ready にならなくても画面が
        // 黒いままにならないよう保険をかける。
        static constexpr float ASSET_WAIT_TIMEOUT = 8.0f;

        enum class State { Idle, FadeOut, WaitAssets, FadeIn };

        // シーンのファイルへ切り替える (暗転してから実際に切り替わる)。
        //   **こちらを使う**。コードのシーンを渡す下の形は、いずれ消える
        void ChangeScene(std::string scenePath) {
            ChangeScene(std::make_shared<Engine::Core::DataScene>(std::move(scenePath)));
        }

        // 新しいシーンへの切り替えを予約する (暗転してから実際に切り替わる)
        void ChangeScene(std::shared_ptr<Engine::Core::IScene> next) {
            if (state_ == State::FadeOut || pending_ != nullptr) return; // 二重発行防止
            pending_ = std::move(next);
            state_ = State::FadeOut;
            timer_ = 0.0f;
        }

        // 新しいシーンの OnSetup から呼ぶ。暗転した状態で開始する。
        void BeginSceneEnter() {
            alpha_ = 1.0f;
            state_ = State::WaitAssets;
            timer_ = 0.0f;
            pending_ = nullptr;
        }

        bool IsBusy() const { return state_ != State::Idle; }
        bool IsCoveringScreen() const { return alpha_ >= 0.999f; }
        float GetAlpha() const { return alpha_; }

        // 暗転しきって切り替えるべきシーンがあれば取り出す (無ければ nullptr)
        std::shared_ptr<Engine::Core::IScene> TakePendingScene() {
            if (state_ != State::FadeOut || alpha_ < 0.999f || pending_ == nullptr) {
                return nullptr;
            }
            auto s = std::move(pending_);
            pending_ = nullptr;
            // 暗転しきった瞬間に、前のシーンで鳴り残っている効果音・3D 音源を止める
            Engine::Audio::AudioEngine::Get().StopAllSfx();
            return s;
        }

        void Tick(float dt, entt::registry* registry = nullptr) {
            switch (state_) {
            case State::Idle:
                alpha_ = 0.0f;
                break;

            case State::FadeOut:
                timer_ += dt;
                alpha_ = std::clamp(timer_ / FADE_OUT_DURATION, 0.0f, 1.0f);
                break;

            case State::WaitAssets:
                timer_ += dt;
                alpha_ = 1.0f;
                if (AreSceneAssetsReady(registry) || timer_ >= ASSET_WAIT_TIMEOUT) {
                    state_ = State::FadeIn;
                    timer_ = 0.0f;
                }
                break;

            case State::FadeIn:
                timer_ += dt;
                alpha_ = 1.0f - std::clamp(timer_ / FADE_IN_DURATION, 0.0f, 1.0f);
                if (alpha_ <= 0.0f) {
                    alpha_ = 0.0f;
                    state_ = State::Idle;
                }
                break;
            }

            // 音声も画面と同じだけフェードさせる (暗転中は無音、明転に合わせて戻る)
            Engine::Audio::AudioEngine::Get().SetSceneFade(1.0f - alpha_);
        }

        // 暗幕は画面全体の 1 枚の UI (Fade.ui.json) にして、いちばん手前に置く (21 の U8)。
        //   UI の仕組みの命令はシーンの即時描画より後に送られるので、即時描画のままだと UI の下に潜る。
        //   シーンをまたいで開いたまま (UI の仕組みはシーンの外にある)
        void Draw(entt::registry& registry) {
            auto* ui = Engine::Core::FindService<Engine::UI::UISystem>(registry);
            if (!ui) return;
            if (!ui->IsOpen(fadeUi_)) fadeUi_ = ui->Open(LuminousScreenAssets::Fade, { .SortOrder = LuminousScreenOrder::Fade });
            const Engine::UI::UIElement root = ui->Find(fadeUi_, "Fade");
            const bool visible = alpha_ > 0.001f;
            ui->SetVisibility(root, visible ? Engine::UI::UIVisibility::Visible : Engine::UI::UIVisibility::Collapsed);
            if (visible) ui->SetColor(root, { 1.0f, 1.0f, 1.0f, alpha_ });
        }

        void Reset() {
            state_ = State::Idle;
            alpha_ = 0.0f;
            Engine::Audio::AudioEngine::Get().SetSceneFade(1.0f);
            timer_ = 0.0f;
            pending_ = nullptr;
        }

    private:
        // シーン内の全マテリアルが Ready (PSO コンパイル完了) になっているか。
        // MaterialAsset::State は非同期コンパイルスレッドから更新される。
        static bool AreSceneAssetsReady(entt::registry* registry) {
            if (registry == nullptr) return true;

            auto* matMgr = Engine::Core::FindService<Engine::Graphics::MaterialManager>(*registry);
            if (matMgr == nullptr) return true;

            auto view = registry->view<Engine::Graphics::MaterialComponent>();
            for (auto entity : view) {
                const auto& mc = view.get<Engine::Graphics::MaterialComponent>(entity);
                auto* inst = matMgr->GetInstance(mc.Handle);
                if (inst == nullptr) continue;
                auto* asset = matMgr->GetAsset(inst->AssetHandle);
                if (asset == nullptr) continue;
                const auto st = asset->State.load(std::memory_order_acquire);
                // Compiling は当然として、Uninitialized (ウォームアップ発行前) も未完了扱い。
                // Failed はいくら待っても変わらないので待たない。
                if (st == Engine::Graphics::AssetState::Compiling ||
                    st == Engine::Graphics::AssetState::Uninitialized) {
                    return false;
                }
            }
            return true;
        }

        State state_ = State::Idle;
        float alpha_ = 0.0f;
        float timer_ = 0.0f;
        std::shared_ptr<Engine::Core::IScene> pending_;
        Engine::UI::UIInstanceId fadeUi_;   // 暗幕の UI
    };

    // ========================================================================
    // StageIntroOverlay
    // ------------------------------------------------------------------------
    // ステージ開始時に画面中央へ出すオーバーレイ画像。
    // フェードイン -> 一定時間表示 -> フェードアウト。
    // ========================================================================
    class StageIntroOverlay {
    public:
        static constexpr float FADE_IN = 0.6f;
        static constexpr float HOLD = 1.8f;
        static constexpr float FADE_OUT = 0.8f;

        void Begin(const std::string& imagePath) {
            imagePath_ = imagePath;
            timer_ = 0.0f;
            active_ = !imagePath_.empty();
        }

        void Tick(float dt) {
            if (!active_) return;
            timer_ += dt;
            if (timer_ >= FADE_IN + HOLD + FADE_OUT) active_ = false;
        }

        bool IsActive() const { return active_; }

        // 出す絵 (ステージ名)
        const std::string& ImagePath() const { return imagePath_; }

        // 今の透明度 (0.002 以下なら描かない)。フェードイン → 保つ → フェードアウトの折れ線を、端が滑らかになるように均す
        float Alpha() const {
            if (!active_) return 0.0f;
            float a;
            if (timer_ < FADE_IN) {
                a = timer_ / FADE_IN;
            } else if (timer_ < FADE_IN + HOLD) {
                a = 1.0f;
            } else {
                a = 1.0f - (timer_ - FADE_IN - HOLD) / FADE_OUT;
            }
            a = std::clamp(a, 0.0f, 1.0f);
            // 端を滑らかに見せるためイーズ
            return a * a * (3.0f - 2.0f * a);
        }


    private:
        std::string imagePath_;
        float timer_ = 0.0f;
        bool active_ = false;
    };
}
