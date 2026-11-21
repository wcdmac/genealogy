// 校验视图：跑一致性规则，看数据质量问题清单。
// 「无证据事实数」等核心质量指标随 M5 证据链落地后在此展示。

import { useCallback, useEffect, useRef, useState } from "react";

import { useClient } from "../context";
import { ErrorBar } from "../components/Modal";

interface Issue {
  rule_id?: string;
  severity?: "error" | "warning" | "info";
  entity_type?: string;
  entity_id?: string;
  message?: string;
  suggestion?: string;
}

export function ValidateView() {
  const { client } = useClient();
  const [issues, setIssues] = useState<Issue[]>([]);
  const [summary, setSummary] = useState<{ error?: number; warning?: number; info?: number }>({});
  const [error, setError] = useState<unknown>(null);
  const [running, setRunning] = useState(false);

  // 在途请求取消（第三轮新-W3）：连点「重新校验」时旧响应不再覆盖
  const runAbortRef = useRef<AbortController | null>(null);
  const run = useCallback(async () => {
    runAbortRef.current?.abort();
    const controller = new AbortController();
    runAbortRef.current = controller;
    setRunning(true);
    try {
      setError(null);
      const result = await client.runValidation(undefined, {
        signal: controller.signal,
      });
      if (controller.signal.aborted) return;
      setIssues(result.issues ?? []);
      setSummary(result.summary ?? {});
    } catch (caught) {
      if (controller.signal.aborted) return;
      setError(caught);
    } finally {
      // 只由当前这轮收 running 标志——被新一轮 abort 时不越权清标志
      if (runAbortRef.current === controller) setRunning(false);
    }
  }, [client]);

  useEffect(() => {
    void run();
  }, [run]);

  return (
    <section>
      <div className="toolbar">
        <button className="primary" disabled={running} onClick={() => void run()}>
          {running ? "校验中…" : "重新校验"}
        </button>
        <span className="badge badge-error">错误 {summary.error ?? 0}</span>
        <span className="badge badge-warn">警告 {summary.warning ?? 0}</span>
        <span className="badge">提示 {summary.info ?? 0}</span>
      </div>

      <ErrorBar error={error} />

      <table className="grid">
        <thead>
          <tr>
            <th>级别</th>
            <th>规则</th>
            <th>对象</th>
            <th>说明</th>
            <th>建议</th>
          </tr>
        </thead>
        <tbody>
          {issues.map((issue, index) => (
            <tr key={`${issue.rule_id}-${index}`}>
              <td>{severityLabel(issue.severity)}</td>
              <td className="mono">{issue.rule_id}</td>
              <td className="mono">
                {issue.entity_type}/{issue.entity_id?.slice(0, 8)}
              </td>
              <td>{issue.message}</td>
              <td className="muted">{issue.suggestion}</td>
            </tr>
          ))}
          {issues.length === 0 && (
            <tr>
              <td colSpan={5} className="empty">
                没有发现问题。
              </td>
            </tr>
          )}
        </tbody>
      </table>
    </section>
  );
}

function severityLabel(severity?: string): string {
  if (severity === "error") return "错误";
  if (severity === "warning") return "警告";
  return "提示";
}
