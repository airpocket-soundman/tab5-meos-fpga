# Serial file upload acknowledgements

`put PATH` retains its existing base64-line protocol. `put --ack PATH` adds flow control for slow storage operations:

1. Send `put --ack PATH` followed by a newline. As with ordinary `put`, the destination is created or truncated. Use a fresh path for tests.
2. Wait for `put: ready`. The file has now been opened successfully.
3. Send one base64 line, normally 76 characters. Wait for `put: ack N`, where N is the cumulative number of decoded bytes successfully written. Then send the next line. Keep lines below the shell's 128-byte input limit and split base64 on four-character boundaries.
4. Send a line containing only `.`. Verify the returned byte count and CRC32 in `put: wrote N bytes, crc32 XXXXXXXX`. Close errors are checked before success is reported.

The acknowledgement follows the VFS write; the FAT adapter synchronizes that write. It does not add SCSI cache flush or safe-eject guarantees. Input is not echoed during upload. The original protocol is unchanged unless `--ack` is selected.

## 検証

固定待ち時間だけで次の行を送る試験では、ストレージ側の準備・書き込みに送信が先行し、PC側のシリアル送信タイムアウトが発生した。準備完了と各行の書き込み確認を待つ方式を追加した。

Full-Speedハブ経由のexFAT `/usb`とFAT `/usb1`へ、それぞれ8192バイトの新規ファイルを送り、完了応答のバイト数・CRC32と全内容の読み戻しが一致した。CRC32はそれぞれ`bc4ee937`、`e6dbf5d6`。この試験中にカードリーダーも接続され、3台目として認識・マウントされた。キーボード受信機はこの試験では接続されていない。

原文と実行記録はローカルの`D:/tmp/meos-idf/native-put-ack-manifest.json`と`native-put-ack-test.log`に保持する。シリアル送信方式の修正と、キーボード入力を続けながらの長時間I/O検証は区別する。
