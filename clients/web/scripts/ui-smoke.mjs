// 浏览器端 UI 闭环验证（M5 验收：「浏览器端完成建档→关系→世系图→导出闭环」）。
//
// 与 src/api/smoke.ts 的分工：那个验证 client.ts 的 HTTP 行为；
// 本脚本验证真实浏览器里 React 应用跑得通——连接、建档、人物卡、
// 世系图 SVG、导出下载、校验视图，全部实际操作一遍。
//
// 前置：npm run build（dist/）；build/bin/genealogy-server.exe。
// 跑法：node scripts/ui-smoke.mjs（playwright 装在受管 workspace）。
// 退出码：0 全过；1 断言失败；2 环境问题。

import { spawn } from "node:child_process";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { fileURLToPath } from "node:url";

const here = fileURLToPath(new URL(".", import.meta.url));
const webRoot = join(here, "..");
const repoRoot = join(here, "..", "..", "..");
const serverBin = join(repoRoot, "build", "bin", "genealogy-server.exe");
const distDir = join(webRoot, "dist");

// playwright 是本工程 devDependency（CI 的 npm ci 会装上），
// 浏览器二进制首次跑前执行：node scripts/ui-smoke.mjs 会自动提示，
// 或手动 npx playwright install chromium。
const { chromium } = await import("playwright");

const UI_PORT = 4173;
const API_PORT = 8099;
const UI_BASE = `http://127.0.0.1:${UI_PORT}`;
const API_BASE = `http://127.0.0.1:${API_PORT}/api/v1`;

let failures = 0;
function check(label, condition, detail) {
  if (condition) {
    console.log(`  ok  ${label}`);
  } else {
    failures += 1;
    console.error(`  FAIL ${label}${detail === undefined ? "" : ` — ${String(detail)}`}`);
  }
}

async function waitReady(url, timeoutMs = 20000) {
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    try {
      const response = await fetch(url);
      if (response.ok) return true;
    } catch {
      // 还没起来
    }
    await new Promise((resolve) => setTimeout(resolve, 250));
  }
  return false;
}

