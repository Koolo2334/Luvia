module;

#include <string>
#include <unordered_map>
#include <vector>

#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.Luminous.EditorRuntime;

import App.Luminous.EditorSystem;

// ============================================================================
// ステージエディタを動かしている間の持ち物 (LuminousEditorRuntime)
//
//   本編・タイトルと同じ考え方。置き場をシーンからレジストリへ移すと、
//   更新処理が `this` を捕まえずに済み、名前つきのシステムにできる。
//
//   作っているステージそのもの (editor_.Stage) もここに入るが、
//   **保存はしない**。ステージは Assets/Data/Stages/*.json として
//   別に書き出されるもので、そちらが正本
// ============================================================================

export namespace App::Luminous {

    struct LuminousEditorRuntime {
        EditorSystem Editor;

        // エディタ中はエンジン汎用の "Engine Control Panel" を隠す。退出時に元へ戻す
        bool SavedShowEnginePanel = true;

        entt::entity Camera = entt::null;
        std::unordered_map<uint64_t, entt::entity> ObjectEntities;
        std::unordered_map<uint64_t, entt::entity> LightEntities;
        std::unordered_map<uint64_t, entt::entity> FlameEntities;
        std::vector<entt::entity> GridEntities;

        // 置く前に見せる半透明の見本
        entt::entity PreviewGhost = entt::null;
        std::string CurrentGhostAsset = "";
        bool CurrentGhostConflict = false;

        // 0: Floors, 1: Walls, 2: Stairs, 3: Pedestals, 4: Specials, 5: Props
        int ActivePaletteCategory = 0;
        bool ShowSavePopup = false;
        bool ShowLoadPopup = false;
        char FilePathBuffer[256] = "Assets/Data/Stages/CustomDungeon.json";
    };

    // いま開いているステージエディタの持ち物。無ければ作って返す
    inline LuminousEditorRuntime& EditorRuntime(entt::registry& registry) {
        const auto view = registry.view<LuminousEditorRuntime>();
        if (!view.empty()) return registry.get<LuminousEditorRuntime>(view.front());
        return registry.emplace<LuminousEditorRuntime>(registry.create());
    }

} // namespace App::Luminous
