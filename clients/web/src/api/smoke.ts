// 读写闭环冒烟（M2 验收：「TS 客户端由生成器产出并可跑通读写」）。
//
// 做什么：自己拉起一个临时库上的 genealogy-server，用 client.ts 走一遍
// 「宗族建档 → 三代人 → 婚姻 → 事件 → 检索 → 算法 → 导出 → 删除」，
// 关键路径（409 乐观锁、删除后 404、五服/称谓）逐条断言。
//
// 跑法：npm run smoke（依赖 build/bin/genealogy-server.exe 已构建）。
// 退出码：0 全部通过；1 有断言失败；2 环境问题（server 起不来等）。
//
// 为什么用 Node 的 type stripping 直接 import client.ts：冒烟要验证的
// 就是这份封装本身，另抄一遍原生 fetch 等于验证了两样不同的东西。
// Node >= 22.18 默认启用 erasable-syntax 类型擦除，client.ts 只含
// 可擦除类型（无 enum/namespace/参数属性），无需 tsc 预编译。

import { spawn} from "node:child_process";
import { existsSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

import { ApiError, GenealogyClient, paginate } from "./client.ts";

const here = fileURLToPath(new URL(".", import.meta.url));
// here = clients/web/src/api/，上四级才是仓库根（clients/web/src/api → … → genealogy）
const serverBin = join(here, "..", "..", "..", "..", "build", "bin", "genealogy-server.exe");
// Drogon 等 DLL 在 vcpkg installed bin；CI 上 vcpkg 不在 D 盘，
// 用 VCPKG_ROOT 覆盖（CI 步骤里 export PATH 亦可，双保险）。
const vcpkgBin = process.env.VCPKG_ROOT
  ? join(process.env.VCPKG_ROOT, "installed", "x64-windows", "bin")
  : "D:\\vcpkg\\installed\\x64-windows\\bin";

let failures = 0;

function check(label: string, condition: boolean, detail?: unknown): void {
  if (condition) {
    console.log(`  ok  ${label}`);
  } else {
    failures += 1;
    console.error(`  FAIL ${label}${detail === undefined ? "" : ` — ${String(detail)}`}`);
  }
}

async function expectApiError(
  label: string,
  fn: () => Promise<unknown>,
  status: number,
): Promise<void> {
  try {
    await fn();
    check(label, false, `期望 ${status} 但调用成功了`);
  } catch (error) {
    if (error instanceof ApiError) {
      check(label, error.status === status, `实际 ${error.status} (${error.message})`);
    } else {
      check(label, false, `非 ApiError: ${String(error)}`);
    }
  }
}

/** 等 server 就绪（health 探活），超时判失败。 */
async function waitReady(base: string, timeoutMs = 15_000): Promise<boolean> {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(`${base}/health`);
      if (response.ok) return true;
    } catch {
      // 端口还没起来，继续等
    }
    await new Promise((resolve) => setTimeout(resolve, 200));
  }
  return false;
}

