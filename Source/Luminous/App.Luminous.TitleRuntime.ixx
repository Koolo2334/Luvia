module;

#include <vector>

#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.Luminous.TitleRuntime;

import App.Luminous.MenuCursor;
import Engine.Core.Components.Lifecycle;

// ============================================================================
// タイトル画面を動かしている間の持ち物 (LuminousTitleRuntime)
//
//   本編 (LuminousPlayRuntime) と同じ考え方。
//   置き場を**シーンからレジストリへ移す**と、更新処理が `this` を
//   捕まえなくて済み、名前つきのシステムとして登録できる。
//
//   中身はどれも画面を回すためだけのもの (どの向きを向いているか、
//   どのボタンを選んでいるか、背景の炎がどこにあるか)。
//   ステージのような**残すべきデータは無い**ので、保存しない。
// ============================================================================

export namespace App::Luminous {

    // どの画面を出しているか。
    //   クラスの中に置くと、持ち物の置き場から見えない (import が輪になる)
    enum class LuminousTitleView {
        MainMenu,
        StageSelect,
        CustomStageList
    };

    struct LuminousTitleRuntime {
        LuminousTitleView View = LuminousTitleView::MainMenu;
        int Selection = 0;
        float Timer = 0.0f;
        float CameraYaw = 0.0f;

        entt::entity Camera = entt::null;
        entt::entity OrbLight = entt::null;
        entt::entity OrbMesh = entt::null;
        std::vector<entt::entity> RoomEntities;

        // 背景の松明の炎
        struct TitleFlameParticle {
            entt::entity Entity = entt::null;
            DirectX::XMFLOAT3 Origin = { 0, 0, 0 };
            float LocalY = 0.0f;
            float Phase = 0.0f;
            float Speed = 0.0f;
            float Radius = 0.0f;
            float BaseScale = 0.082f;
            DirectX::XMFLOAT3 CurrentPosition = { 0, 0, 0 };
            float CurrentScale = 0.0f;
        };
        std::vector<TitleFlameParticle> FlameParticles;

        int LastHoveredBtn = -1;
        bool WasMouseDown = false;
        LuminousMenuCursor Cursor;
        bool BgmStarted = false;
    };

    // いま出ているタイトル画面の持ち物。無ければ作って返す
    inline LuminousTitleRuntime& TitleRuntime(entt::registry& registry) {
        const auto view = registry.view<LuminousTitleRuntime>();
        if (!view.empty()) return registry.get<LuminousTitleRuntime>(view.front());
        const entt::entity entity = registry.create();
        // **シーンが終われば一緒に片付く**。付けないと、データだけのシーンから
        // 起こしたときに前のシーンの持ち物が居残る
        registry.emplace<Engine::Core::SceneScopeTag>(entity);
        // 走っている間だけの持ち物。.scene.json には書き出さない
        registry.emplace<Engine::Core::GeneratedTag>(entity);
        return registry.emplace<LuminousTitleRuntime>(entity);
    }

} // namespace App::Luminous
