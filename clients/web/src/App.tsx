// 应用外壳：连接条 + 左侧导航 + 视图区。
//
// 视图切换用朴素的 state 路由——参考实现不需要 URL 路由的复杂度，
// 但每个视图的入口在导航上一目了然。

import { useState } from "react";

import { useClient } from "./context";
import { PersonsView } from "./views/PersonsView";
import { TreeView } from "./views/TreeView";
import { ExportView } from "./views/ExportView";
import { ValidateView } from "./views/ValidateView";
import { MigrationView } from "./views/MigrationView";

type ViewKey = "persons" | "tree" | "migration" | "export" | "validate";

const NAV: Array<{ key: ViewKey; label: string; hint: string }> = [
  { key: "persons", label: "人物", hint: "检索 · 建档 · 人物卡" },
  { key: "tree", label: "世系图", hint: "五世一图 / 全量" },
  { key: "migration", label: "迁徙", hint: "迁徙登记 · 地图" },
  { key: "export", label: "导入导出", hint: "GEDCOM 双向" },
  { key: "validate", label: "校验", hint: "数据质量规则" },
];

export function App() {
  const { connection, connect, disconnect } = useClient();
  const [view, setView] = useState<ViewKey>("persons");
  const [serverInput, setServerInput] = useState(connection.baseUrl);

  async function onConnect() {
    await connect(serverInput.trim() || "/api/v1");
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
          </main>
        </div>
      )}
    </div>
  );
}