async function main(): Promise<number> {
  if (!existsSync(serverBin)) {
    console.error(`[smoke] 找不到 ${serverBin}，先构建 server`);
    return 2;
  }

  const port = 18080 + Math.floor(Math.random() * 2000);
  const base = `http://127.0.0.1:${port}/api/v1`;
  const dbPath = join(tmpdir(), `genealogy-smoke-${process.pid}.db`);

  console.log(`[smoke] server: ${serverBin}\n[smoke] db: ${dbPath}\n[smoke] port: ${port}`);
  const server = spawn(serverBin, ["--port", String(port), "--db", dbPath, "--threads", "2"], {
    stdio: ["ignore", "pipe", "pipe"],
    env: { ...process.env, PATH: `${vcpkgBin};${process.env.PATH ?? ""}` },
  });
  server.stderr.on("data", (chunk: Buffer) => process.stderr.write(`[server] ${chunk}`));

  try {
    if (!(await waitReady(base))) {
      console.error("[smoke] server 未就绪");
      return 2;
    }

    const client = new GenealogyClient(base);

    // ---- 鉴权 ----
    const role = await client.login("smoke", "smoke");
    check("login 返回 steward", role === "steward", role);
    const me = await client.me();
    // 本地免鉴权模式下 server 恒以 local steward 身份响应（不解析令牌），
    // 这是设计行为（main.cpp 注释：只有本机能连，开箱即用）。
    check("me 本地身份为 local", me.username === "local", me.username);

    // ---- 宗族 ----
    const lineage = await client.createLineage({
      name: "冒烟测试宗族",
      surname: "测试",
    });
    check("建档宗族", typeof lineage.id === "string" && lineage.id.length > 0, lineage.id);

    const stats0 = await client.getStats();
    check("统计端点可达", typeof stats0.person_count === "number", stats0);

    // ---- 三代人 ----
    const grandpa = await client.createPerson({
      lineage_id: lineage.id,
      family_name: "测",
      given_name: "始祖",
      gender: "male",
      generation_no: 1,
    });
    const father = await client.createPerson({
      lineage_id: lineage.id,
      family_name: "测",
      given_name: "二世",
      gender: "male",
      father_id: grandpa.id,
      generation_no: 2,
    });
    const son = await client.createPerson({
      lineage_id: lineage.id,
      family_name: "测",
      given_name: "三世",
      gender: "male",
      father_id: father.id,
      generation_no: 3,
    });
    check("三代建档", Boolean(grandpa.id && father.id && son.id));
    check("建档返回 rev", typeof grandpa.rev === "number", grandpa.rev);

    // ---- 详情 + 乐观锁 ----
    const detail = await client.getPerson(son.id, ["events", "unions"]);
    check("人物详情可读", detail.id === son.id);
    const renamed = await client.updatePerson(
      son.id,
      { courtesy_name: "三世公" },
      son.rev,
    );
    check("PATCH 更新字", renamed.courtesy_name === "三世公", renamed.courtesy_name);
    await expectApiError(
      "旧 rev 再更新得 409",
      () => client.updatePerson(son.id, { courtesy_name: "又改" }, son.rev),
      409,
    );
    const fresh = await client.getPerson(son.id);
    check("重新 get 拿到新 rev", fresh.rev !== son.rev, `${son.rev} -> ${fresh.rev}`);

    // ---- 婚姻 + 事件 ----
    const wife = await client.createPerson({
      lineage_id: lineage.id,
      family_name: "配",
      given_name: "氏",
      gender: "female",
    });
    const union = await client.createUnion({
      type: "marriage",
      members: [
        { person_id: grandpa.id, role: "husband" },
        { person_id: wife.id, role: "wife" },
      ],
    });
    check("建立婚姻", typeof union.id === "string", union.id);

    const event = await client.createEvent({
      subject_type: "person",
      subject_id: grandpa.id,
      type: "birth",
      description: "冒烟事件",
    });
    check("登记事件", typeof event.id === "string", event.id);

    // ---- 世系算法 ----
    const mourning = await client.getMourningDegree(son.id, grandpa.id);
    // 孙为祖父母 = 齐衰不杖期 13 月（承重孙才 27，此处非承重）
    check("五服：孙→祖父 齐衰不杖期", mourning.garment === "qicui_buzhang", mourning.garment);
    check("五服期 13 月", mourning.duration_months === 13, mourning.duration_months);

    const appellation = await client.getAppellation(son.id, grandpa.id);
    check("称谓：孙称祖父", appellation.standard === "祖父", appellation.standard);

    const ancestors = await client.getPersonAncestors(son.id, 5);
    check("祖先链可达", Boolean(ancestors));
    const tree = await client.getPersonTree(grandpa.id, { mode: "full", depth: 5 });
    check("世系图可达", Boolean(tree));

    // ---- 检索 / 统计 / 校验 ----
    const searchPage = await client.search({ q: "三世" });
    check("全文检索命中", (searchPage.data ?? []).some((p) => p.id === son.id));
    const listPage = await client.listPersons({ lineage_id: lineage.id });
    check("人物列表含始祖", (listPage.data ?? []).some((p) => p.id === grandpa.id));

    const validation = await client.runValidation();
    check("校验端点返回 issue 数组", Array.isArray(validation.issues));

    // ---- 导出 ----
    const gedcom = await client.exportGedcom(lineage.id);
    // GEDCOM 记录用 @I001@ 式局部 ID；NAME 段为「名 /姓/」+ GIVN/SURN 子标签
    check("GEDCOM 导出含始祖", gedcom.includes("GIVN 始祖") && gedcom.includes("SURN 测"), gedcom.slice(0, 80));
    const dot = await client.exportTree(grandpa.id, "dot");
    check("DOT 导出是 digraph", dot.includes("digraph"), dot.slice(0, 40));

    // ---- 删除 + 404 ----
    await client.deletePerson(son.id);
    await expectApiError("删除后 get 得 404", () => client.getPerson(son.id), 404);

    // ---- SSE 变更流（M5-3）----
    const notices: Array<{ type?: string; subject_id?: string }> = [];
    const unsubscribe = client.subscribe((notice) =>
      notices.push(notice as { type?: string; subject_id?: string }),
    );
    const ssePerson = await client.createPerson({
      lineage_id: lineage.id,
      family_name: "测",
      given_name: "SSE探针",
      gender: "male",
    });
    // 写操作应实时推到已订阅的连接（本地总线同步广播）
    const sseDeadline = Date.now() + 3000;
    while (notices.length === 0 && Date.now() < sseDeadline) {
      await new Promise((resolve) => setTimeout(resolve, 100));
    }
    check(
      "SSE 实时收到 person.created",
      notices.some(
        (notice) =>
          notice.type === "person.created" && notice.subject_id === ssePerson.id,
      ),
      JSON.stringify(notices),
    );
    unsubscribe();
    await client.deletePerson(ssePerson.id);

    // ---- 迁徙（M5-4）----
    const fromPlace = await client.createPlace({
      name: "冒烟故里",
      lat: 27.83,
      lng: 112.94,
    });
    const toPlace = await client.createPlace({
      name: "冒烟新邑",
      lat: 29.56,
      lng: 106.55,
    });
    const migration = await client.createMigration({
      subject_type: "person",
      subject_id: grandpa.id,
      from_place_id: fromPlace.id,
      to_place_id: toPlace.id,
      reason: "营商",
    });
    check("迁徙登记", typeof migration.id === "string", migration.id);
    const migrationPage = await client.listMigrations({
      subject_id: grandpa.id,
    });
    check(
      "迁徙按主体可查",
      (migrationPage.data ?? []).some((row) => row.id === migration.id),
    );
    await expectApiError(
      "缺目的地迁徙被拒（400）",
      () =>
        client.createMigration({
          subject_type: "person",
          subject_id: grandpa.id,
          to_place_id: "",
        }),
      400,
    );

    // ---- 文档（M7.4）：建档 → 正文检索命中 → merge PATCH 保 content → 删 ----
    const doc = await client.createDocument({
      lineage_id: lineage.id,
      type: "preface",
      title: "冒烟谱序",
      content: "我族建德星堂义庄，凡族人谨守家训。",
      seq: 1,
    });
    check("文档建档", typeof doc.id === "string", doc.id);
    const docHits = await client.search({ q: "义庄", scope: "document" });
    check(
      "文档正文检索命中（trigram）",
      (docHits.data ?? []).some((row) => row.id === doc.id),
    );
    const docPatched = await client.updateDocument(doc.id, {
      lineage_id: lineage.id,
      type: "preface",
      title: "冒烟谱序·改",
    });
    check(
      "文档 PATCH merge 保留正文",
      docPatched.title === "冒烟谱序·改" && docPatched.content !== undefined,
    );
    const docSubHits = await client.search({ q: "堂义庄", scope: "document" });
    check(
      "文档子串检索命中",
      (docSubHits.data ?? []).some((row) => row.id === doc.id),
    );
    await client.deleteDocument(doc.id);
    await expectApiError("删除后 get 得 404", () => client.getDocument(doc.id), 404);

    // ---- 分页迭代器 ----
    let visited = 0;
    for await (const _person of paginate((cursor) =>
      client.listPersons({ lineage_id: lineage.id, cursor, limit: 2 }),
    )) {
      visited += 1;
    }
    check("分页迭代器遍历", visited >= 3, `visited=${visited}`);

    console.log(failures === 0 ? "\n[smoke] 全部通过" : `\n[smoke] ${failures} 项失败`);
    return failures === 0 ? 0 : 1;
  } finally {
    server.kill();
    // 临时库连同 -wal/-shm 一起清
    for (const suffix of ["", "-wal", "-shm"]) {
      try {
        rmSync(dbPath + suffix, { force: true });
      } catch {
        // 清不掉不致命
      }
    }
  }
}

main().then((code) => process.exit(code));
