# USBホスト実装（2026-10-23）

## 現在の構成

USB-Aホストは独自ディスクリプタDMAドライバへ集約する。現在の既定は`native_dma`。MeOSのUSBタスクが接続状態とDWC2のイベントをポーリングし、MSCブロックI/OはMeOS mutexで直列化する。FreeRTOSスケジューラーとESP-IDF USB Host Libraryは使用しない。

PHYの電源・クロック設定はESP-IDFの`usb_new_phy`、コントローラー操作はIDFのDWC2 HALを利用する。列挙、ブートHID、MSC/BOTは独自ドライバ。転送は元のディスクリプタDMAを維持し、ホストモード完了待ちとFIFO設定の読み戻し検査を追加した。初期化の比較試験は[DMA調査記録](USB_DMA_INVESTIGATION.md)へ記録した。

TinyUSB 0.19.0のOS_NONE・FIFO実装は比較用に保持する。CMakeの`-DM5OS_USB_BACKEND=native_dma`または`-DM5OS_USB_BACKEND=tinyusb`で切り替える。既存ビルドディレクトリのキャッシュは以前の選択を保持するため、明示指定する。同一ファームウェアには選択した実装だけをリンクする。TinyUSB版の取得元・ライセンス・変更点は`third_party/tinyusb/README.meos.md`を参照。

## 参照した実装

- [M5Stack公式Tab5 UserDemo](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_usb.cpp)：ESP-IDF USB Host Library、HID Host、FreeRTOSタスク。
- [Tab5thinclient](https://github.com/airpocket-soundman/Tab5_thinclient)：TinyUSB HostのHID／MSC API、FatFs。ローカルの`src/Tab5KeyboardInput.cpp`と`src/main.cpp`も確認。
- [TinyUSB DWC2 Host](https://github.com/hathach/tinyusb/blob/0.19.0/src/portable/synopsys/dwc2/hcd_dwc2.c)：コントローラーの転送・応答制御。

## USBメモリ

現段階の対象は直結したMSC BOT/SCSI transparentのLUN 0、512バイトセクター。独自BOTのREAD/WRITE(10)をMeOSのblockdevへ接続し、FatFsで`/usb`へマウントする。`/sd`は独立したボリュームとして併存する。自動フォーマットはしない。exFAT、UAS、512バイト以外のセクターは未対応。

転送完了時はCSWの署名・タグ・ステータス・残量を確認する。HALの静的DMAバッファを介してキャッシュ同期し、完了後に読み出し先へコピーする。タイムアウト時は転送チャネルを閉じる。BOT Reset Recoveryなどのエラー復旧は未完了。FatFs書き込み後には`f_sync`を行う。SCSI SYNCHRONIZE CACHEと安全な取り外しコマンドはまだ追加しておらず、突然の取り外しや電源断に対する永続化は保証しない。

取り外し通知を受けると媒体を無効化し、古いファイルハンドルを閉じるまで再マウントしない。取り外し・再接続の実機試験、長時間転送と機器差の評価は未完了。

### 実機確認

- 接続媒体：058f:6387、30,720,000セクター、約15 GB。
- 37バイト、512バイト、8192バイトの新規ファイルを作成し、`put`のCRC32をPC側の値と照合。
- 3ファイルとも`cat`で全内容を読み出し、PC側の原文と照合。
- 再起動後、3ファイルとも再度読み出して全内容が一致。
- `/usb`と`/sd`の同時マウント、および既存のmicroSDテストファイルの読み出しを確認。
- 上記のファイル試験はTinyUSB FIFO版と修正した独自DMA版の両方で実施。負荷・速度の比較測定は未実施。

テストファイルは媒体に残している。原文・CRC・実行ログはローカルの`D:/tmp/meos-idf/native-dma-test-manifest.json`、`native-dma-roundtrip.log`、`native-dma-persistence.log`へ保存し、媒体上の既存ファイル名を含む生ログは公開しない。TinyUSB版の比較記録も保持する。上記は複数セクターにまたがるファイルの試験であり、すべての転送サイズ・媒体・電源断の保証ではない。

## キーボードと次の段階

ブートHIDキーボードのレポートを独自ドライバで受け取り、MeOSキー変換・IMEへ渡す。キーリピートと修飾キーを保持する。初期化修正前の独自ドライバでは受信機の83回入力・転送エラー0を確認済みだが、今回の修正版での再確認は未完了。

ハブは未対応。次に独自DMAドライバへハブ管理、複数MSCインスタンス、独立したVFSマウント、キーボード併用、機器ごとの取り外し・再接続を追加する。TinyUSBを実装の参照・比較に用いる。USBカメラ（UVC）は別段階で追加する。

## 旧実装との関係

元の独自DWC2実装はマウント・一覧取得まで動作したが、連続するFAT書き込みでOUT転送が停止した。TinyUSBを比較用に導入し、その初期化順序からホストモード完了待ちの欠落を見つけた。待ちを省いた比較版ではFIFO設定の不一致を再現し、修正版では読み書き・再起動後保持が通った。過去のすべての停止が同じ原因とは断定しない。診断用の転送間待ち時間、特殊タグ、読み戻しによる回避、追加チャネル等の試作コードは採用していない。
