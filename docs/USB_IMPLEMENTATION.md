# USBホスト実装（2026-10-23）

## 現在の構成

USB-AホストはTinyUSB 0.19.0へ移行した。`OPT_OS_NONE`を指定し、MeOSのUSBタスクがDWC2の割り込み状態とTinyUSBのイベント処理をポーリングする。USB処理とMSCブロックI/OはMeOS mutexで直列化する。FreeRTOSスケジューラーとESP-IDF USB Host Libraryは使用しない。

PHYの電源・クロック設定はESP-IDFの`usb_new_phy`に任せ、列挙・エンドポイント管理・HID・MSC/BOT・DWC2転送にはTinyUSBの公開実装を使用する。現在はDWC2 FIFO方式で、DMAは無効。TinyUSB側のポート番号1がESP32-P4のHigh-Speedコントローラーに対応する。独自HALで使っていたIDFの番号0とは異なる。

TinyUSBのクラスドライバーとDWC2転送ロジックは変更していない。ESP32向けプラットフォームフックのみ、CPU割り込みの登録をしないポーリング構成とROM delayに変更した。取得元・コミット・ライセンス・変更点は`third_party/tinyusb/README.meos.md`へ記録した。

## 参照した実装

- [M5Stack公式Tab5 UserDemo](https://github.com/m5stack/M5Tab5-UserDemo/blob/main/platforms/tab5/main/hal/components/hal_usb.cpp)：ESP-IDF USB Host Library、HID Host、FreeRTOSタスク。
- [Tab5thinclient](https://github.com/airpocket-soundman/Tab5_thinclient)：TinyUSB HostのHID／MSC API、FatFs。ローカルの`src/Tab5KeyboardInput.cpp`と`src/main.cpp`も確認。
- [TinyUSB DWC2 Host](https://github.com/hathach/tinyusb/blob/0.19.0/src/portable/synopsys/dwc2/hcd_dwc2.c)：コントローラーの転送・応答制御。

## USBメモリ

現段階の対象は直結したMSC BOT/SCSI transparentのLUN 0、512バイトセクター。TinyUSBのREAD/WRITE(10)をMeOSのblockdevへ接続し、FatFsで`/usb`へマウントする。`/sd`は独立したボリュームとして併存する。自動フォーマットはしない。exFAT、UAS、512バイト以外のセクターは未対応。

転送完了時はCSWの署名・タグ・ステータス・残量を確認する。非同期I/Oには永続バッファーを使い、タイムアウト後に呼び出し元の解放済みメモリーへアクセスさせない。転送失敗後は再接続するまで追加I/Oを拒否する。FatFs書き込み後には`f_sync`を行う。SCSI SYNCHRONIZE CACHEはまだ追加しておらず、突然の電源断に対する永続化は保証しない。

取り外し通知を受けると媒体を無効化し、古いファイルハンドルを閉じるまで再マウントしない。取り外し・再接続の実機試験、長時間転送と機器差の評価は未完了。

### 実機確認

- 接続媒体：058f:6387、30,720,000セクター、約15 GB。
- 37バイト、512バイト、8192バイトの新規ファイルを作成し、`put`のCRC32をPC側の値と照合。
- 3ファイルとも`cat`で全内容を読み出し、PC側の原文と照合。
- 再起動後、3ファイルとも再度読み出して全内容が一致。
- `/usb`と`/sd`の同時マウント、および既存のmicroSDテストファイルの読み出しを確認。
- internal heapのfree約111 KB、fail count 0。PSRAMのfree約20 MB。これは測定時点の値。

テストファイルは媒体に残している。原文・CRC・実行ログはローカルの`D:/tmp/meos-idf/tinyusb-test-manifest.json`、`tinyusb-roundtrip.log`、`tinyusb-persistence.log`へ保存し、媒体上の既存ファイル名を含む生ログは公開しない。上記は複数セクターにまたがるファイルの試験であり、すべての転送サイズ・カード種類・電源断の保証ではない。

## キーボードと次の段階

ブートHIDキーボードのレポートをTinyUSBのHID Hostから受け取り、既存のMeOSキー変換・IMEへ渡す。キーリピートと修飾キーを保持する。旧独自ドライバーでは受信機の83回入力・転送エラー0を確認済みだが、TinyUSB移行後の受信機実機再確認は未完了。

ハブは現コミットでは無効。次にTinyUSBのハブ機能、複数MSCインスタンス、独立したVFSマウント、キーボード併用、機器ごとの取り外し・再接続を実装・確認する。

## 旧実装との関係

元の独自DWC2実装は割り込みINキーボードとUSB FATのマウント・一覧取得まで動作したが、連続するFAT書き込みでOUT転送が停止した。TinyUSBへの置き換え後に上記の書き込み・保持試験が通った。旧実装の停止原因そのものは未確定であり、DMAのハードウェア障害と断定していない。診断用の待ち時間、特殊タグ、読み戻しによる回避、追加チャネル等の試作コードは採用していない。
