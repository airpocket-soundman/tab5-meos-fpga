# 第3版監査指摘の修正完了記録

日付：2026-10-24。修正先：[tab5-tang-primer20k-carrier main 851adf4](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier/tree/851adf4)。
元の監査対象は91dc3b4。監査本文と当時のデータは履歴として保持する。

| 指摘 | 修正コミット | 確認結果 |
|---|---|---|
| R3-01 配置済みESDランド検査 | 30177ca | パッド実体の寸法・位置・向き・形状・層、マスク／ペースト、部品／端子欠落を検査。正常、回転、裏面を含む13ケース成功 |
| R3-02 配線幅・分類 | f17a484 | 全長を区間で照合し、幅不一致・二重分類・stale・未分類・ビア形状変更を検出。11ケース成功 |
| R3-03 D2説明 | 7f0a710 | D2=赤／D1=緑／D0=青へ訂正し、符号語・制御期間・P/N入替のRTL試験要求を登録 |
| 検証記録・公開追記 | 851adf4 | 24ケースの結果、再現手順、55ファイルのハッシュ照合を公開 |

正常として受理6ケース、不正として拒否18ケース。形状の回転・裏面試験は銅導通を保証する試験とは区別する。検査がクラッシュしたり結果JSONを生成しない場合を、期待した拒否として数えない。

- [実装・再現手順](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier/blob/851adf4/docs/HDMI_R3_検査修正_2026-10-24.md)
- [ESD回帰結果](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier/blob/851adf4/kicad/sim/regression_esd.json)
- [分類回帰結果](https://github.com/airpocket-soundman/tab5-tang-primer20k-carrier/blob/851adf4/kicad/sim/regression_classification.json)
- [公開追記](https://airpocket-soundman.github.io/tab5-tang-primer20k-carrier/hdmi-audit-checks.html)

回路・PCB・チャネルモデル／数値結果とMeOSファームウェアは変更していない。D2について完了したのは文書訂正とRTL試験要求であり、送信RTL・実機表示は未実装／未検証。3D較正、PARLIO、電源断・逆給電、最終ピン表・GPIO所有権の検証も継続課題。

キャリアの元のローカルチェックアウトには未コミットのREADMEとAGENTS.mdがあったため、上書きしていない。作業は `D:/github/airpocket-soundman/tab5-tang-primer20k-carrier-r3-checks` の `codex/hdmi-r3-checks` で行い、確認後にmainへfast-forwardでプッシュした。
