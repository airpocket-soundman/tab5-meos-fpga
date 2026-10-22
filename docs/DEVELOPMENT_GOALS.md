# Tab5 MeOS FPGA — 開発目的と開始記録

更新日：2026-10-23

## 目的と範囲

Tang Primer 20K側の128MB DDR3をTab5から活用するMeOS版を開発する。大容量データの分割処理、明示的な退避・復元、演算用バッファを主な研究対象とし、DDR3をVRAMとして利用するFPGA GPU構成も検討する。

Linux版とMeOS版を別リポジトリで管理し、通信プロトコル、データ照合、評価条件を整合させる。OSごとのAPI・メモリ管理・スケジューラ・DMA対応は個別に調査する。Linux用ドライバをそのまま利用できるとは扱わない。

FPGA側DDR3は明示的に転送する外部データ領域であり、通常のCPUポインタで参照できる主記憶や透過的なmalloc領域とは扱わない。GPU用と外部RAM用の同時搭載は初期の要件とせず、資源・帯域の評価後に判断する。

## 上流と開始状態

- フォーク元：[shi3z/MeOS](https://github.com/shi3z/MeOS)。開始コミット：`a6a9f234e509737f9fe84fe13468a8ec78b60fc9`。
- このフォーク：[airpocket-soundman/tab5-meos-fpga](https://github.com/airpocket-soundman/tab5-meos-fpga)。
- ローカル配置：`D:\github\airpocket-soundman\tab5-meos-fpga`。
- `origin` はこのフォーク、`upstream` は上流MeOS。
- フォーク作成、ローカル取得、リモート設定、資料とビルド入口の確認まで実施。独自機能の実装、ビルド、実機起動、FPGA通信、性能比較は未実施。
- 既存READMEの動作確認は上流の報告であり、このフォークでの検証結果ではない。

## 関連リポジトリ

| リポジトリ | 役割 |
| --- | --- |
| [tab5-linux-fpga](https://github.com/airpocket-soundman/tab5-linux-fpga) | Linux版OS・通信ドライバ。RAM活用とGPU構成の[検討資料](https://github.com/airpocket-soundman/tab5-linux-fpga/blob/main/docs/RAM_GPU_PLAN.md)を参照 |
| [esp32p4-linux-fpga-memory](https://github.com/airpocket-soundman/esp32p4-linux-fpga-memory) | 既存の外部メモリ研究計画・FPGA回路・評価側の起点 |
| [tab5-tang-primer20k-carrier](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier) | 共通キャリア・配線・電源仕様の起点 |
| [tab5-fpga-gpu](https://github.com/airpocket-soundman/tab5-fpga-gpu) | GPU回路・描画仕様・参照実装と評価 |

## 次の作業

1. `tools/build.sh`、`tools/env.sh`、Tab5固有ドライバを調べ、再現可能なビルド環境を準備する。上流のTab5向け環境指定はESP-IDF v5.4.4。実際に使用した版と成功結果を別途記録する。
2. 上流の入口 `sh tools/build.sh tab5 build` でビルドし、成果物のハッシュを保存する。書き込みは今回の作業に含めない。
3. 既存ファームウェアとC6の状態を確認した上で、実機検証の範囲・復元手順を定める。
4. キャリア共通仕様を照合し、GPIO競合、PARLIO、DMA、割り込み、キャッシュ同期、MeOSのタスクとの協調を調べる。
5. 低速通信とDDR3全域照合から、小容量バッファによる分割処理・退避復元へ進む。Linux版とは同じ入力・データ量・精度・転送条件で比較する。

## 取り扱い

上流の履歴、著作権表示、第三者コードのライセンスを維持する。ルートのライセンスファイルは開始時点で見当たらず、GitHub APIも未分類。再配布条件が必要な段階で上流の許諾を確認し、このフォーク独自のライセンスを上流全体へ付与しない。

作業終了時は対象コミット、環境、検証内容と結果、未解決事項、次の作業を記録する。上流報告と独自検証を区別し、認証情報や生ログを公開しない。
