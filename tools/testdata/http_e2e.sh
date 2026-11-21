#!/usr/bin/env bash
# http_e2e.sh —— REST 服务端到端（Drogon，免鉴权本地模式）
# 自建小库（导入主谱）→ 起 server → curl 断言。
# 用法：bash tools/testdata/http_e2e.sh   （在仓库根执行）
#
# 状态码一律从响应头首行解析，不用 curl -w：Git Bash 自带的是 cygwin
# 构建的 curl，其 -w '%{http_code}' 会丢花括号（2026-10-23 实测）；
# Windows 原生 curl 的 -w 正常但默认走系统代理，连 127.0.0.1 会 502。
set -u -o pipefail
cd "$(dirname "$0")/../.."

PY="$(command -v python3 || command -v python)"
GENE="./build/bin/genea.exe"
SERVER="./build/bin/genealogy-server.exe"
OUT=".data/testdata/http"
PORT=18099
BASE="http://127.0.0.1:$PORT/api/v1"
PASS=0; FAIL=0

ok()  { PASS=$((PASS+1)); echo "  PASS  $1"; }
bad() { FAIL=$((FAIL+1)); echo "  FAIL  $1"; }
check_eq() { if [ "$2" = "$3" ]; then ok "$1"; else bad "$1（期望[$2] 实际[$3]）"; fi; }
check_ge() { if [ "$2" -ge "$3" ] 2>/dev/null; then ok "$1"; else bad "$1（期望>=$3] 实际[$2]）"; fi; }
check_has() { case "$2" in *"$3"*) ok "$1";; *) bad "$1（未包含[$3]）";; esac; }

# 带 body 的请求：body 原样输出（调用方重定向到文件），状态码入 CODE
req() {
  curl -s -D "$OUT/.hdr" "$@"
  CODE=$(head -1 "$OUT/.hdr" | tr -d '\r' | awk '{print $2}')
}
# 只要状态码
code_only() { curl -s -o /dev/null -D - "$@" | head -1 | tr -d '\r' | awk '{print $2}'; }

rm -rf "$OUT"; mkdir -p "$OUT"
DB="$OUT/http.db"

