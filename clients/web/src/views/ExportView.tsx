// 导入导出视图：GEDCOM 双向 + 世系图 DOT/Mermaid 下载。
// 闭环的最后一环——「导出」在这里完成。

import { useRef, useState } from "react";

import { useClient } from "../context";
import { ErrorBar } from "../components/Modal";

export function ExportView() {
  const { client } = useClient();
  const [lineageId, setLineageId] = useState("");
  const [treePersonId, setTreePersonId] = useState("");
  const [format, setFormat] = useState<"dot" | "mermaid">("dot");
  const [importLineageId, setImportLineageId] = useState("");
  const [error, setError] = useState<unknown>(null);
  const [message, setMessage] = useState("");
  const fileRef = useRef<HTMLInputElement>(null);

  function download(filename: string, text: string) {
    const blob = new Blob([text], { type: "text/plain;charset=utf-8" });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement("a");
    anchor.href = url;
    anchor.download = filename;
    anchor.click();
    URL.revokeObjectURL(url);
  }

  async function onExportGedcom() {
    try {
      setError(null);
      const text = await client.exportGedcom(lineageId.trim());
      download(`${lineageId.trim() || "lineage"}.ged`, text);
      setMessage("GEDCOM 已导出");
    } catch (caught) {
      setError(caught);
    }
  }

  async function onExportTree() {
    try {
      setError(null);
      const text = await client.exportTree(treePersonId.trim(), format);
      download(`${treePersonId.trim() || "tree"}.${format === "dot" ? "dot" : "mmd"}`, text);
      setMessage(`世系图（${format}）已导出`);
    } catch (caught) {
      setError(caught);
    }
  }

  async function onImport(file: File) {
    try {
      setError(null);
      const result = await client.importGedcom(file, {
        lineage_id: importLineageId.trim() || undefined,
        filename: file.name,
      });
      setMessage(
        `导入完成：${result.imported ?? 0} 人，跳过 ${result.skipped ?? 0} 条` +
          (result.warnings?.length ? `；${result.warnings.length} 条警告` : ""),
      );
    } catch (caught) {
      setError(caught);
    }
  }

  return (
    <section className="stack">
      <ErrorBar error={error} />
      {message && <div className="banner banner-ok">{message}</div>}

      <div className="panel">
        <h3>GEDCOM 导出</h3>
        <div className="field-row">
          <input
            className="mono"
            placeholder="宗族 ID"
            value={lineageId}
            onChange={(event) => setLineageId(event.target.value)}
            size={28}
          />
          <button
            className="primary"
            disabled={!lineageId.trim()}
            onClick={() => void onExportGedcom()}
          >
            导出 .ged
          </button>
        </div>
        <p className="muted">整族导出（人物 + 婚姻 + 事件 + 地点），供其他谱牒软件互操作。</p>
      </div>

      <div className="panel">
        <h3>世系图导出</h3>
        <div className="field-row">
          <input
            className="mono"
            placeholder="中心人物 ID"
            value={treePersonId}
            onChange={(event) => setTreePersonId(event.target.value)}
            size={28}
          />
          <select
            aria-label="格式"
            value={format}
            onChange={(event) => setFormat(event.target.value as typeof format)}
          >
            <option value="dot">Graphviz DOT</option>
            <option value="mermaid">Mermaid</option>
          </select>
          <button
            className="primary"
            disabled={!treePersonId.trim()}
            onClick={() => void onExportTree()}
          >
            导出
          </button>
        </div>
        <p className="muted">以该人物为根的子孙树；DOT 可接 graphviz 出图，Mermaid 可直接贴进 Markdown。</p>
      </div>

      <div className="panel">
        <h3>GEDCOM 导入</h3>
        <div className="field-row">
          <input
            className="mono"
            placeholder="目标宗族 ID（可空）"
            value={importLineageId}
            onChange={(event) => setImportLineageId(event.target.value)}
            size={28}
          />
          <input
            ref={fileRef}
            type="file"
            accept=".ged,.gedcom,text/plain"
            onChange={(event) => {
              const file = event.target.files?.[0];
              if (file) void onImport(file);
            }}
          />
        </div>
        <p className="muted">
          导入返回映射损失报告（警告 = 有信息没进来，如 OBJE/SOUR 未建模）；
          本家谱软件导出的文件往返零损失。
        </p>
      </div>
    </section>
  );
}
