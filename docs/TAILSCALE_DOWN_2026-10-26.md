# Tailscale初期downと停止処理の修正

2026-10-26。実装コミット：`191fc24`。

## 原因と変更

- `tailscale_boot_start()` がシェルごとに接続を開始していた。保存済みIDの初期化だけに変更し、起動・新しいシェル・GUIを開くだけでは接続しない。接続は `tailscale up` またはGUIのU／AuthKey操作で明示する。
- downは希望状態を変えるだけで、認証待ちの応答が戻るとNEEDS_LOGIN／RUNNINGへ上書きできた。制御通信に世代番号を付け、downで旧世代を無効化する。登録結果とmap結果を公開する前にロック内で再確認し、古い応答では状態・URL・コールバックを戻さない。
- 認証待ちの受信・HTTP/2処理にも中止確認を入れた。終了とupが重なる場合は、daemonフラグの更新をロック内で行い、新しいupを取りこぼさない。
- downで認証URLをクリアする。同じURLは再試行のたびに出力しない。一般の名前解決から暗黙にupする処理も除いた。
- GUIのU操作は `tailscale_up()` を使い、daemonが終了済みでも正しく開始する。タスク作成失敗時はdownへ戻す。

## 検証

- ESP-IDF v5.4.4、native DMA USB、BLE・MicroPython・SLを含む構成でビルド成功。COM22のTab5へ書き込み、ハッシュ照合成功。
- 再起動後の `tailscale status` がstopped。10秒の取得中に認証案内0件。
- 明示的upで実際の認証待ちへ到達した後にdown。切断応答と、その後のstoppedを確認。続く24秒の観測で認証案内0件。[機密情報を除いた結果](validation/tailscale-down-2026-10-26.json)。
- `tests/test_tailscale_lifecycle.py` は実際のC関数を抽出し、ホスト上でネットワーク・タスクだけをモックしてコンパイル・実行する。初期down、停止中syncの無通信、register/followup/map応答中のdown、同じURLの再表示抑制、正常map処理、シェル初期化が明示upを保持すること、旧世代の無効化、中止された受信、タスク作成失敗を確認した。

```powershell
python tests/test_tailscale_lifecycle.py --cc D:/tmp/tab5-zig013/ziglang/zig.exe
```

このホスト試験は実際の暗号通信・スケジューラの完全な並行性検証ではない。実機試験は未認証状態での停止と再起動を対象とし、認証済みtailnetの長時間通信・同時クライアントによる高速up/downの繰返しは未検証。

## 資源

直前の測定表示版に対してBIN +208 B、静的HP SRAM +8 B、内部ヒープ予約192 KiBは変更なし。同じSDK・ツールチェーン・機能設定で比較した。起動時には24 KiBのTailscaleタスクスタックと、接続試行用の512 KiB PSRAM応答バッファを確保しない。

実機でdown後に内部ヒープ空き110,400 B、PSRAM空き20,705,712 B、確保失敗はいずれも0を確認。実行時の差分は接続状態や周辺機器で変わるため、固定RAM差分と混同しない。最新値は[資源記録](RESOURCE_USAGE.md)と `resources/current.json` に保存した。