async function main() {
  if (!existsSync(serverBin)) {
    console.error(`[ui-smoke] 找不到 ${serverBin}`);
    return 2;
  }
  if (!existsSync(join(distDir, "index.html"))) {
    console.error("[ui-smoke] 找不到 dist/index.html，先 npm run build");
    return 2;
  }

  const dbPath = join(mkdtempSync(join(tmpdir(), "genealogy-ui-")), "smoke.db");

  const server = spawn(serverBin, ["--port", String(API_PORT), "--db", dbPath, "--threads", "2"], {
    stdio: ["ignore", "pipe", "pipe"],
  });
  server.stderr.on("data", (chunk) => process.stderr.write(`[server] ${chunk}`));

  // vite preview 托管 dist（生产构建的真实形态）。
  // --host 127.0.0.1 必须显式：默认 "localhost" 可能只绑 ::1，
  // 脚本用 127.0.0.1 探测就连不上。
  const viteBin = join(webRoot, "node_modules", "vite", "bin", "vite.js");
  const preview = spawn(
    process.execPath,
    [viteBin, "preview", "--port", String(UI_PORT), "--strictPort", "--host", "127.0.0.1"],
    { cwd: webRoot, stdio: ["ignore", "pipe", "pipe"] },
  );
  preview.stderr.on("data", (chunk) => process.stderr.write(`[preview] ${chunk}`));

  const browser = await chromium.launch();
  const page = await browser.newPage();
  const consoleErrors = [];
  page.on("pageerror", (error) => consoleErrors.push(String(error)));
  page.on("console", (message) => {
    if (message.type() === "error") consoleErrors.push(message.text());
  });

  try {
    if (!(await waitReady(`${API_BASE}/health`))) {
      console.error("[ui-smoke] server 未就绪");
      return 2;
    }
    if (!(await waitReady(UI_BASE))) {
      console.error("[ui-smoke] preview 未就绪");
      return 2;
    }

    // ---- 打开页面，连接 ----
    await page.goto(UI_BASE);
    await page.fill('input[aria-label="服务器地址"]', API_BASE);
    await page.getByRole("button", { name: "连接" }).click();
    await page.waitForSelector("text=已连接", { timeout: 15000 });
    check("连接成功（徽标出现）", true);

    // ---- 新建宗族 ----
    // 定位走 label 文本（Field 是 label 包裹结构）；placeholder 放的是
    // 示例值（如：湘衡陈氏），不是字段名。
    await page.getByRole("button", { name: "新建宗族" }).click();
    await page.getByLabel("宗族名").fill("UI 闭环宗族");
    await page.getByLabel("姓氏").fill("测");
    await page.getByRole("button", { name: "建档", exact: true }).click();
    // option 在未展开的 select 里被判 hidden，不能 waitForSelector(visible)，
    // 轮询存在性即可
    await page.waitForFunction(
      () =>
        document.querySelectorAll('select[aria-label="宗族"] option').length > 0 &&
        [...document.querySelectorAll('select[aria-label="宗族"] option')].some((option) =>
          option.textContent?.includes("UI 闭环宗族"),
        ),
      undefined,
      { timeout: 10000 },
    );
    check("宗族建档并出现在下拉", true);

    // ---- 新建人物（闭环：建档）----
    await page.getByRole("button", { name: "新建人物" }).click();
    await page.locator(".modal").getByLabel("姓", { exact: true }).fill("测");
    await page.locator(".modal").getByLabel("名", { exact: true }).fill("UI始祖");
    await page.locator(".modal").getByLabel("世代").fill("1");
    await page.getByRole("button", { name: "保存" }).click();
    await page.waitForSelector(".modal-head:has-text('测UI始祖')", { timeout: 10000 });
    check("人物建档并弹出人物卡", true);

    // ---- SSE 实时条（M5-3）：建档的变更应实时推到界面 ----
    await page.waitForSelector(".event-feed .badge-ok", { timeout: 10000 });
    check("实时协同徽标在线", true);
    await page.waitForSelector(".event-item:has-text('person.created')", {
      timeout: 10000,
    });
    check("实时条收到 person.created", true);

    // 人物卡要素：五服/称谓工具 + 编辑按钮
    check("人物卡含五服/称谓工具", await page.locator("text=五服 / 称谓").count() > 0);

    // 关闭人物卡 → 列表出现
    await page.locator(".modal-head button").click();
    await page.waitForSelector("table.grid tbody tr:has-text('测UI始祖')", { timeout: 10000 });
    check("列表出现新人物", true);

    // 抓人物 ID 供世系图用
    const idCell = await page
      .locator("table.grid tbody tr:has-text('测UI始祖') td.mono")
      .first()
      .textContent();
    const personId = (idCell ?? "").trim();

    // ---- 世系图（闭环：世系图）----
    await page.getByRole("button", { name: /世系图/ }).click();
    await page.getByPlaceholder("中心人物 ID", { exact: true }).fill(personId);
    await page.getByRole("button", { name: "绘制" }).click();
    await page.waitForSelector("svg .tree-node", { timeout: 10000 });
    check("世系图 SVG 渲染出节点", true);
    const nodeText = await page.locator("svg .tree-node .node-name").first().textContent();
    check("世系图节点姓名正确", (nodeText ?? "").includes("UI始祖"), nodeText);

    // ---- 导出（闭环：导出）----
    // 宗族 ID 从人物视图的下拉取（导出视图只有 ID 输入框）。
    // 切回视图会重挂载 PersonsView，下拉由 effect 重新填充——
    // 必须等它有值，否则拿到空串、导出按钮保持 disabled。
    await page.getByRole("button", { name: /人物/ }).click();
    await page.waitForFunction(
      () => {
        const select = document.querySelector('select[aria-label="宗族"]');
        return Boolean(select && select.value);
      },
      undefined,
      { timeout: 10000 },
    );
    const selectedLineage = await page
      .locator('select[aria-label="宗族"]')
      .inputValue();
    await page.getByRole("button", { name: /导入导出/ }).click();

    const downloadPromise = page.waitForEvent("download", { timeout: 15000 });
    await page.getByPlaceholder("宗族 ID", { exact: true }).fill(selectedLineage);
    await page.getByRole("button", { name: "导出 .ged" }).click();
    const download = await downloadPromise;
    check("GEDCOM 导出触发下载", download.suggestedFilename().endsWith(".ged"), download.suggestedFilename());

    // DOT 导出
    const dotPromise = page.waitForEvent("download", { timeout: 15000 });
    await page.getByPlaceholder("中心人物 ID", { exact: true }).fill(personId);
    await page.getByRole("button", { name: "导出", exact: true }).click();
    const dotDownload = await dotPromise;
    check("世系图导出触发下载", dotDownload.suggestedFilename().endsWith(".dot"), dotDownload.suggestedFilename());

    // ---- 迁徙（M5-4）：登记地点 → 登记迁徙 → 地图与列表 ----
    await page.getByRole("button", { name: /迁徙/ }).click();
    await page.getByRole("button", { name: "登记地点" }).click();
    await page.getByLabel("地名").fill("UI故里");
    await page.getByLabel("纬度 lat").fill("27.83");
    await page.getByLabel("经度 lng").fill("112.94");
    await page
      .locator(".modal")
      .getByRole("button", { name: "登记", exact: true })
      .click();
    // 有坐标地点后迁徙地图应渲染出来
    await page.waitForSelector("svg.migration-map", { timeout: 10000 });
    check("迁徙地图渲染", true);

    await page.getByRole("button", { name: "登记迁徙" }).click();
    await page.locator(".modal").getByLabel("主体 ID").fill(personId);
    await page
      .locator(".modal")
      .getByLabel("迁入地")
      .selectOption({ label: "UI故里" });
    await page
      .locator(".modal")
      .getByRole("button", { name: "登记", exact: true })
      .click();
    await page.waitForSelector(
      "table.grid tbody tr:has-text('UI故里')",
      { timeout: 10000 },
    );
    check("迁徙登记出现在列表", true);

    // ---- 校验视图 ----
    await page.getByRole("button", { name: /校验/ }).click();
    await page.waitForSelector("table.grid", { timeout: 15000 });
    check("校验视图渲染", true);

    // ---- 无 JS 运行时错误 ----
    check("浏览器无未捕获错误", consoleErrors.length === 0, consoleErrors.slice(0, 3).join(" | "));

    await page.screenshot({ path: join(repoRoot, "build", "ui-smoke.png"), fullPage: true });
    console.log("[ui-smoke] 截图: build/ui-smoke.png");

    // ---- M9.4 验收门：窄视口（375×812，isMobile）完成建档→关系查询 ----
    const mobile = await browser.newContext({
      viewport: { width: 375, height: 812 },
      isMobile: true,
      hasTouch: true,
    });
    const mpage = await mobile.newPage();
    mpage.on("pageerror", (e) => consoleErrors.push("mobile: " + String(e)));
    await mpage.goto(UI_BASE);
    await mpage.fill('input[aria-label="服务器地址"]', API_BASE);
    await mpage.getByRole("button", { name: "连接" }).click();
    await mpage.waitForSelector("text=已连接", { timeout: 15000 });

    // 窄屏断言：导航可见（响应式折行后仍可达；按钮可访问名含 hint 文本）
    const navVisible = await mpage
      .getByRole("button", { name: /人物/ })
      .first()
      .isVisible();
    check("窄屏导航可见（响应式布局生效）", navVisible);

    // 窄屏建档：人物视图 → 新建人物 → 弹人物卡（对齐桌面流程选择器）
    await mpage.getByRole("button", { name: /新建人物/ }).click();
    await mpage.locator(".modal").getByLabel("姓", { exact: true }).fill("窄");
    await mpage.locator(".modal").getByLabel("名", { exact: true }).fill("屏建档");
    await mpage.locator(".modal").getByLabel("世代").fill("1");
    // 窄屏下弹窗内容高于视口：滚到底再点保存（否则 <dd> 等内容遮挡）
    await mpage.locator(".modal").evaluate((el) => {
      el.scrollTop = el.scrollHeight;
    });
    await mpage.getByRole("button", { name: "保存" }).click();
    await mpage.waitForSelector(".modal-head:has-text('窄屏建档')", {
      timeout: 10000,
    });
    check("窄屏人物建档并弹人物卡", true);

    // 关人物卡（开着会盖住导航——modal-backdrop 拦截点击）
    await mpage.getByRole("button", { name: "关闭" }).click();
    await mpage.waitForTimeout(300);

    // 窄屏关系查询：抓人物 ID → 世系图视图填中心人物 → 绘制
    // （对齐桌面流程：树以「中心人物 ID」为根，须显式填写）
    const mobileIdCell = await mpage
      .locator("table.grid tbody tr:has-text('窄屏建档') td.mono")
      .first()
      .textContent();
    const mobilePersonId = (mobileIdCell ?? "").trim();
    await mpage.getByRole("button", { name: /世系图/ }).click();
    await mpage.getByPlaceholder("中心人物 ID", { exact: true }).fill(mobilePersonId);
    await mpage.getByRole("button", { name: "绘制" }).click();
    await mpage.waitForSelector("svg .tree-node", { timeout: 10000 });
    check("窄屏世系图渲染（关系查询闭环）", true);

    await mpage.screenshot({
      path: join(repoRoot, "build", "ui-smoke-mobile.png"),
      fullPage: true,
    });
    await mobile.close();
    console.log("[ui-smoke] 窄视口验收: build/ui-smoke-mobile.png");

    console.log(failures === 0 ? "\n[ui-smoke] 全部通过" : `\n[ui-smoke] ${failures} 项失败`);
    return failures === 0 ? 0 : 1;
  } finally {
    await browser.close();
    server.kill();
    preview.kill();
    try {
      rmSync(dbPath, { force: true });
    } catch {
      // 临时库清理失败不致命
    }
  }
}

main().then((code) => process.exit(code));
