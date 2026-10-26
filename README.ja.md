# Tab5 MeOS FPGA

[English](README.md) | 日本語

FPGAによる描画支援を組み込んだ、M5Stack Tab5向けMeOSのGUI環境を研究するフォークです。MeOSが入力・ウィンドウ・アプリの状態を管理し、Tang Primer 20Kと128MB DDR3で画像資源の保持と一部の描画処理を担う構成を目指します。

**[開発目的・実装状況・ロードマップ — 日本語 / English](https://airpocket-soundman.github.io/tab5-meos-fpga/ja/)**


`image FILE.bmp` / `image --info FILE.bmp` によるBMP表示・情報取得を追加しました。[対応範囲と検証](docs/IMAGE_VIEWER.md)。FPGA通信のコーデック・mock診断も追加済みですが、[物理通信は未実装](docs/FPGA_LINK.md)です。

## 現在の状態

基準版とサイズ最適化版のビルド・書き込み、Tab5の起動とシリアルシェルを確認済みです。microSDと、Full-Speedハブ経由のUSBメモリ2台（exFAT・FAT）で読み書き・再起動後保持を確認しました。ハブ配下キーボードのコマンド入力とUSBカードリーダーのマウント・一覧取得も確認。`lsusb`で接続とマウント先を表示できます。[接続診断](docs/USB_DIAGNOSTICS.md)、[USB実装記録](docs/USB_IMPLEMENTATION.md)を参照してください。入力中の長時間負荷、High-Speed異速度混在、FPGA通信、GUI拡張、周辺機能全体の回帰は未検証です。

USB Ethernet（RTL8153 CDC-ECM）のリンク・DHCP・ping・SSH通信と、SSHサーバー／クライアントの同時利用を実機確認しました。端末制御の拡張はホスト試験済み。タッチはST7123/ST7121入力を追加して再検証中です。USBストレージの読み書きとEthernet pingの併用試験ではパケット損失がなくなりました。C6は公式MCUアプリへ更新し、Wi-Fi初期化・検索・WPA2接続・DHCPを確認しました。Ethernetを外した状態でDNS・HTTP通信も実機確認済みです。[Ethernet](docs/USB_ETHERNET_IMPLEMENTATION.md)、[SSHサーバー](docs/SSH_SERVER_IMPLEMENTATION.md)、[端末機能](docs/TERMINAL_IMPLEMENTATION.md)の実装記録を参照してください。

ネイティブSLは実機シリアルで完走・ANSI画面復帰・Ctrl-C/Esc後の再実行を確認。目視画質・SSH描画は未評価です。Wi-Fi SFTPは認証済み試験クライアントでSD102400バイト往復・既存EXCL保護・RO書込み拒否を確認。実ヘルパー＋Windows OpenSSH 9でもSD20480バイト転送が成功。Linux実プロセス/FUSEとUSBのネットワーク転送は未確認です。[SL移植](user/sl/README.md)・[ストレージヘルパー](docs/STORAGE_HELPER.md)を参照。

## 実装予定

- FPGA通信、DDR3バッファ、矩形塗りつぶし・画像コピー、GUI統合。
- ローカル／SSH端末の共通化、VT100を基礎とするVT220の必要機能・xterm拡張、キー入力、スクロールバック。
- USB UVCカメラ対応。Tab5内蔵カメラの既存ドライバを実機検証し、不足を修正。
- USBマウス、ST7123を含むタッチ統合、実現性を調査した上でのBLEキーボード・マウス。
- Tab5のmicroSD、USBメモリ、FAT対応。exFATは条件を評価。
- SD／USBのSFTP公開とLinux持ち込み用ヘルパー。既存SSHFS／FUSEによるマウントを試し、利用できなければファイル転送へ切り替えます。[検証範囲と制限](docs/STORAGE_HELPER.md)を参照してください。
- MicroPython v1.26.1の初期ネイティブポートとREPL・スクリプト・VFS・machine.Pin／ADC／PWM／UART／SoftI2C／SoftSPIと単調時刻timeを実装。全体ビルドと実機REPL・Pin/ADC入力・GC負荷試験に成功。SDファイル/import・例外後回収・MemoryError回復・Ctrl-Cと次VM起動も確認。長時間動作と外部出力実測は未確認。[仕様と制約](docs/MICROPYTHON.md)を参照。
- CPythonとPyxel本体の移植を検討し、既存ゲームをソース・素材の事前変換なしで実行。互換性とメモリを検証後、FPGAによる描画高速化を追加。
- 小規模TUIアプリの移植。nano／Vimは依存関係を調査。Linux実行ファイルの直接実行には非対応。

同一SDK・ツールによる再ビルド比較、成果物ハッシュと実行時メモリの扱いは[資源使用量](docs/RESOURCE_USAGE.md)に記録し、[公開ページ](https://airpocket-soundman.github.io/tab5-meos-fpga/ja/#resources)へ表示しています。

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

- [FPGAキャリアの適合性・製作前要求](docs/FPGA_CARRIER_REQUIREMENTS.md)

- [キャリア第2レポートのレビューと2D・3D解析](docs/CARRIER_R2_REVIEW_2026-10-24.md)

- [キャリア第3レポートの監査結果・修正指示](docs/CARRIER_R3_AUDIT_HANDOFF_2026-10-24.md)
