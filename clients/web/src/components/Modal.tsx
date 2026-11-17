// 共享弹层。参考实现不引组件库，一个够用的 modal 就够。

import type { ReactNode } from "react";

export function Modal({
  title,
  onClose,
  children,
  width = 520,
}: {
  title: string;
  onClose: () => void;
  children: ReactNode;
  width?: number;
}) {
  return (
    <div className="modal-backdrop" onClick={onClose}>
      <div
        className="modal"
        style={{ width: `min(${width}px, 92vw)` }}
        onClick={(event) => event.stopPropagation()}
      >
        <div className="modal-head">
          <h3>{title}</h3>
          <button className="ghost" onClick={onClose} aria-label="关闭">
            ✕
          </button>
        </div>
        <div className="modal-body">{children}</div>
      </div>
    </div>
  );
}

/** 行内字段编辑行：标签 + 控件。 */
export function Field({
  label,
  children,
  hint,
}: {
  label: string;
  children: ReactNode;
  hint?: string;
}) {
  return (
    <label className="field">
      <span className="field-label">
        {label}
        {hint && <span className="field-hint">{hint}</span>}
      </span>
      {children}
    </label>
  );
}

/** 错误条：把 ApiError 的 problem.detail 显示给人看。 */
export function ErrorBar({ error }: { error: unknown }) {
  if (!error) return null;
  const message =
    error instanceof Error ? error.message : String(error);
  return <div className="banner banner-error">{message}</div>;
}
