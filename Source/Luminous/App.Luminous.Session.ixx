module;

#include <string>

#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.Luminous.Session;

import Engine.Core.Components.Lifecycle;

// ============================================================================
// シーンをまたいで残る持ち物 (LuminousSession)
//
//   「どのステージを遊ぶか」のような、**シーンとシーンの間で受け渡す値**を置く。
//
//   なぜシーンの引数ではないのか:
//     市販エンジンでも、シーンに引数を渡すのは標準ではない。
//       Unity  SceneManager.LoadScene(name)     引数なし → 永続シングルトン
//       Godot  change_scene_to_file(path)       引数なし → Autoload
//       Unreal OpenLevel(name, Options)         URL はあるが実際は GameInstance
//     そして Luvia には決め手がある。**プレイヤーがカスタムステージを作る**ので、
//     ステージの数はビルド時に決まらない。シーンのファイルに焼き込むことが
//     原理的にできず、実行時に選ぶしかない。
//
//   置き場は GlobalScopeTag を付けたエンティティ。
//   シーン遷移で消えず、**.scene.json にも書き出されない**
//   (書き出しは SceneScopeTag が付いたものだけ)。つまりエンジンへの追加は不要。
// ============================================================================

export namespace App::Luminous {

    struct LuminousSession {
        // 遊ぶステージ (空ならステージ 1)
        std::string StagePath;
        // 基本ステージの登録番号 (1 始まり)。0 は実績を記録しない
        int StageNumber = 0;
        // 終わったらステージエディタへ戻る (エディタのテストプレイから来たとき)
        bool ReturnToEditor = false;
        // タイトルのシーンをステージ選択の画面から始める (本編から戻るとき)。
        //   以前はステージ選択のために別のシーン (中身はタイトルと同じ部屋) があった
        bool TitleStartsAtStageSelect = false;
    };

    // シーンをまたいで残る持ち物。無ければ作って返す
    inline LuminousSession& Session(entt::registry& registry) {
        const auto view = registry.view<LuminousSession>();
        if (!view.empty()) return registry.get<LuminousSession>(view.front());
        const entt::entity entity = registry.create();
        // **シーンが変わっても消さない**
        registry.emplace<Engine::Core::GlobalScopeTag>(entity);
        return registry.emplace<LuminousSession>(entity);
    }

} // namespace App::Luminous
