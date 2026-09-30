# Luvia

光による視覚効果とゲームシステムを融合した、一人称視点のパズルゲーム。
宝玉の光が届く範囲では壁がすり抜けられるようになり、床は穴になる。宝玉は 1 つしか持てないので、どの台座に置くかを考えながらゴールを目指す。

自作のゲームエンジン [RaDX Engine](https://github.com/Koolo2334/RaDXEngine) で制作した。このリポジトリには Luvia のプロジェクトだけが入っている (エンジンは含まない)。

## 必要なもの

- Windows 10 / 11 (x64)、Direct3D 12 に対応した GPU
- RaDX Engine のエディタ (リリースの `RaDXEngine-0.3.0-Editor-SDK-win64.zip`、または RaDX Engine のリポジトリをビルドしたもの)
- Visual Studio 2022 (「C++ によるデスクトップ開発」、MSVC v143、Windows SDK)

## ビルドと実行

1. `RaDXEditor.exe` を起動し、プロジェクトの選択画面で `Luvia.rdxproj` を開く
2. エディタの [プロジェクト] → [Visual Studio プロジェクトファイルを生成] で `Luvia.sln` を作り、ビルドする
   (コマンドなら `RaDXEditor.exe --project=<このフォルダ> --generate-project` の後に `Luvia.sln` を Release・x64 でビルド)。
   ビルドの後段でアセットの変換とシェーダーの事前コンパイルが走る
3. エディタでプレイするか、`RaDXGame.exe --project=<このフォルダ>` でゲームだけを起動する

`Luvia.sln`・`Binaries/`・`Intermediate/`・`Saved/` は作り直せる生成物なので、リポジトリには入れていない。

## 操作方法

| 操作 | キーボード・マウス | ゲームパッド |
| :--- | :--- | :--- |
| 移動 | W / A / S / D | 左スティック・十字キー |
| 視点移動 | マウス | 右スティック |
| 調べる (宝玉を取る・置く、宝箱を開ける) | E / L キー | 右ボタン (Xbox: B) |
| 決定 | L キー・左クリック | 右ボタン (Xbox: B) |
| 戻る | K キー | 下ボタン (Xbox: A) |
| ポーズ | Space キー | START |

ステージエディタ: 1 / 2 キーで配置と選択の切り替え、Tab キーで真上と 3D の視点の切り替え、F5 キーでテストプレイ。

## フォルダの構成

| 場所 | 中身 |
| :--- | :--- |
| `Luvia.rdxproj` | プロジェクトの設定 (既定のシーン・描画パイプライン・後処理) |
| `Assets/` | シーン・ステージ (`Data/BaseStages/`)・モデル・音・UI と `.meta` |
| `Source/` | ゲームの C++ (`Luminous/` に本編・エディタ・光の判定など) |
| `Shaders/` | ゲームのシェーダー (反転壁・反転床・宝玉など) |
| `Tests/` | 自動テストの設定 |

## 素材

3D モデルは配布素材「Free Modular Low Poly Dungeon」を使用している。それ以外は作者の制作。
