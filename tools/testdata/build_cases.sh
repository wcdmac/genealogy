#!/usr/bin/env bash
# build_cases.sh —— 用 CLI 构建「校验规则触发集」cases.db
# 每条规则对应一组最小数据；用 genea io validate --rule <R> 单独断言命中。
# 用法：bash tools/testdata/build_cases.sh   （在仓库根执行）
set -u -o pipefail
cd "$(dirname "$0")/../.."

PY="$(command -v python3 || command -v python)"
GENE="./build/bin/genea.exe"
OUT=".data/testdata"
DB="$OUT/cases.db"
# 只清自己的库，绝不 rm 整个 OUT——run_e2e.sh 的产物也在同一目录
mkdir -p "$OUT"
rm -f "$DB" "$DB-wal" "$DB-shm"

jid() { $PY -c "import sys,json;print(json.load(sys.stdin).get('id',''))"; }

$GENE db init --db "$DB" >/dev/null || { echo "建库失败"; exit 1; }


# 基准家庭：F1/M1 已婚（避免干扰 R7）
F1=$($GENE person add --db "$DB" --family-name 陈 --given-name 大洪 --gender male --birth 1850 --generation 1 --format json | jid)
M1=$($GENE person add --db "$DB" --family-name 王 --given-name 大娘子 --gender female --birth 1852 --generation 1 --format json | jid)
$GENE union add --db "$DB" --type marriage --member "$F1:husband,$M1:wife" --format json >/dev/null

# R14 排行断层：同胞 1、2、4（跳 3）
$GENE person add --db "$DB" --family-name 陈 --given-name 甲 --gender male --birth 1875 --birth-order 1 --generation 2 --father "$F1" --mother "$M1" --format json >/dev/null
$GENE person add --db "$DB" --family-name 陈 --given-name 乙 --gender male --birth 1877 --birth-order 2 --generation 2 --father "$F1" --mother "$M1" --format json >/dev/null
$GENE person add --db "$DB" --family-name 陈 --given-name 丁 --gender male --birth 1881 --birth-order 4 --generation 2 --father "$F1" --mother "$M1" --format json >/dev/null

# R6 配偶性别相同（两名男性婚姻结合）
P1=$($GENE person add --db "$DB" --family-name 张 --given-name 龙 --gender male --birth 1880 --format json | jid)
P2=$($GENE person add --db "$DB" --family-name 赵 --given-name 虎 --gender male --birth 1881 --format json | jid)
$GENE union add --db "$DB" --type marriage --member "$P1:husband,$P2:husband" --format json >/dev/null

# R7 父母冗余字段无对应结合 + R8 有父母无世代序号
F2=$($GENE person add --db "$DB" --family-name 周 --given-name 父 --gender male --birth 1855 --format json | jid)
M2=$($GENE person add --db "$DB" --family-name 吴 --given-name 母 --gender female --birth 1858 --format json | jid)
$GENE person add --db "$DB" --family-name 周 --given-name 子 --gender male --birth 1885 --father "$F2" --mother "$M2" --format json >/dev/null

# R9 性别未登记（缺省 unknown）
$GENE person add --db "$DB" --family-name 郑 --given-name 无性别 --birth 1890 --format json >/dev/null

# R10 生年缺失
$GENE person add --db "$DB" --family-name 冯 --given-name 无生年 --gender male --format json >/dev/null

echo "cases.db 构建完成: $DB"
echo "单谱单库：book 行由 db init 建立"
