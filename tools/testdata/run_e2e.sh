#!/usr/bin/env bash
# run_e2e.sh —— genealogy 全过程端到端测试（CLI 层）
# 数据：tools/testdata/{family_gedcom.ged, broken_gedcom.ged}
# 覆盖：建库/建档/导入/校验/查询/世系算法/历法/导出往返/树渲染/字辈/
#       迁徙/备份恢复/灌库/规则触发/世系环鲁棒性。
# 用法：bash tools/testdata/run_e2e.sh   （在仓库根执行）
set -u -o pipefail
cd "$(dirname "$0")/../.."

PY="$(command -v python3 || command -v python)"
GENE="./build/bin/genea.exe"
OUT=".data/testdata"
PASS=0; FAIL=0

ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
check_eq() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1（期望[$2] 实际[$3]）"; fi; }
check_ge() { if [ "$2" -ge "$3" ] 2>/dev/null; then ok "$1"; else bad "$1（期望>=$3] 实际[$2]）"; fi; }
check_has() { case "$2" in *"$3"*) ok "$1";; *) bad "$1（未包含[$3]）";; esac; }

jid()     { $PY -c "import sys,json;print(json.load(sys.stdin).get('id',''))"; }
jlen()    { $PY -c "import sys,json;print(len(json.load(sys.stdin)))"; }
jval()    { $PY -c "import sys,json;print(json.load(sys.stdin)['$1'])" ; }
# 带重试：Windows 下 MSYS 管道+原生 exe 偶发丢字节（2026-10-23 曾致
# 「show 陈德明」间歇 FAIL——管道丢字节使 python 解析不到 id）
pid_of()  {
  local out i
  for i in 1 2 3; do
    out=$($GENE person list --db "$DB" --format json 2>/dev/null | $PY -c "
import sys,json
name=sys.argv[1]
for p in json.load(sys.stdin):
    if p['family_name']+p['given_name']==name: print(p['id']); break
" "$1" 2>/dev/null)
    [ -n "$out" ] && { echo "$out"; return 0; }
    sleep 0.3
  done
}
rel_gap() { $GENE person relation --db "$DB" "$1" "$2" --format json | $PY -c "import sys,json;d=json.load(sys.stdin);print('null' if d['generation_gap'] is None else d['generation_gap'])"; }
rule_counts() { $GENE io validate --db "$1" --rule "$2" --format json | $PY -c "import sys,json;d=json.load(sys.stdin)['summary'];print(d['error'],d['warning'],d['info'])"; }

rm -rf "$OUT"; mkdir -p "$OUT"
DB="$OUT/e2e.db"

echo "== 1. 建库 =="
$GENE db init --db "$DB" --format json >/dev/null 2>&1
check_eq "db init 退出码" 0 $?

echo "== 2. 宗族建档 =="
L=$($GENE lineage add --db "$DB" --name "中湘陈氏测试谱" --surname 陈 --commandery 颍川 --hall 德星堂 --format json | jid)
check_ge "lineage add 返回 ID" ${#L} 20

echo "== 3. GEDCOM 导入主谱 =="
$GENE io import gedcom tools/testdata/family_gedcom.ged --db "$DB" --lineage "$L" --format json > "$OUT/import.json" 2>&1
check_eq "import 退出码" 0 $?
check_eq "导入人物数" 11 "$($PY -c "import json;print(json.load(open('$OUT/import.json'))['imported']['persons'])")"
check_eq "导入结合数" 4  "$($PY -c "import json;print(json.load(open('$OUT/import.json'))['imported']['unions'])")"
check_ge "映射损失>=3（SOUR/OBJE 显式化）" "$($PY -c "import json;print(len(json.load(open('$OUT/import.json'))['losses']))")" 3

echo "== 4. 主谱一致性校验（应无 error） =="
$GENE io validate --db "$DB" --lineage "$L" --format json > "$OUT/validate.json" 2>&1
check_eq "validate 退出码（error=0）" 0 $?
check_eq "error 计数" 0 "$($PY -c "import json;print(json.load(open('$OUT/validate.json'))['summary']['error'])")"

echo "== 5. 人物查询 =="
OUT1=$($GENE person show --db "$DB" "$(pid_of 陈德明)" --format json 2>&1)
check_has "show 陈德明" "$OUT1" "德明"
check_eq "person list 总数" 11 "$($GENE person list --db "$DB" --format json | jlen)"

echo "== 6. 世系算法（关系/辈分） =="
check_eq "德明 vs 世昌 辈差+3（曾祖孙）" 3 "$(rel_gap "$(pid_of 陈德明)" "$(pid_of 陈世昌)")"
check_eq "文远 vs 承嗣 辈差+1（父子）"   1 "$(rel_gap "$(pid_of 陈文远)" "$(pid_of 陈承嗣)")"
check_eq "文远 vs 婉仪 辈差0（同父异母兄妹）" 0 "$(rel_gap "$(pid_of 陈文远)" "$(pid_of 陈婉仪)")"
RELTXT=$($GENE person relation --db "$DB" "$(pid_of 陈承嗣)" "$(pid_of 陈德明)" 2>&1)
check_has "承嗣 vs 德明 五服输出" "$RELTXT" "五服"

echo "== 7. 十代子孙树 =="
TREE=$($GENE person tree --db "$DB" "$(pid_of 陈德明)" --depth 4 --format json)
check_ge "树节点数>=7" "$(echo "$TREE" | jval nodes)" 7

echo "== 8. GEDCOM 导出与往返 =="
$GENE io export gedcom --db "$DB" --lineage "$L" --out "$OUT/roundtrip.ged" --submitter 测试谱师 --format json >/dev/null 2>&1
check_eq "export 退出码" 0 $?
check_ge "导出文件非空" "$(wc -c < "$OUT/roundtrip.ged")" 500
L2=$($GENE lineage add --db "$DB" --name "往返校验谱" --surname 陈 --format json | jid)
$GENE io import gedcom "$OUT/roundtrip.ged" --db "$DB" --lineage "$L2" --format json > "$OUT/roundtrip.json" 2>&1
check_eq "往返导入人物数一致" 11 "$($PY -c "import json;print(json.load(open('$OUT/roundtrip.json'))['imported']['persons'])")"

echo "== 9. 世系树渲染（DOT / Mermaid） =="
DOT=$($GENE io export tree --db "$DB" --person "$(pid_of 陈德明)" --tree-format dot --depth 3 2>&1)
check_has "DOT 含 digraph" "$DOT" "digraph"
MERMAID=$($GENE io export tree --db "$DB" --person "$(pid_of 陈德明)" --tree-format mermaid --depth 3 2>&1)
check_has "Mermaid 含 graph" "$MERMAID" "graph"

echo "== 10. 历法工具 =="
check_has "convert 1864-05-09 农历四月初四" "$($GENE date convert 1864-05-09 2>&1)" "四月初四"
check_has "convert 1864-05-09 甲子年"      "$($GENE date convert 1864-05-09 2>&1)" "甲子"
check_has "era 同治三年 = 1864"           "$($GENE date era 同治三年 2>&1)" "1864"
check_has "lunar 闰六月二十 = 2025-08-13"  "$($GENE date lunar --year 2025 --month 6 --day 20 --leap 2>&1)" "2025-08-13"
INFO=$($GENE date info 2>&1)
check_has "date info 下界 1583" "$INFO" "1583"
check_has "date info 上界 3000" "$INFO" "3000"

echo "== 11. 字辈引擎 =="
# 派语与谱中辈字名对齐：德明(1世,德)→文远/文达(2世,文)→承嗣(3世,承)→世昌(4世,世)
$GENE poem set --db "$DB" --lineage "$L" --text "德文承世，源远流长" --format json > "$OUT/poem.json" 2>&1
check_eq "poem set 辈字数" 8 "$(jlen < "$OUT/poem.json")"
$GENE poem check --db "$DB" --lineage "$L" --format json >/dev/null 2>&1
check_eq "poem check 退出码" 0 $?

echo "== 12. 迁徙登记 =="
P_FROM=$($GENE place add --db "$DB" --name 中湘 --type county --modern "湖南省湘潭县" --format json | jid)
P_TO=$($GENE place add --db "$DB" --name 渝州 --type prefecture --modern "重庆市" --format json | jid)
MIG=$($GENE migration add --db "$DB" --subject-type person --subject "$(pid_of 陈世昌)" --from "$P_FROM" --to "$P_TO" --reason 营商 --date "民国三十五年" --format json | jid)
check_ge "migration add 返回 ID" ${#MIG} 20
check_eq "migration list 计数" 1 "$($GENE migration list --db "$DB" --format json | jlen)"

echo "== 13. 事件登记 =="
$GENE event add --db "$DB" --subject-type person --subject "$(pid_of 陈世昌)" --type annotation --description "端到端测试事件" --format json >/dev/null 2>&1
check_eq "event add 退出码" 0 $?

echo "== 14. 合成谱灌库 =="
# 不传 --lineage：让 seed 自建宗族，避免随机姓名+世代序号干扰
# 第 11 节 L 宗族的派语校验（poem check 只查本宗族人物）
$GENE db seed --db "$DB" --persons 200 --format json > "$OUT/seed.json" 2>&1
check_eq "seed 退出码" 0 $?
SEED_P=$($PY -c "import json;print(json.load(open('$OUT/seed.json'))['persons'])")
check_ge "seed 实际生成>=200" "$SEED_P" 200
check_eq "灌库后总数=22+seed（11主谱+11往返+seed）" $((22+SEED_P)) "$($GENE person list --db "$DB" --limit 100000 --format json | jlen)"

echo "== 15. 备份与恢复 =="
BEFORE=$($GENE person list --db "$DB" --limit 100000 --format json | jlen)
mkdir -p "$OUT/bk"
$GENE db backup --db "$DB" --out "$OUT/bk" --format json > "$OUT/backup.json" 2>&1
check_eq "backup 退出码" 0 $?
check_eq "备份快照存在" "yes" "$([ -f "$($PY -c "import json;print(json.load(open('$OUT/backup.json'))['snapshot'])")" ] && echo yes || echo no)"
rm -f "$OUT/restored.db"
$GENE db restore --db "$OUT/restored.db" --from "$OUT/bk" --format json >/dev/null 2>&1
check_eq "restore 退出码" 0 $?
check_eq "恢复后人物数一致" "$BEFORE" "$($GENE person list --db "$OUT/restored.db" --limit 100000 --format json | jlen)"
# 目标已存在未 force 应拒绝（退出码 4）
$GENE db restore --db "$OUT/restored.db" --from "$OUT/bk" >/dev/null 2>&1
check_eq "restore 拒绝覆盖（exit 4）" 4 $?

echo "== 16. 脏数据导入与校验规则命中 =="
DB2="$OUT/broken.db"
$GENE db init --db "$DB2" >/dev/null 2>&1
L3=$($GENE lineage add --db "$DB2" --name "脏数据谱" --surname 陈 --format json | jid)
timeout 60 $GENE io import gedcom tools/testdata/broken_gedcom.ged --db "$DB2" --lineage "$L3" > "$OUT/broken_import.txt" 2>&1
BROKEN_IMPORT_RC=$?
check_eq "broken 导入未挂起（60s 超时保护）" "ok" $([ $BROKEN_IMPORT_RC -eq 124 ] && echo hung || echo ok)
if [ $BROKEN_IMPORT_RC -eq 0 ]; then
  C=$(rule_counts "$DB2" R1);  set -- $C; check_ge "R1 出生晚于卒年 error"  $1 1
  C=$(rule_counts "$DB2" R2);  set -- $C; check_ge "R2 父子年龄差<12 error"  $1 1
  C=$(rule_counts "$DB2" R3);  set -- $C; check_ge "R3 父超龄生子 warning" $2 1
  C=$(rule_counts "$DB2" R5);  set -- $C; check_ge "R5 疑似重复人物 warning" $2 1
  C=$(rule_counts "$DB2" R12); set -- $C; check_ge "R12 日期未解析 info"     $3 1
  C=$(rule_counts "$DB2" R13); set -- $C; check_ge "R13 葬早于卒 warning"    $2 1
  $GENE io validate --db "$DB2" >/dev/null 2>&1
  check_eq "broken 全量 validate 退出码 1" 1 $?
else
  bad "broken 导入失败或被中止（rc=$BROKEN_IMPORT_RC，见 $OUT/broken_import.txt）"
fi

echo "== 16b. 世系环导入（应明确拒绝，非挂起非误导报错） =="
DB4="$OUT/cycle.db"
$GENE db init --db "$DB4" >/dev/null 2>&1
L4=$($GENE lineage add --db "$DB4" --name "环测试谱" --surname 陈 --format json | jid)
timeout 60 $GENE io import gedcom tools/testdata/cycle_gedcom.ged --db "$DB4" --lineage "$L4" > "$OUT/cycle_import.txt" 2>&1
CYCLE_RC=$?
check_eq "环导入未挂起" "ok" $([ $CYCLE_RC -eq 124 ] && echo hung || echo ok)
check_eq "环导入被拒绝（InvalidArgument=2）" 2 $CYCLE_RC
check_has "报错点名世系环" "$(cat "$OUT/cycle_import.txt")" "世系环"
check_has "报错带出环上人名" "$(cat "$OUT/cycle_import.txt")" "成环"

echo "== 17. CLI 规则触发集 =="
bash tools/testdata/build_cases.sh > "$OUT/cases_build.txt" 2>&1
check_eq "build_cases 退出码" 0 $?
DB3="$OUT/cases.db"
if [ -f "$DB3" ]; then
  C=$(rule_counts "$DB3" R6);  set -- $C; check_ge "R6 配偶同性别 warning" $2 1
  C=$(rule_counts "$DB3" R7);  set -- $C; check_ge "R7 父母字段不一致 error" $1 1
  C=$(rule_counts "$DB3" R9);  set -- $C; check_ge "R9 性别未登记 info"     $3 1
  C=$(rule_counts "$DB3" R10); set -- $C; check_ge "R10 生年缺失 info"      $3 1
  C=$(rule_counts "$DB3" R14); set -- $C; check_ge "R14 排行断层 warning"   $2 1
fi

echo
echo "=========================================="
echo "端到端结果: PASS=$PASS FAIL=$FAIL"
echo "=========================================="
[ $FAIL -eq 0 ]
