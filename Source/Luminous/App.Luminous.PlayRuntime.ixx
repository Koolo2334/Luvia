module;

#include <vector>

#include <entt/entt.hpp>
#include <DirectXMath.h>

export module App.Luminous.PlayRuntime;

import App.Luminous.Types;
import App.Luminous.StageData;
import App.Luminous.Optics;

// ============================================================================
// 本編を動かしている間の持ち物 (LuminousPlayRuntime)
//
//   ここに入れるのは「ステージから作り直せるもの」だけ。
//     ステージそのもの (どの床・壁・台座が、どこに在るか)
//     そこから組み立てた宝玉・遮蔽壁・当たり判定の格子・台座の焼き込み
//     経過時間と一時停止
//
//   **保存しない**。リフレクションに登録していないので .scene.json には出ない。
//   出す必要も無い。ステージ (PlacedObject) さえ在れば、同じものが組み上がる。
//
//   置き場を**シーンからレジストリへ移した**のが要点。
//   シーンの持ち物だった頃は、更新処理が `this` を捕まえたラムダ 1 つでしか
//   書けず、名前つきのシステムに分けられなかった。レジストリに在れば、
//   どのシステムからも registry だけで辿り着ける。
//
//   取り出すのは PlayRuntime(registry)。ぶら下げる先のエンティティは
//   シーンが作る (シーンが終わるときに一緒に片付くように)。
// ============================================================================

export namespace App::Luminous {

    struct LuminousPlayRuntime {
        LuminousStage Stage;

        // ステージから組み立てたもの
        std::vector<OrbRuntimeState> Orbs;
        std::vector<OcclusionWall> OcclusionWalls;
        SpatialGrid Grid;
        std::vector<PedestalColliderBake> BakedPedestals;

        // 遮蔽ジオメトリの世代番号。ステージを組み直すたびに進めて、
        // OrbOcclusionPass 側の深度アトラスキャッシュを確実に捨てさせる
        uint32_t GeometryRevision = 0;

        float GameTime = 0.0f;
        bool Paused = false;
    };

    // いま走っている本編の持ち物。無ければ作って返す
    inline LuminousPlayRuntime& PlayRuntime(entt::registry& registry) {
        const auto view = registry.view<LuminousPlayRuntime>();
        if (!view.empty()) return registry.get<LuminousPlayRuntime>(view.front());
        return registry.emplace<LuminousPlayRuntime>(registry.create());
    }

} // namespace App::Luminous
