# フォーカス連動表示

Windows 版 OBS Studio 31 以降向けのプラグインです。登録したウィンドウキャプチャ、または「特定のウィンドウをキャプチャ」しているゲームキャプチャを、その対象ウィンドウが最前面でフォーカスされているときだけ表示します。フォーカスが外れると非表示になります。

## 使い方

1. OBS を再起動し、メニューの **ドック → フォーカス連動** を開く。
2. 一覧から、連動させたいソースにチェックを入れる。
3. キャプチャ対象のウィンドウを前面にするとソースが表示され、別のウィンドウに切り替えると非表示になる。

チェックを外すと、登録する前の表示状態に戻します。OBS を再起動したあとにチェックを外した場合は、登録前の状態が残っていないのでソースを表示します。

ゲームキャプチャの「全画面アプリケーションをキャプチャ」と「ホットキーで前面のウィンドウをキャプチャ」は、設定上の対象ウィンドウがないため登録できません。

登録中は、ソース一覧の目のアイコンもこのプラグインが切り替えます。OBS を管理者として起動していないと、管理者権限のウィンドウは最前面かどうか判定できず、その間は非表示になります。

登録したソースは、シーンコレクションごとに保存されます。同じソースが複数のシーンやグループにあっても、まとめて表示を切り替えます。

## 動作環境

- Windows 10 以降、64bit
- OBS Studio 31 以降

macOS と Linux では読み込まれても何もしません。

## ビルド

Visual Studio 2022 と CMake 3.28 以降が必要です。依存する OBS と Qt は、公式プラグインテンプレートの仕組みで取得します。

```bat
cmake --preset windows-x64
cmake --build --preset windows-x64 --config RelWithDebInfo
cmake --install build_x64 --prefix dist --config RelWithDebInfo
```

## 導入

`dist\focus-source` を、次の場所へコピーします。

```text
%APPDATA%\obs-studio\plugins\focus-source\
```

コピー後の配置は次のとおりです。

```text
%APPDATA%\obs-studio\plugins\focus-source\bin\64bit\focus-source.dll
%APPDATA%\obs-studio\plugins\focus-source\data\locale\
```

OBS を起動し直してから、ドック一覧に「フォーカス連動」が出ることを確認してください。
