# PSP Deck

PSP-3000 を USB で Mac につなぎ、PSP の画面とボタンで Mac を操作する Stream Deck 風コントローラー（開発中）。

- L/R でタブ切り替え、起動中アプリのカルーセル、Chrome のタブ一覧、ボタンへのショートカット割り当て
- PSP の内蔵マイクを USB Audio Class のマイクとして Mac に見せる

設計は [docs/DESIGN.md](docs/DESIGN.md)。現在はフェーズ 0（USB の検証）。

## 必要なもの

- PSP-3000（6.61 ARK 系 CFW）、USB ケーブル
- macOS（Apple Silicon）、Homebrew の `libusb` と `libmpc`、[pspdev](https://github.com/pspdev/pspdev)

## ライセンス

MIT。USB 転送の処理は [PSPLink usbhostfs](https://github.com/pspdev/psplinkusb)（BSD-3-Clause）を参考にしている。
