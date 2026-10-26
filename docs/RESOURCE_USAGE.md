# Resource usage / 資源使用量

Baseline measured 2026-10-24; latest snapshot updated 2026-10-26. KiB = 1,024 bytes; MiB = 1,048,576 bytes.

## 同一条件での比較

機能追加前の上流版 a6a9f23 と現在の版を、同じ ESP-IDF v5.4.4、GCC 14.2.0、サイズ最適化、SDK設定でビルドしました。フラッシュの増加率とRAMの増加率は別です。

| 測定対象 | 追加前 | 機能追加後・測定表示追加前 | 現在・測定表示とTailscale修正込み |
|---|---:|---:|---:|
| ファームウェアBIN | 1,076,832 B | 1,410,224 B | 1,410,880 B |
| 静的HP SRAM（内部ヒープ予約を含む） | 445,374 B | 463,110 B | 463,118 B |
| 内部ヒープ予約領域 | 196,608 B | 196,608 B | 196,608 B |
| 静的HP SRAM（内部ヒープ予約を除く） | 248,766 B | 266,502 B | 266,510 B |

機能追加による増加は、BIN **333,392 B（325.58 KiB、30.96%）**、静的HP SRAM **17,736 B（17.32 KiB、3.98%）**です。今回の `mem --json` 追加はBINをさらに448 B増やし、静的RAMは増やしていません。実行時に512 Bのローカルバッファを使います。2026-10-26のTailscale初期down・停止処理修正は、さらにBIN208 B、静的RAM8 B増です。比較JSONは `resources/tailscale-default-down-comparison.json` に記録しています。

最初に書き込んだデバッグ最適化版は1,139,632 Bでした。そこからの単純差分には最適化による削減が混ざるため、上表はサイズ最適化版を基準にしています。

## RAMの読み方

静的HP SRAMには192 KiBの内部ヒープ全体が既に含まれます。実行時のヒープ使用量をこの値へ再加算すると二重計上になります。実際に使っている領域を概算する場合は「静的領域からヒープ予約を除いた値＋使用中ヒープ」を使い、同じ実行条件で比較します。

物理HP SRAMは768 KiBですが、今回のリンカーが利用可能とする領域は576,464 Bです。静的確保463,118 Bはこの領域の80.34%、残りは113,346 Bです。この残りがそのままMeOSヒープに追加されるわけではなく、セクション配置にも制約があります。

PSRAMは物理32 MiBで、表示用の4 MiBを除いた28 MiBをMeOSヒープとして扱います。

2026-10-26の修正版で、Tailscaleのupが認証待ちへ到達した後にdownし、24秒の再表示なしを確認した後の待機値です。前面VM・SSHサーバは動かしていません。USB機器は接続状態のままです。ピークは起動後累積です。

| ヒープ | 使用中 | 空き | 最大連続空き | 起動後ピーク |
|---|---:|---:|---:|---:|
| 内部RAM（全196,608 B） | 86,208 B | 110,400 B | 106,272 B | 110,800 B |
| PSRAM（全29,360,128 B） | 8,654,416 B | 20,705,712 B | 20,176,912 B | 9,183,200 B |

この時点の確保失敗カウンタは両方0です。過去の初期版ログは内部RAM使用80 KiB、PSRAM使用8,246 KiBでした。現在は約84.2 KiBと8,451.6 KiBですが、接続機器・画面状態・ネットワーク条件が同一ではなく、これは参考比較です。別の試験後にはPSRAM 9,031 KiBも観測しており、画面バッファ等の保持状態で変わります。過去ログの「KB」は切り捨てたKiBでした。

## 起動・実行時に追加される主な領域

| 機能 | 主な追加確保 | 解放・注意 |
|---|---|---|
| MicroPython | PSRAMのGCヒープ512 KiB、内部RAMのタスクスタック32 KiB、VFS登録約1.2 KiB PSRAM | 通常のVM終了時に解放 |
| SSHサーバ／SFTP | サーバの内部RAMスタック16 KiB、SFTPエンジン約70 KiB PSRAMとSSHセッション領域 | SFTP用の別スタック16 KiBを重ねて数えない |
| SL | コンテキストと文字キャンバスで最大約28 KiB PSRAM | 通常完了・取消・期限終了時に解放 |

