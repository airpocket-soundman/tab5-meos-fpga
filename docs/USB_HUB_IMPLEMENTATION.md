# Native USB hub implementation / 独自USBハブ実装

2026-10-23. The native DMA driver enumerates one external hub and provides three independent MSC slots. The tested hub is 05e3:0606, four ports, connected at Full-Speed. The later buffer-DMA implementation adds mixed-speed support; see the validation below.

## 構成

- 外部ハブ1段、最大7ポート、MSC BOT/SCSI LUN 0を最大3台、ブートHIDキーボード1台。ネストしたハブとマウス入力は未対応。
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

### ハブ経由キーボード

受信機1ea7:0002をポート1・アドレス2、Full-Speed、割り込みIN 0x81・8バイトとして認識。ブートプロトコルを指定し、ストレージとは別の周期DMAチャネル・ディスクリプタ・バッファを割り当てた。他の転送処理が先にデコードしたHID完了イベントも保存して、次のキーボード処理へ渡す。ポート状態の監視は250 ms、キーボード処理は待機時5 ms間隔とし、キー変換・IME・リピートへ接続した。

利用者が無線キーボードで`echo HUBKEY`を入力し、シリアルログにもコマンドと`HUBKEY`の実行結果を確認した。入力12回、最後はEnter。その後の実際の抜き差しを利用者が報告し、切断通知と再認識状態を確認。この抜き差しを含む試験後、転送エラーの累積値は12。0エラーとは扱わない。

キーボード用チャネルを有効にしたファームでも、2媒体の既存512・8192バイト4ファイルの全内容と媒体分離、SD読み出しを再確認した。連続した高速入力、文字を入力しながらの長時間ストレージ負荷、キーリピート・全修飾キー・Low-Speed機器の回帰は未確認。

ストレージの個別抜き差し、ハブ本体の取り外し、ファイルを開いたままの再接続、長時間試験は未確認。Full-Speedで確認した結果を480 Mbit/sでの動作確認とは扱わない。USB 2.0 High-SpeedハブとFull/Low-Speed機器の混在への対応と、その後の実機確認は下記を参照。

参照：[Espressif USB Host limitations](https://docs.espressif.com/projects/esp-usb/en/latest/esp32p4/usb_host.html)、同梱TinyUSB 0.19.0の`src/host/hub.c`とDWC2ホスト実装。

## High-Speed Ethernet hub and concurrent I/O

A Realtek 0bda:5411 USB3 hub negotiated USB2 High-Speed on Tab5. Two USB drives,
a USB microSD reader and RTL8153 Ethernet enumerate together. Both drives passed
new 8192-byte and 512-byte write/read checks. After separating MSC transaction
ownership from the HAL mutex, stored-file reads and new writes passed alongside
20/20 ICMP replies. See [Ethernet results](USB_ETHERNET_IMPLEMENTATION.md).
These storage write/read results predate the buffer-DMA conversion below.

## Buffer DMA and mixed-speed keyboards

The native host now defaults to buffer DMA for all six channels: control,
storage, keyboard, ECM RX/TX and ECM notifications. DWC2's Descriptor DMA mode
cannot perform split transactions, and its mode switch applies to the whole
controller. The original driver remains independent of TinyUSB and FreeRTOS.

Device routes retain downstream speed, hub address and port. Full-/low-speed
endpoints beneath a high-speed hub use start-split and complete-split phases.
The engine advances buffers and DATA toggles only after a completed transaction.
NAK/NYET are retried without declaring a keyboard disconnect. Periodic completion
is scheduled in the start-split frame; fresh HFNUM/ODDFRM programming immediately
before CHENA avoids stale scheduling. Failed halts quarantine DMA buffers.

On hardware, the Realtek 0bda:5411 hub enumerated the 1ea7:0002 Full-Speed
receiver (EP81, 8-byte reports), two High-Speed drives and RTL8153 Ethernet.
After removing unnecessary delays between split phases, keyboard reports and
shell command execution were captured, and the user confirmed input/execution.
Ethernet ping returned 10/10 replies (21–53 ms) with the receiver present.

The normal image removes temporary kbdDMA diagnostics. On its final boot,
a FAT drive mounted and Ethernet linked. The subsequent new-file write and
ping attempt could not validate coexistence: the USB topology became empty
and /usb was absent. No write/read success is claimed for this buffer-DMA
regression. Both-drive write/read, sustained keyboard input during storage
I/O and hot-plug stress remain pending. Earlier descriptor-DMA storage results
above are historical and do not substitute for these tests.

Host tests include the actual engine with fake MMIO and check split phase
sizes, PID preservation, NYET retry, PING, short packets, bounds, STALL,
late scheduling, frame expiry and halt quarantine. They do not emulate USB
signals, real DMA or cache coherency.

Run: `python tests/usb_host/run_buffer_dma_test.py --cc PATH_TO_ZIG`

References: [TinyUSB DWC2 implementation](https://github.com/hathach/tinyusb/blob/master/src/portable/synopsys/dwc2/hcd_dwc2.c),
[Linux DWC2 Descriptor DMA restriction](https://github.com/torvalds/linux/blob/master/drivers/usb/dwc2/hcd_ddma.c).
