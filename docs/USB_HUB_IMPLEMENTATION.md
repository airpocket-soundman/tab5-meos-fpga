# Native USB hub implementation / 独自USBハブ実装

2026-10-23. The native descriptor DMA driver now enumerates one external hub and provides three independent MSC slots. The tested hub is 05e3:0606, four ports, connected at Full-Speed. High-Speed hubs with mixed-speed devices still require buffer DMA and split-transaction support; those configurations are not verified.

## 構成

- 外部ハブ1段、最大7ポート、MSC BOT/SCSI LUN 0を最大3台。ネストしたハブとハブ経由HID入力は未対応。
- デバイス記述子は先頭8バイトを読み、EP0パケットサイズを確定してから全体を取得する。
- ポートの給電・接続状態・リセット・接続変化の解除をクラス要求で管理し、子機器に別アドレスを割り当てる。
- DATA PIDはアドレスとエンドポイントごとに保持する。MSCの容量・タグ・入出力エンドポイント・有効状態を媒体ごとに分離する。
- 共有のMeOS mutexでハブ制御とSCSIコマンドを直列化する。複数媒体は同時にマウントできるが、USB転送の要求処理は直列化する。
- マウント先は`/usb`、`/usb1`、`/usb2`。microSDは`/sd`を維持する。FatFsは4ボリューム、VFSは8マウントへ拡張した。
- 取り外し時は対象ボリュームを無効化する。古いファイルハンドルのあるスロットは再利用しない。FATロックを保持したI/Oがバスロックを取るため、ハブ処理はバスロック保持中にFATへ入らない。

## 実機確認の範囲

| Hub port | Address | Device | Sectors (512 bytes) |
| --- | --- | --- | --- |
| 2 | 3 | 090c:2000 | 245760000 |
| 4 | 5 | 058f:6387 | 30720000 |

2本を別アドレスで認識。既存のFAT媒体を`/usb1`へマウントし、37・512・8192バイトの既存検証用ファイルを全内容読み出して一致を確認した。`/sd`の既存検証用ファイルも読めた。もう1本は読み取りでexFATと判別した。exFAT対応の追加と2本の新規書き込み試験は別途記録する。フォーマットは行わない。

### exFAT追加後の確認

FatFsのexFATを有効化し、無効時に未定義となる`CONFIG_FATFS_USE_LABEL`へ既定値0を与えた。既存のexFAT媒体を`/usb`、FAT媒体を`/usb1`へ同時マウント。各媒体で新規512・8192バイトの書き込み、CRC32と全内容の読み戻しが一致した。ボード再起動後も4ファイルが一致し、各ファイルが反対側の媒体には存在しないことを確認した。`/sd`の既存検証用ファイルも読み出した。

| Volume | Bytes | CRC32 |
| --- | --- | --- |
| /usb (exFAT) | 512 | 04ab5014 |
| /usb (exFAT) | 8192 | c5bfafd6 |
| /usb1 (FAT) | 512 | b083cb6e |
| /usb1 (FAT) | 8192 | d8ef7d83 |

試験用ファイルは媒体に残している。途中でPCから中断した検証用ファイルも残る。既存ファイルの上書きや再フォーマットはしていない。性能測定、長時間試験、全ファイル操作の回帰は未実施。原文・CRCはローカルの`native-hub-test-manifest.json`と`native-hub-fat-large-manifest.json`、再起動・媒体分離の確認は`native-hub-persistence.log`へ保存した。

ホスト上の4ボリュームFAT12試験では、媒体間の内容分離、USBボリュームのアンマウント後もSDへアクセスできること、作成・読み書き・同期・再マウント・長い名前・ディレクトリ操作・削除・重複書き込み拒否が通った。

ハブ配下の個別抜き差し、ハブ本体の取り外し、ファイルを開いたままの再接続、長時間試験、HID入力との併用は未確認。Full-Speedで確認した結果を480 Mbit/sでの動作確認とは扱わない。USB 2.0 High-SpeedハブとFull/Low-Speed機器の混在は、独自バッファDMAとスプリット転送の追加対象。

参照：[Espressif USB Host limitations](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)、同梱TinyUSB 0.19.0の`src/host/hub.c`とDWC2ホスト実装。