これらは設計上の主要確保量で、総コストやピーク全体を表すものではありません。同時実行する組合せは実測が必要です。特に内部ヒープは総量192 KiBなので、空き容量だけでなく最大連続空きも確認します。

## 開発途中のBIN記録

以下は累積チェックポイントです。設定や同時変更が異なるため、隣同士の差分を個々の機能の純粋なコストとはみなしません。共有ライブラリの費用も単純加算できません。

| チェックポイント | BIN |
|---|---:|
| 上流・サイズ最適化 | 1,076,832 B |
| 独自USB | 1,098,496 B |
| ハブ・exFAT | 1,106,464 B |
| lsusb | 1,110,128 B |
| Ethernet・SSH統合 | 1,132,352 B |
| BLE・カメラ・表示統合 | 1,206,176 B |
| SFTP統合 | 1,230,800 B |
| 初期MicroPython統合 | 1,396,080 B |
| MicroPython周辺機器API | 1,402,544 B |
| SL・SSHフィンガープリント | 1,410,016 B |
| SSH EOF修正 | 1,410,224 B |
| バイト単位メモリ表示 | 1,410,880 B |

C6更新用ファームウェアを一時的に内包した保守イメージ（2,216,592 B）は通常版の比較から除外しています。履歴の根拠は開発ログであり、現在と追加前の再ビルド成果物以外は、統一条件で再測定したものではありません。

## 再現条件と測定データ

- [追加前スナップショット](resources/baseline.json)、[追加機能版](resources/features.json)、[現在版](resources/current.json)
- [機能追加の差分](resources/features-comparison.json)、[測定表示込みの差分](resources/comparison.json)
- [測定SDK設定](resources/sdkconfig.measurement)、[旧版をビルドするための宣言追加](resources/baseline-compile.patch)
- [測定ツール](../tools/resource_report.py)、[テスト](../tests/test_resource_report.py)

旧版には現在のコンパイラで必要となる `sh_streq` の前方宣言を1行追加しました。処理内容は変更していません。SDK設定のSHA256は両版で一致しています。P4 360 MHz、L2キャッシュ128 KiB、PSRAM 200 MHzです。新しいCMake機能フラグはJSONに記録し、旧版で存在しない項目はnullです。

JSONにはBIN・ELF・SDK設定のハッシュを保存しています。`revision_declared` は作業者によるソース版のラベルです。成果物の `artifact_project_version` はCMake生成時の古い版名を含む場合があり、ソースと成果物の一致を暗号学的に証明するものではありません。実行時ログと書き込みイメージの対応も、書き込み・起動手順による記録です。

## 今後の機能追加で確認する項目

1. 同じSDK、コンパイラ、最適化、接続機器で追加前後をビルドし、BIN・静的RAM・ヒープ予約を記録する。
2. `mem --json` を待機時、機能動作中、終了後に取得する。ネットワーク、画面、接続機器、同時実行アプリも記録する。
3. 使用中・空き・最大連続空き・確保失敗・ピークを確認する。ピークは起動後累積なので、比較時には同じ起動手順を使う。
4. 開始と終了を繰り返し、終了後に確保が残り続けないか確認する。
5. 必要なら機能無効ビルドも作り、その機能の限界費用を調べる。共有部分があるので機能別差分の合計は全体差分と一致するとは限らない。

ESP-IDFのPython環境での例です。SDK設定テンプレートは別のローカル設定ファイルへコピーして使い、追跡中のテンプレートをビルドで書き換えないようにします。

```powershell
python tools/resource_report.py snapshot --build-dir build-tab5-size --label before --revision COMMIT --output before.json
# 同じ条件で機能追加後をビルド。mem --json の出力を runtime.txt に保存。
python tools/resource_report.py snapshot --build-dir build-tab5-size --label after --revision COMMIT --runtime-log runtime.txt --workload "idle shell, same peripherals" --output after.json
python tools/resource_report.py compare before.json after.json --output comparison.json
```

必要に応じて比較に `--max-flash-growth 32768 --max-static-ram-growth 4096` のような許容増分を指定できます。値はバイトで、超過時は終了コード2になります。全機能共通の固定予算として採用したものではありません。
