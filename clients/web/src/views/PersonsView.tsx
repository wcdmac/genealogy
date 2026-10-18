// 人物列表视图：宗族选择 + 检索 + 分页 + 建档入口。
//
// 闭环第一步都在这里：选/建宗族 → 看到人 → 新建人物 → 点进人物卡。

import { useCallback, useEffect, useState } from "react";

import { useClient } from "../context";
import { ErrorBar, Modal } from "../components/Modal";
import { PersonForm } from "../components/PersonForm";
import { PersonCard } from "../components/PersonCard";
import type { Lineage, Person } from "../api/client";

interface Filters {
  q: string;
  gender: "" | "male" | "female" | "unknown";
  living: "" | "yes" | "no";
}

export function PersonsView() {
  const { client } = useClient();
  const [lineages, setLineages] = useState<Lineage[]>([]);
  const [lineageId, setLineageId] = useState<string>("");
  const [persons, setPersons] = useState<Person[]>([]);
  const [cursor, setCursor] = useState<string | undefined>();
  const [hasMore, setHasMore] = useState(false);
  const [filters, setFilters] = useState<Filters>({
    q: "",
    gender: "",
    living: "",
  });
  const [applied, setApplied] = useState<Filters>(filters);
  const [error, setError] = useState<unknown>(null);
  const [showCreate, setShowCreate] = useState(false);
  const [showLineageCreate, setShowLineageCreate] = useState(false);
  const [selected, setSelected] = useState<string | null>(null);

  // 宗族列表：连上就拉一次
  useEffect(() => {
    client
      .listLineages({ limit: 50 })
      .then((page) => {
        const list = (page.data ?? []) as unknown as Lineage[];
        setLineages(list);
        if (list.length > 0) setLineageId((prev) => prev || list[0].id);
      })
      .catch(setError);
  }, [client]);

  const reload = useCallback(
    async (nextCursor?: string) => {
      if (!lineageId) return;
      try {
        setError(null);
        const page = await client.listPersons({
          lineage_id: lineageId,
          q: applied.q || undefined,
          gender: applied.gender || undefined,
          is_living:
            applied.living === "" ? undefined : applied.living === "yes",
          cursor: nextCursor,
          limit: 50,
        });
        setPersons((page.data ?? []) as unknown as Person[]);
        setCursor(page.page?.next_cursor ?? undefined);
        setHasMore(Boolean(page.page?.has_more));
      } catch (caught) {
        setError(caught);
      }
    },
    [client, lineageId, applied],
  );

  useEffect(() => {
    void reload(undefined);
  }, [reload]);

  return (
    <section>
      <div className="toolbar">
        <select
          aria-label="宗族"
          value={lineageId}
          onChange={(event) => setLineageId(event.target.value)}
        >
          <option value="">（选择宗族）</option>
          {lineages.map((lineage) => (
            <option key={lineage.id} value={lineage.id}>
              {lineage.name}（{lineage.surname}）
            </option>
          ))}
        </select>
        <button onClick={() => setShowLineageCreate(true)}>新建宗族</button>
        <span className="spacer" />
        <input
          placeholder="检索姓名/字号"
          value={filters.q}
          onChange={(event) =>
            setFilters({ ...filters, q: event.target.value })
          }
          onKeyDown={(event) => {
            if (event.key === "Enter") setApplied(filters);
          }}
        />
        <select
          aria-label="性别"
          value={filters.gender}
          onChange={(event) =>
            setFilters({ ...filters, gender: event.target.value as Filters["gender"] })
          }
        >
          <option value="">全部性别</option>
          <option value="male">男</option>
          <option value="female">女</option>
          <option value="unknown">未知</option>
        </select>
        <select
          aria-label="在世"
          value={filters.living}
          onChange={(event) =>
            setFilters({ ...filters, living: event.target.value as Filters["living"] })
          }
        >
          <option value="">全部</option>
          <option value="yes">在世</option>
          <option value="no">已故</option>
        </select>
        <button className="primary" onClick={() => setApplied(filters)}>
          检索
        </button>
        <button
          className="primary"
          disabled={!lineageId}
          onClick={() => setShowCreate(true)}
        >
          新建人物
        </button>
      </div>

      <ErrorBar error={error} />

      <table className="grid">
        <thead>
          <tr>
            <th>姓名</th>
            <th>性别</th>
            <th>世代</th>
            <th>行几</th>
            <th>状态</th>
            <th>ID</th>
          </tr>
        </thead>
        <tbody>
          {persons.map((person) => (
            <tr
              key={person.id}
              onClick={() => setSelected(person.id)}
              className="clickable"
            >
              <td>
                {person.family_name}
                {person.given_name}
                {person.courtesy_name ? `（字${person.courtesy_name}）` : ""}
              </td>
              <td>{genderLabel(person.gender)}</td>
              <td>{person.generation_no ?? "—"}</td>
              <td>{person.birth_order ?? "—"}</td>
              <td>{person.is_living ? "在世" : "已故"}</td>
              <td className="mono">{person.id}</td>
            </tr>
          ))}
          {persons.length === 0 && (
            <tr>
              <td colSpan={6} className="empty">
                没有人物。连上 server 后点「新建人物」开始建档。
              </td>
            </tr>
          )}
        </tbody>
      </table>

      <div className="toolbar">
        <button disabled={!cursor || !hasMore} onClick={() => void reload(cursor)}>
          下一页
        </button>
        {hasMore && <span className="muted">还有更多</span>}
      </div>

      {showLineageCreate && (
        <LineageCreateDialog
          onClose={() => setShowLineageCreate(false)}
          onCreated={(lineage) => {
            setLineages((prev) => [...prev, lineage]);
            setLineageId(lineage.id);
            setShowLineageCreate(false);
          }}
        />
      )}
      {showCreate && (
        <PersonForm
          mode="create"
          lineageId={lineageId}
          onClose={() => setShowCreate(false)}
          onSaved={(person) => {
            setShowCreate(false);
            void reload(undefined);
            setSelected(person.id);
          }}
        />
      )}
      {selected && (
        <PersonCard
          personId={selected}
          onClose={() => setSelected(null)}
          onChanged={() => void reload(undefined)}
          onSelectPerson={setSelected}
        />
      )}
    </section>
  );
}

