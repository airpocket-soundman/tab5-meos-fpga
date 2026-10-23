# USB connection diagnostics

The native DMA firmware provides `lsusb`. It displays a cached snapshot of enumeration state, without issuing extra USB requests or holding the USB bus while printing. The TinyUSB comparison build does not currently provide this command.

Columns: USB address, parent hub/address and port, VID:PID, negotiated speed, device class, recognized interface type, USB configuration state, and assigned storage mount. `configured` means SET_CONFIGURATION succeeded; it does not guarantee a usable filesystem. `(unmounted)` identifies an assigned storage slot whose mount failed. Unsupported but addressed devices are included. Devices that fail before a valid descriptor/address is obtained cannot be listed.

`mount` lists active filesystems; `keys` reports keyboard input and transfer counters. USB block-device nodes are not exposed under `/dev`.

## 実機表示（2026-10-23）

| Address | Parent:port | VID:PID | Speed | Type | Mount |
| --- | --- | --- | --- | --- | --- |
| 1 | 0:0 | 05e3:0606 | FS | Hub | — |
| 2 | 1:1 | 14cd:1212 | FS | USB microSD reader | /usb |
| 3 | 1:2 | 090c:2000 | FS | Storage | /usb1 |
| 5 | 1:4 | 058f:6387 | FS | Storage | /usb2 |

カードリーダーはMSCとして61,255,680個の512バイトセクターを報告し、カードのマウントとルートディレクトリ読み出しを確認した。カードには書き込んでいない。受信機を抜いてそのポートへリーダーを挿した構成で、ハブとストレージ3台を一覧できた。カード交換を挿したまま検出する処理と、複数スロット式リーダーのLUN 1以降は未対応。

マウント名は空きスロットへの割り当て順であり、媒体を恒久的に識別する名前ではない。機器構成を変えて再起動すると割り当てが変わるため、`lsusb`でポートとマウントを照合する。上表より前の試験ではexFATメモリが`/usb`だったが、リーダーを含めた再起動後は`/usb1`になった。UUID・ボリュームラベルによる固定マウントは未実装。

USB 2.0 High-Speedハブ、Full/Low-Speed機器との混在、ハブ多段接続は、この表示コマンドを追加したことによって対応済みにはならない。転送機能の対応範囲は[USB実装](USB_IMPLEMENTATION.md)、[ハブ検証記録](USB_HUB_IMPLEMENTATION.md)を参照。
