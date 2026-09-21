// ゲームの DLL が外に見せる、たった 2 つの入口 (S-4)。
//
//   exe はゲームの型を 1 つも知らない。この 2 つを名前で引いて呼ぶだけ。
//   そのため「別のゲームを動かす」ことが、DLL の差し替えだけでできる。
//
//   C の呼び出し規約 (extern "C") にしているのは、名前の装飾を避けて
//   GetProcAddress で引けるようにするため。
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <DirectXMath.h>

import Engine.Core.GameModule;
import App.GameModule;
import App.GameTests;

extern "C" __declspec(dllexport)
void RegisterGameModule(Engine::Core::GameModuleContext& ctx) {
    App::RegisterGameModule(ctx);
}

extern "C" __declspec(dllexport)
void RunGameSelfTests() {
    App::RunGameSelfTests();
}
