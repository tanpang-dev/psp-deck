# PSP Deck

PSP を USB で Mac につなぎ、PSP の画面とボタンで Mac を操作する Stream Deck 風のコントローラーです。

- ボタンにショートカット・アプリ起動・シェルコマンドなどを割り当てられる（Mac 側の設定画面で選ぶ）
- L / R で前面アプリのタブを前後に切り替え、画面上部に今のタブ名を表示
- 起動中のアプリをアイコン付きのカルーセルで表示し、十字キーで選ぶとそのアプリに切り替え
- アナログスティックでスクロール、マウスモードではカーソル操作
- PSP の内蔵マイクを Mac の入力デバイス（「"PSP" Type C」）として使える

## 必要なもの

- CFW を導入した PSP-3000
- Mac（Apple Silicon）
- USB ケーブル

## 使い方

1. PSP 側：`psp/` でビルドしてできた `dist/PSPDECK` フォルダを、メモリースティックの `/PSP/GAME/` にコピーし、ゲームの一覧から「PSP Deck」を起動します。
2. Mac 側：`mac/build-app.sh` でできた `PSPDeck.app` を起動し、アクセシビリティの許可を与えます。メニューバーのアイコンから設定を開けます。
3. マイクを使うときは、録音するアプリの入力に「"PSP" Type C」を選びます。

## ビルド

- PSP：[pspdev](https://github.com/pspdev/pspdev) を入れて、`psp/` で `make dist`
- Mac：Homebrew の `libusb` を入れて、`mac/` で `./build-app.sh`

設計と、開発中にわかった PSP の USB・音声入力の挙動は [docs/DESIGN.md](docs/DESIGN.md) にまとめています。

## ライセンス

MIT
