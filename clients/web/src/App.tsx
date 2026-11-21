// 应用外壳：连接条 + 左侧导航 + 视图区。
//
// 视图切换用朴素的 state 路由——参考实现不需要 URL 路由的复杂度，
// 但每个视图的入口在导航上一目了然。

import { useState } from "react";

import { useClient } from "./context";
import { EventFeed } from "./components/EventFeed";
import { PersonsView } from "./views/PersonsView";
import { TreeView } from "./views/TreeView";
import { ExportView } from "./views/ExportView";
import { ValidateView } from "./views/ValidateView";
import { MigrationView } from "./views/MigrationView";
import { TranscribeView } from "./views/TranscribeView";

type ViewKey =
  | "persons"
  | "tree"
  | "migration"
  | "export"
  | "validate"
  | "transcribe";

const NAV: Array<{ key: ViewKey; label: string; hint: string }> = [
  { key: "persons", label: "人物", hint: "检索 · 建档 · 人物卡" },
  { key: "tree", label: "世系图", hint: "五世一图 / 全量" },
  { key: "migration", label: "迁徙", hint: "迁徙登记 · 地图" },
  { key: "export", label: "导入导出", hint: "GEDCOM 双向" },
  { key: "validate", label: "校验", hint: "数据质量规则" },
  { key: "transcribe", label: "转录工作台", hint: "老谱扫描件 → 结构化录入" },
];

// 服务器地址的轻量源边界（M13，复核裁定为轻量版而非硬白名单——
// README 明确支持跨端口填 http://127.0.0.1:8080/api/v1 这类局域网
// 地址，硬白名单会误伤）。三件事：scheme 只认 http/https 或同源
// 相对路径；外域（hostname 不同）连接前显式确认；换址清旧令牌在
// context.connect。
function isConnectableAddress(target: string): boolean {
  if (target.startsWith("/")) return true;  // 同源相对路径
  try {
    const url = new URL(target);
    return url.protocol === "http:" || url.protocol === "https:";
  } catch {
    return false;  // 「localhost:8080/api/v1」这类无 scheme 会被 URL
                   // 解析成伪协议——此前静默产出坏 URL，一并堵掉
  }
}

function isSameHost(target: string): boolean {
  if (target.startsWith("/")) return true;
  if (typeof window === "undefined") return true;  // 非浏览器环境不拦
  try {
    // 按 hostname 比较（不含端口）：README 明确支持同机跨端口部署
    // （vite 代理端口 → server 端口），硬按 origin 会误伤这一场景；
    // 「外域」= hostname 不同，才需要确认。
    return (
      new URL(target, window.location.origin).hostname ===
      window.location.hostname
    );
  } catch {
    return false;
  }
}

export function App() {
  const { connection, connect, disconnect } = useClient();
  const [view, setView] = useState<ViewKey>("persons");
  const [serverInput, setServerInput] = useState(connection.baseUrl);
  // 凭据可输入（2026-11 静态审查 H10：曾硬编码 web/web，--require-auth
  // 部署下无法登录也无法轮换凭据）
  const [userInput, setUserInput] = useState("web");
  const [passInput, setPassInput] = useState("web");
  const [addressNotice, setAddressNotice] = useState<string | null>(null);

  async function onConnect() {
    const target = serverInput.trim() || "/api/v1";
    if (!isConnectableAddress(target)) {
      setAddressNotice(
        "地址需为 http(s):// 开头的完整地址，或 / 开头的同源路径",
      );
      return;
    }
    // 外域 = hostname 不同的地址：凭据与数据都会发往该主机。诱导粘贴
    // 外部地址是真实外发路径，连接前让用户看清去向；同主机跨端口
    // （局域网多端口部署）与同地址重连（如重登）不打扰。
    if (
      !isSameHost(target) &&
      target !== connection.baseUrl &&
      typeof window !== "undefined" &&
      !window.confirm(
        `即将连接外部服务器：${new URL(target, window.location.origin).origin}\n` +
          "输入的凭据与后续数据都会发往该地址。确定继续吗？",
      )
    ) {
      return;
    }
    setAddressNotice(null);
    await connect(target, userInput.trim(), passInput);
  }

  return (
    <div className="app">
      <header className="topbar">
        <div className="brand">家谱管理 · Web 参考实现</div>
        <div className="connection">
          <input
            aria-label="服务器地址"
            value={serverInput}
            onChange={(event) => setServerInput(event.target.value)}
            placeholder="/api/v1"
            size={28}
          />
          <input
            aria-label="用户名"
            value={userInput}
            onChange={(event) => setUserInput(event.target.value)}
            size={10}
          />
          <input
            aria-label="口令"
            type="password"
            value={passInput}
            onChange={(event) => setPassInput(event.target.value)}
            size={10}
          />
          {connection.connected ? (
            <>
              <span className="badge badge-ok">
                已连接{connection.role ? ` · ${connection.role}` : ""}
              </span>
              <button onClick={disconnect}>断开</button>
            </>
          ) : (
            <button className="primary" onClick={onConnect}>
              连接
            </button>
          )}
        </div>
      </header>

      {connection.error && (
        <div className="banner banner-error">连接失败：{connection.error}</div>
      )}
      {addressNotice && (
        <div className="banner banner-error">{addressNotice}</div>
      )}
      {connection.connected && <EventFeed />}
      {!connection.connected && !connection.error && (
        <div className="banner">
          未连接。先启动 <code>genealogy-server</code>（默认 127.0.0.1:8080），
          再点「连接」——本地免鉴权模式下任意口令可过。
        </div>
      )}

      {connection.connected && (
        <div className="body">
          <nav className="nav">
            {NAV.map((item) => (
              <button
                key={item.key}
                className={view === item.key ? "nav-item active" : "nav-item"}
                onClick={() => setView(item.key)}
              >
                <span className="nav-label">{item.label}</span>
                <span className="nav-hint">{item.hint}</span>
              </button>
            ))}
          </nav>
          <main className="content">
            {view === "persons" && <PersonsView />}
            {view === "tree" && <TreeView />}
            {view === "migration" && <MigrationView />}
            {view === "export" && <ExportView />}
            {view === "validate" && <ValidateView />}
            {view === "transcribe" && <TranscribeView />}
          </main>
        </div>
      )}
    </div>
  );
}
