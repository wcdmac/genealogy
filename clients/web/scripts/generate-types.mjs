// 从 openapi.yaml 生成 TS 类型。
//
// 为什么用 openapi-typescript 而不是 openapi-generator：
// 后者要 JVM（CI 三个平台装 Java 成本高、体积大），且生成的客户端
// 代码量大到没法 review。openapi-typescript 只产出**类型**（纯声明、
// 零运行时），请求发送用本项目自己的薄封装（src/api/client.ts）——
// 类型来自生成器（事实源不漂移），运行时几十行可控可读。
//
// 用法：npm run generate（CI 里每次构建都跑，产物入库前的校验见
// tools/validate_openapi.py——spec 坏了这里立刻红）。

import { execFileSync } from "node:child_process";
import { mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const here = dirname(fileURLToPath(import.meta.url));
const repoRoot = join(here, "..", "..", "..");
const spec = join(repoRoot, "shared", "openapi", "openapi.yaml");
const out = join(here, "..", "src", "api", "types.ts");

mkdirSync(dirname(out), { recursive: true });

// 直接调包的 JS 入口（bin/cli.js），不走 node_modules/.bin 的 .cmd：
// Node 18.20+ 起 spawnSync .cmd 需 shell:true（CVE-2024-27980 修复），
// 而引 shell 只会把转义问题引进来。用当前 node 进程本身最干净。
const cli = join(here, "..", "node_modules", "openapi-typescript", "bin", "cli.js");

execFileSync(process.execPath, [cli, spec, "-o", out], { stdio: "inherit" });

// 生成产物是纯类型声明；预置一行头注释说明「勿手改」
const banner = [
  "// 本文件由 openapi-typescript 从 shared/openapi/openapi.yaml 生成，勿手改。",
  "// 改类型请改规范（spec 是唯一事实源），然后 npm run generate。",
  "",
].join("\n");
writeFileSync(out, banner + readFileSync(out, "utf8"));

console.log("[generate] types.ts written to", out);