echo "== 准备数据 =="
$GENE db init --db "$DB" >/dev/null 2>&1
L=$($GENE lineage add --db "$DB" --name "HTTP测试谱" --surname 陈 --format json | $PY -c "import sys,json;print(json.load(sys.stdin)['id'])")
$GENE io import gedcom tools/testdata/family_gedcom.ged --db "$DB" --lineage "$L" >/dev/null 2>&1
FOUNDER=$($GENE person list --db "$DB" --format json | $PY -c "
import sys,json
for p in json.load(sys.stdin):
    if p['family_name']+p['given_name']=='陈德明': print(p['id']); break")

echo "== 启动 server（端口 $PORT） =="
$SERVER --db "$DB" --port $PORT > "$OUT/server.log" 2>&1 &
SRV=$!
trap 'kill $SRV 2>/dev/null; taskkill //F //PID $SRV 2>/dev/null' EXIT
sleep 2
if ! kill -0 $SRV 2>/dev/null; then bad "server 启动失败（见 $OUT/server.log）"; cat "$OUT/server.log"; exit 1; fi
ok "server 进程存活"

echo "== 1. health / openapi =="
check_eq "GET /health 200" 200 "$(code_only "$BASE/health")"
req "$BASE/health" > "$OUT/health.json"
check_eq "GET /openapi.yaml 200" 200 "$(code_only "http://127.0.0.1:$PORT/openapi.yaml")"
req "http://127.0.0.1:$PORT/openapi.yaml" > "$OUT/openapi.yaml"
check_has "openapi.yaml 内容" "$(head -c 200 "$OUT/openapi.yaml")" "openapi"

echo "== 2. 人物 CRUD =="
# 含中文的 body 必须 UTF-8 落盘后 --data-binary 发送：Windows 原生 curl
# 会把 argv 从 UTF-16 转成系统 ANSI 码页（本机 GBK），内联中文到服务端
# 就成了非法 UTF-8——server 的 body_guard 按 RFC 8259 正确拒收 400。
printf '%s' "{\"lineage_id\":\"$L\",\"family_name\":\"陈\",\"given_name\":\"HTTP测试\",\"gender\":\"male\"}" > "$OUT/create_body.json"
req -X POST "$BASE/persons" -H 'Content-Type: application/json' --data-binary @"$OUT/create_body.json" > "$OUT/create.json"
check_eq "POST /persons 201" 201 "$CODE"
NEWID=$($PY -c "import json;print(json.load(open('$OUT/create.json'))['id'])")

echo "== 3. ETag / If-Match 乐观锁 =="
curl -s -D "$OUT/head.txt" "$BASE/persons/$NEWID" -o "$OUT/get.json" >/dev/null
check_eq "GET /persons/{id} 200" 200 "$(code_only "$BASE/persons/$NEWID")"
ETAG=$(grep -i '^etag:' "$OUT/head.txt" | tr -d '\r' | awk '{print $2}')
check_ge "ETag 头存在" ${#ETAG} 1
printf '%s' '{"courtesy_name":"盲写"}' > "$OUT/p_body.json"
req -X PATCH "$BASE/persons/$NEWID" -H 'Content-Type: application/json' --data-binary @"$OUT/p_body.json" > "$OUT/p428.json"
check_eq "PATCH 无 If-Match → 428" 428 "$CODE"
check_has "428 是 problem+json" "$(cat "$OUT/p428.json")" "problem"
printf '%s' '{"courtesy_name":"旧版本"}' > "$OUT/p_body.json"
req -X PATCH "$BASE/persons/$NEWID" -H 'Content-Type: application/json' -H 'If-Match: 999999' --data-binary @"$OUT/p_body.json" > "$OUT/p409.json"
check_eq "PATCH 错误 rev → 409" 409 "$CODE"
printf '%s' '{"courtesy_name":"HTTP字"}' > "$OUT/p_body.json"
req -X PATCH "$BASE/persons/$NEWID" -H 'Content-Type: application/json' -H "If-Match: $ETAG" --data-binary @"$OUT/p_body.json" > "$OUT/p200.json"
check_eq "PATCH 正确 rev → 200" 200 "$CODE"
check_has "PATCH 生效（courtesy_name）" "$(cat "$OUT/p200.json")" "HTTP字"

echo "== 4. 检索（FTS） =="
req "$BASE/persons?q=%E6%96%87%E8%BF%9C" > "$OUT/q.json"
check_eq "GET /persons?q=文远 200" 200 "$CODE"
# 分页 envelope 的数组 key 是 data（page_envelope），不是 items
check_ge "FTS 命中>=1" "$($PY -c "import json;print(len(json.load(open('$OUT/q.json')).get('data',[])))")" 1
req "$BASE/search?q=%E9%99%88" > "$OUT/s.json"
check_eq "GET /search?q=陈 200" 200 "$CODE"
check_ge "search 命中>=1" "$($PY -c "import json;print(len(json.load(open('$OUT/s.json')).get('data',[])))")" 1

echo "== 5. 世系算法端点 =="
DESC=$($GENE person list --db "$DB" --format json | $PY -c "
import sys,json
for p in json.load(sys.stdin):
    if p['family_name']+p['given_name']=='陈世昌': print(p['id']); break")
req "$BASE/persons/$DESC/ancestors?depth=3" > "$OUT/anc.json"
check_eq "GET /ancestors 200" 200 "$CODE"
check_ge "祖先代数>=3" "$($PY -c "import json;print(len(json.load(open('$OUT/anc.json'))['generations']))")" 3
req "$BASE/persons/$FOUNDER/descendants?depth=3" > "$OUT/desc.json"
check_eq "GET /descendants 200" 200 "$CODE"
req "$BASE/persons/$FOUNDER/tree?mode=five_generation" > "$OUT/tree.json"
check_eq "GET /tree?mode=five_generation 200" 200 "$CODE"
req "$BASE/persons/$FOUNDER/mourning-degree?to=$DESC" > "$OUT/mourn.json"
check_eq "GET /mourning-degree 200" 200 "$CODE"
check_has "五服输出 garment" "$(cat "$OUT/mourn.json")" "garment"
req "$BASE/persons/$FOUNDER/appellation?to=$DESC" > "$OUT/app.json"
check_eq "GET /appellation 200" 200 "$CODE"
check_has "称谓输出 standard" "$(cat "$OUT/app.json")" "standard"

echo "== 6. 校验 / 统计 =="
req -X POST "$BASE/validate" -H 'Content-Type: application/json' -d '{"rule_ids":["R1","R5"]}' > "$OUT/val.json"
check_eq "POST /validate 200" 200 "$CODE"
req "$BASE/stats" > "$OUT/stats.json"
check_eq "GET /stats 200" 200 "$CODE"

echo "== 7. 错误体规范（problem+json） =="
req "$BASE/nope" > "$OUT/404.json"
CT=$(curl -s -o /dev/null -D - "$BASE/nope" | grep -i '^content-type:' | tr -d '\r')
check_has "404 content-type 为 problem+json" "$CT" "problem+json"
check_has "404 body 有 type/status" "$(cat "$OUT/404.json")" "not-found"

echo "== 8. SSE 变更流 =="
# -N 必须：curl 输出到文件时全缓冲，不关缓冲的话小帧会滞留在
# curl 进程内，kill 之前 sse.txt 里永远是空的
curl -s -N --max-time 6 "$BASE/events/stream" > "$OUT/sse.txt" 2>&1 &
SSE=$!
sleep 1
printf '%s' "{\"lineage_id\":\"$L\",\"family_name\":\"陈\",\"given_name\":\"SSE触发\",\"gender\":\"male\"}" > "$OUT/sse_body.json"
curl -s -X POST "$BASE/persons" -H 'Content-Type: application/json' --data-binary @"$OUT/sse_body.json" >/dev/null
# 轮询等 change 帧落盘再 kill：帧到达与 curl 写文件有毫秒级时序差
for _ in 1 2 3 4 5 6 7 8; do
  grep -q "change" "$OUT/sse.txt" 2>/dev/null && break
  sleep 0.5
done
kill $SSE 2>/dev/null
check_has "SSE 收到 change 帧" "$(cat "$OUT/sse.txt")" "change"

echo "== 9. 删除 =="
check_eq "DELETE /persons/{id} 204" 204 "$(code_only -X DELETE "$BASE/persons/$NEWID")"

echo "== 10. multipart 守卫与 GEDCOM 导入（复核 2.5/H1 验收门） =="
# 10a. multipart 头 + 非法 UTF-8 body 打 JSON 端点：必须 415 拒收，且
# 进程存活。此前 multipart 任意端点放行，GBK 字节直进 json::parse 是
# 进程级终止路径（2026-11-12 GBK 请求体排查记录的同款场景）。
printf '\xC4\xE3\xBA\xC3' > "$OUT/bad_utf8.bin"
check_eq "multipart+非法UTF-8 打 /persons -> 415" 415 \
  "$(code_only -X POST "$BASE/persons" -H 'Content-Type: multipart/form-data; boundary=e2e' --data-binary @"$OUT/bad_utf8.bin")"
check_eq "server 仍存活（/health 200）" 200 "$(code_only "$BASE/health")"
# 10b. 合法载体不受影响：/imports/gedcom 收 multipart 正常导入
BOUND=e2eboundary
printf -- "--%s\r\n" "$BOUND" > "$OUT/mp.f"
printf 'Content-Disposition: form-data; name="lineage_id"\r\n\r\n%s\r\n' "$L" >> "$OUT/mp.f"
printf -- "--%s\r\n" "$BOUND" >> "$OUT/mp.f"
printf 'Content-Disposition: form-data; name="file"; filename="t.ged"\r\n' >> "$OUT/mp.f"
printf 'Content-Type: application/octet-stream\r\n\r\n' >> "$OUT/mp.f"
cat tools/testdata/family_gedcom.ged >> "$OUT/mp.f"
printf -- "\r\n--%s--\r\n" "$BOUND" >> "$OUT/mp.f"
req -X POST "$BASE/imports/gedcom" -H "Content-Type: multipart/form-data; boundary=$BOUND" --data-binary @"$OUT/mp.f" > "$OUT/import_mp.json"
check_eq "multipart 导入 GEDCOM 200" 200 "$CODE"
check_has "导入结果含 imported" "$(cat "$OUT/import_mp.json")" "imported"

echo
echo "=========================================="
echo "HTTP 端到端结果: PASS=$PASS FAIL=$FAIL"
echo "=========================================="
[ $FAIL -eq 0 ]
