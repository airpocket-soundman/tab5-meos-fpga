# Tab5 MeOS FPGA

[English](README.md) | 日本語

FPGAによる描画支援を組み込んだ、M5Stack Tab5向けMeOSのGUI環境を研究するフォークです。MeOSが入力・ウィンドウ・アプリの状態を管理し、Tang Primer 20Kと128MB DDR3で画像資源の保持と一部の描画処理を担う構成を目指します。

**[開発目的・実装状況・ロードマップ — 日本語 / English](https://airpocket-soundman.github.io/tab5-meos-fpga/ja/)**

## 現在の状態

基準版とサイズ最適化版のビルド・書き込み、Tab5の起動とシリアルシェルを確認済みです。microSDとUSBメモリのFAT読み書き・再起動後保持を確認済みです。次はUSBハブ対応です。[USB実装記録](docs/USB_IMPLEMENTATION.md)を参照してください。FPGA通信、GUI拡張、周辺機能全体の回帰は未検証です。

## 実装予定

- FPGA通信、DDR3バッファ、矩形塗りつぶし・画像コピー、GUI統合。
- ローカル／SSH端末の共通化、ANSI色、スクロールバック、互換性確認。
- USBマウス、ST7123を含むタッチ統合、実現性を調査した上でのBLEキーボード・マウス。
- Tab5のmicroSD、USBメモリ、FAT対応。exFATは条件を評価。
- サーバー側FUSEヘルパーを使う、SSH接続先LinuxからのTab5ストレージマウント。ファイル転送とリモート画像表示。
- MicroPythonのREPL・スクリプト・VFS連携。共通のピン管理によるMicroPythonとネイティブシェルからのGPIO操作。
- CPythonとPyxel本体の移植を検討し、既存ゲームをソース・素材の事前変換なしで実行。互換性とメモリを検証後、FPGAによる描画高速化を追加。
- 小規模TUIアプリの移植。nano／Vimは依存関係を調査。Linux実行ファイルの直接実行には非対応。

## 開発資料

[詳細な開発記録](docs/DEVELOPMENT_GOALS.md)、[公開ロードマップ](https://airpocket-soundman.github.io/tab5-meos-fpga/ja/)、[保存した上流README（英語）](docs/UPSTREAM_README.md)を参照してください。

上流のTab5ビルド入口はESP-IDF v5.4.4環境での `sh tools/build.sh tab5 build` です。本フォークでは未再現です。書き込み前には実機状態と復元手順の確認が必要です。

## 関連プロジェクト

- [上流MeOS](https://github.com/shi3z/MeOS)
- [Linux版FPGA研究](https://github.com/airpocket-soundman/tab5-linux-fpga)
- [FPGA GPU](https://github.com/airpocket-soundman/tab5-fpga-gpu)
- [Tab5／Tang Primer 20Kキャリア](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier)
- [Tab5 thin client](https://github.com/airpocket-soundman/Tab5_thinclient)

## 出典・ライセンス

[shi3z/MeOS](https://github.com/shi3z/MeOS)の `a6a9f234e509737f9fe84fe13468a8ec78b60fc9` から開始しました。上流の履歴・著作権表示・第三者ライセンスを維持します。上流の再配布条件は要確認で、このフォークから上流コード全体へ新しいライセンスは付与しません。

## 公開サイト

静的な日英サイトは `docs/site/` にあります。計画更新後は `python tools/build_pages.py` で生成します。日本語本文は `docs/DEVELOPMENT_GOALS.md` を使用し、英語の対応本文は生成スクリプト内で更新します。`main`で公開元が変更されるとGitHub Actionsが生成・公開します。各ページ上部の言語リンクで切り替えられ、JavaScriptは不要です。
