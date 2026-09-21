module;

#include <algorithm>
#include <cmath>
#include <vector>

#include <DirectXMath.h>

export module App.Luminous.MenuCursor;

import Engine.Input;
import Engine.Math;
import App.Luminous.InputConfig;

// ============================================================================
// メニューカーソル
//
//   App.Luminous.Scenes から出したもの。中身は動かしていない。
//   **本編の持ち物 (LuminousPlayRuntime) から使いたい**ので、
//   シーンより下の層に置く必要があった (そうしないと import が輪になる)。
// ============================================================================

export namespace App::Luminous {

    // 元は App.Luminous.Scenes の中にあり、そちらの using をそのまま使っていた
    using Engine::Math::Vector2;

    // ========================================================================
    // メニューカーソル
    // ------------------------------------------------------------------------
    // マウス (ホバー + クリック) と、キーボード W/S/A/D・十字キー・左スティック
    // (カーソル移動) + 決定ボタンの両方でボタンを選べるようにする。
    // ========================================================================
    struct LuminousMenuCursor {
        // マウスのホバーで選択を奪うのは、メニューを開いてから一定時間後に
        // 実際にマウスを動かした (またはクリックした) ときだけ。
        // シーン遷移やカーソルロック解除の直後に、たまたまカーソルの下にあった
        // ボタンが勝手に選択されるのを防ぐ。
        static constexpr float MOUSE_ARM_DELAY = 0.30f;
        static constexpr float MOUSE_ARM_DISTANCE = 8.0f;

        struct Rect { float X = -1e5f, Y = -1e5f, W = 0.0f, H = 0.0f; };

        int Selection = 0;
        int Count = 0;
        bool Confirm = false;
        bool MouseArmed = false;
        float OpenTime = 0.0f;
        float MouseTravel = 0.0f;
        LuminousNavDir PendingDir = LuminousNavDir::None;
        Vector2 RawMouse = Vector2(0.0f, 0.0f);
        int PrevSelection = 0;
        std::vector<Rect> Rects;

        // メニューを開き直したとき (画面切り替え・ポーズ開始など) に呼ぶ
        void Reset(int selection = 0) {
            Selection = selection;
            MouseArmed = false;
            OpenTime = 0.0f;
            MouseTravel = 0.0f;
        }

        // 1 フレームに 1 回、そのフレームに並ぶボタン数を渡して呼ぶ
        void BeginFrame(float dt, int count, const Vector2& mouse) {
            Count = count;
            RawMouse = mouse;
            PrevSelection = Selection;
            Rects.assign(static_cast<size_t>((std::max)(count, 0)), Rect{});
            OpenTime += dt;

            if (LuminousInputDevice::IsGamepad()) {
                MouseArmed = false;
                MouseTravel = 0.0f;
            } else if (OpenTime > MOUSE_ARM_DELAY) {
                const Vector2 d = Input::GetMouseDelta();
                MouseTravel += std::abs(d.x) + std::abs(d.y);
                if (MouseTravel > MOUSE_ARM_DISTANCE || Input::GetKeyDown(KeyCode::MOUSE_LEFT)) {
                    MouseArmed = true;
                }
            }

            PendingDir = LuminousMenuNav::Poll(dt);
            if (PendingDir != LuminousNavDir::None) {
                // キー/パッドで動かしたら、マウスを動かし直すまでホバーで上書きしない
                MouseArmed = false;
                MouseTravel = 0.0f;
            }

            Selection = (count > 0) ? std::clamp(Selection, 0, count - 1) : 0;
            Confirm = LuminousInput::ConfirmPressed();
        }

        // ボタン判定に使うマウス座標 (ホバーが無効な間は画面外)
        Vector2 Mouse() const {
            return MouseArmed ? RawMouse : Vector2(-100000.0f, -100000.0f);
        }

        // ボタンの矩形を登録する (描画より前に呼ぶ)。十字方向の移動先探しとホバー判定に使う
        void Item(int index, float x, float y, float w, float h) {
            if (index < 0 || index >= static_cast<int>(Rects.size())) return;
            Rects[static_cast<size_t>(index)] = Rect{ x, y, w, h };
            if (MouseArmed && RawMouse.x >= x && RawMouse.x <= x + w && RawMouse.y >= y && RawMouse.y <= y + h) {
                Selection = index;
            }
        }

        // 登録した矩形の配置から、押した方向にある最も近いボタンへ移動する。
        // 上下左右に並んだメニューでも、見た目どおりの方向へ動く。
        // その方向に何もなければ反対側の端へ回り込む。
        // 戻り値: このフレームで選択が変わったか
        bool EndFrame() {
            if (PendingDir != LuminousNavDir::None && Count > 0 && Selection < static_cast<int>(Rects.size())) {
                const Rect& cur = Rects[static_cast<size_t>(Selection)];
                const float cx = cur.X + cur.W * 0.5f;
                const float cy = cur.Y + cur.H * 0.5f;
                float dx = 0.0f, dy = 0.0f;
                switch (PendingDir) {
                case LuminousNavDir::Up:    dy = -1.0f; break;
                case LuminousNavDir::Down:  dy = 1.0f; break;
                case LuminousNavDir::Left:  dx = -1.0f; break;
                case LuminousNavDir::Right: dx = 1.0f; break;
                default: break;
                }

                int best = -1;
                float bestScore = 1e30f;
                int wrap = -1;
                float wrapScore = 1e30f;
                for (int j = 0; j < static_cast<int>(Rects.size()); ++j) {
                    if (j == Selection) continue;
                    const Rect& r = Rects[static_cast<size_t>(j)];
                    if (r.W <= 0.0f) continue;
                    const float vx = r.X + r.W * 0.5f - cx;
                    const float vy = r.Y + r.H * 0.5f - cy;
                    const float along = vx * dx + vy * dy;
                    const float perp = std::abs(vx * dy - vy * dx);
                    if (along > 1.0f) {
                        const float score = along + perp * 2.5f;
                        if (score < bestScore) { bestScore = score; best = j; }
                    } else if (along < -1.0f) {
                        // 回り込み候補: 反対側で最も遠く、軸がそろっているもの
                        const float score = along + perp * 2.5f;
                        if (score < wrapScore) { wrapScore = score; wrap = j; }
                    }
                }
                if (best >= 0) Selection = best;
                else if (wrap >= 0) Selection = wrap;
            }
            return Selection != PrevSelection;
        }

        bool Is(int index) const { return Selection == index; }
        bool Activated(int index) const { return Confirm && Selection == index; }
    };

} // namespace App::Luminous
