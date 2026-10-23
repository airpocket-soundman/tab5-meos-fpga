# Native USB DMA investigation / 独自USB DMAの調査

2026-10-23. Target: Tab5 ESP32-P4, ESP-IDF 5.4.4.

## Initialization order / 初期化順序

The original HAL requested host mode and immediately configured FIFOs. Switching to host mode is asynchronous. TinyUSB 0.19.0 waits for GINTSTS.CURMODE before configuring the host FIFOs; the native HAL now does the same, with a 100 ms timeout and FIFO register readback checks.

元のHALはホストモードを要求した直後にFIFOを設定していた。切り替え完了は非同期である。TinyUSB 0.19.0の初期化順序を参考に、GINTSTS.CURMODEを確認してからFIFOを設定するよう修正した。100 msのタイムアウトと設定値の読み戻し照合を追加した。

Controlled comparison on the same device and original descriptor DMA transfer implementation:

| Configuration | Observation |
| --- | --- |
| Wait omitted; register checks retained | NPTX matched `01000200`; PTX read `00000000`, expected `00800300`. Initialization rejected safely. |
| Wait present | RX 512 words; NPTX `01000200`; PTX `00800300`. A new 36-byte file was written and fully read back, CRC32 `8b16cf1b`. |

The result identifies an initialization ordering defect. It does not establish that all previous timeouts have the same cause. The transfer engine remains descriptor DMA; no FIFO-only fallback, artificial transfer delays or extra transfer channels were introduced by this fix.

この比較では初期化順序の不備を確認した。過去のすべてのタイムアウトが同じ原因とは断定しない。転送方式は元のディスクリプタDMAを維持し、FIFO方式への切り替え、転送間の人工的な待ち時間、追加チャネルによる回避は採用していない。

Reference: [TinyUSB 0.19.0 DWC2 host initialization](https://github.com/hathach/tinyusb/blob/0.19.0/src/portable/synopsys/dwc2/hcd_dwc2.c).

## File verification / ファイル検証

New files of 37, 512 and 8192 bytes passed CRC32 checks and full readback. All three were read again after a board reset and matched the original bytes. CRC32 values were `6c227be1`, `a166aacf` and `ab50f0c0`, respectively. The existing microSD verification file was also readable with both volumes mounted. The test media was 058f:6387 with 30,720,000 sectors of 512 bytes. No existing user file was overwritten and no format was performed.

37・512・8192バイトの新規ファイルについてCRC32と全内容の読み戻しが一致し、ボード再起動後も3ファイルすべて一致した。両ボリュームの同時マウントとmicroSDの既存検証用ファイル読み出しも確認。既存利用者ファイルの上書き・フォーマットは行っていない。

## Scope / 対応範囲

### Reconnection / 再接続

The first replug test failed to remount. IDF's HAL masks connection events with a debounce lock on disconnect as well as connect; the native loop did not release that lock before waiting for the next connection edge. The loop now releases the lock and checks physical connection state instead of depending solely on a single event. After MSC removal it returns directly to connection waiting, avoiding an extra removal wait if reinsertion occurs during cleanup.

After the fix, one physical unplug/replug cycle without a board reset produced disconnect, enumeration, MSC ready and `/usb` mount logs. All three original test files were fully read and matched; a new 512-byte file was written and fully read back with CRC32 `a166aacf`. microSD access remained available. Rapid replug, removal during I/O and open-file reconnect are not yet hardware-verified.

初回の抜き差しでは再マウントしなかった。IDF HALが切断時にも設定するデバウンスロックを解除せず、次の接続イベントを待っていた。ロック解除と物理接続状態の確認に変更し、MSC切断後にもう一度取り外しを待つ処理も回避した。修正後はボード再起動なしの抜き差し1回で再列挙・再マウントを確認。既存3ファイルの全内容一致と、新規512バイトの書き込み・全内容読み戻しを確認した。高速な抜き差し、I/O中の取り外し、ファイルを開いたままの再接続は実機未検証。

One directly connected device, boot HID keyboard or MSC BOT/SCSI LUN 0 with 512-byte sectors. No hub or UVC camera support yet. Idle storage replug was hardware-verified once; safe-eject support remains pending. Removing storage during a write can corrupt its filesystem. The driver itself cannot be replaced while the firmware is running.

直結1台のブートHIDキーボード、またはMSC BOT/SCSIのLUN 0・512バイトセクターが対象。ハブ・UVCカメラは未対応。I/O停止中のストレージ抜き差しを1回確認。安全な取り外し機能は未実装。書き込み中の取り外しはファイルシステムを破損する可能性がある。動作中のドライバコード差し替えには対応しない。

No CPU-load or throughput benchmark has been performed. DMA support alone is not evidence of higher application performance.