function genderLabel(gender?: string): string {
  if (gender === "male") return "男";
  if (gender === "female") return "女";
  return "未知";
}

function LineageCreateDialog({
  onClose,
  onCreated,
}: {
  onClose: () => void;
  onCreated: (lineage: Lineage) => void;
}) {
  const { client } = useClient();
  const [name, setName] = useState("");
  const [surname, setSurname] = useState("");
  const [poem, setPoem] = useState("");
  const [error, setError] = useState<unknown>(null);

  async function onSubmit() {
    try {
      const lineage = await client.createLineage({
        name,
        surname,
        generation_poem: poem || undefined,
      });
      onCreated(lineage);
    } catch (caught) {
      setError(caught);
    }
  }

  return (
    <Modal title="新建宗族" onClose={onClose}>
      <ErrorBar error={error} />
      <label className="field">
        <span className="field-label">宗族名</span>
        <input
          value={name}
          onChange={(event) => setName(event.target.value)}
          placeholder="如：湘衡陈氏"
        />
      </label>
      <label className="field">
        <span className="field-label">姓氏</span>
        <input
          value={surname}
          onChange={(event) => setSurname(event.target.value)}
          placeholder="如：陈"
        />
      </label>
      <label className="field">
        <span className="field-label">
          派语<span className="field-hint">字辈联句，可留空</span>
        </span>
        <textarea
          value={poem}
          onChange={(event) => setPoem(event.target.value)}
          rows={2}
          placeholder="如：德茂光先祖，文章振万邦"
        />
      </label>
      <div className="modal-actions">
        <button onClick={onClose}>取消</button>
        <button
          className="primary"
          disabled={!name || !surname}
          onClick={() => void onSubmit()}
        >
          建档
        </button>
      </div>
    </Modal>
  );
}
