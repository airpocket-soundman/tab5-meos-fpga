# USBホスト拡張（2026-10-23）

## 実装範囲

ESP-IDF v5.4.4のDWC2低レベルHALを使用し、FreeRTOSのUSBホストスタックは起動しない。制御転送に加えて、非同期の割り込みIN・bulk転送を追加した。制御用とデータ用のハードウェアチャネルを分け、データ用チャネルは論理エンドポイント間で直列に切り替える。DATA0/DATA1はエンドポイントごとに保存する。周期スケジュールとDMAキャッシュ同期を設定する。

対象はUSB-Aへ直接接続した1機器。HIDブートキーボード、またはUSB Mass Storage BOT/SCSI transparent subclassのLUN 0を扱う。USBハブ、マウス、任意のHID report descriptor解析、UAS、exFAT、512バイト以外のセクター、READ CAPACITY(16)が必要な媒体は未対応。

## 無線キーボード受信機

従来はendpoint 0のGET_REPORTで8バイトを取得し、20回連続失敗すると`keyboard gone`を表示していた。失敗時にUSB抜去と通信失敗を区別していなかった。

新実装はconfiguration descriptorからブートキーボードのinterrupt IN endpointを選択し、非同期にレポートを受け取る。入力のないNAK待ちは切断として数えない。OS側のキーリピートはレポート待機中も処理する。ブートプロトコルを拒否した機器は未対応として記録する。

初期試作でユーザーから入力成功の報告あり。シリアルの`keys`で83回のキー押下、転送エラー0を確認した。取得時の接続種別はnoneで、継続接続・抜き差し全般の保証ではない。受信機のVID/PIDと物理画面はこの確認では記録していない。旧来の統計項目名`i2c errors`は転送方式共通の`transfer errors`へ変更。

## USBメモリ

BOTのCBW・データ・CSW、タグ・署名・残量・ステータス確認、TEST UNIT READY、REQUEST SENSE、READ CAPACITY(10)、READ/WRITE(10)を追加。ブロックデバイスとしてFatFsへ接続し、FAT12/16/32を`/usb`へマウントする。自動フォーマットはしない。

ファイル操作はMeOS mutexで直列化。書き込み後はFatFsを同期する。媒体の突然の取り外しに対する電源断耐性やデバイス内部キャッシュの永続化は保証しない。取り外し後の古いハンドルは失敗させ、残存ハンドルを閉じるまで再マウントしない。実機での媒体認識・FATマウント・ディレクトリ読み取りを確認。ファイル内容の読み取り・書き込み・抜去・再接続は未検証。BOTエラー復旧と機器差への対応は追加評価が必要。

FatFsはESP-IDF v5.4.4同梱版から取得し、ライセンスと変更点を`third_party/fatfs/README.meos.md`へ記録。VFSの一覧名は23バイト上限のため、長い名前は利用可能な8.3別名で表示する。

## 確認

- Tab5向けサイズ最適化ビルド成功。
- 割り込みINの初期試作を実機へ書き込み、上記の入力統計を取得。
- USBメモリ試作を実機へ書き込み、起動を確認。その後、ユーザーによる媒体挿入を受け、`mount`の`fat on /usb`と`ls /usb`の正常応答を確認。既存ファイルは変更していない。
- ホスト上のFAT12テストイメージで読み取り、ディレクトリ・長い名前のファイル作成、書き込み・同期、二重writer拒否、アンマウント・再マウント後の内容照合、一覧と削除を確認。USB転送・VFSアダプターの実機検証を代替するテストではない。
- 再実行：`python tests/usb_host/run_fat_test.py --cc <zig.exeの絶対パス>`。`build-tab5-size/config/sdkconfig.h`が必要。実機の媒体は使用しない。

ローカルログ：`D:\tmp\meos-idf\usb-receiver-stats.log`、`build-usb-msc.log`、`flash-usb-msc.log`、`boot-usb-msc.log`。媒体のファイル名を含む生ログは公開しない。
