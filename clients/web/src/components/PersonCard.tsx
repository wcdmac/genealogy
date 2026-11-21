// 人物卡：详情 + 关系 + 五服/称谓 + 编辑/删除。
//
// 这是闭环的枢纽：从这里进建档编辑、建婚姻、查五服称谓，
// 也能跳到世系图视图的中心人物。

import { useCallback, useEffect, useRef, useState } from "react";

import { useClient } from "../context";
import { ErrorBar, Modal } from "../components/Modal";
import { PersonForm } from "./PersonForm";
import type {
  Appellation,
  MourningDegree,
  Person,
  Union,
} from "../api/client";

export function PersonCard({
  personId,
  onClose,
  onChanged,
  onSelectPerson,
}: {
  personId: string;
  onClose: () => void;
  onChanged: () => void;
  onSelectPerson: (id: string) => void;
}) {
  const { client } = useClient();
  const [person, setPerson] = useState<Person | null>(null);
  const [unions, setUnions] = useState<Union[]>([]);
  const [error, setError] = useState<unknown>(null);
  const [editing, setEditing] = useState(false);
  const [addingSpouse, setAddingSpouse] = useState(false);

  // 在途请求取消（第三轮新-W3）：切换 personId 时旧详情不再覆盖
  const reloadAbortRef = useRef<AbortController | null>(null);
  const reload = useCallback(async () => {
    reloadAbortRef.current?.abort();
    const controller = new AbortController();
    reloadAbortRef.current = controller;
    try {
      setError(null);
      const [detail, unionPage] = await Promise.all([
        client.getPerson(personId, ["events", "unions"], {
          signal: controller.signal,
        }),
        client.listUnions(
          { person_id: personId, limit: 50 },
          { signal: controller.signal },
        ),
      ]);
      if (controller.signal.aborted) return;
      setPerson(detail);
      setUnions((unionPage.data ?? []) as unknown as Union[]);
    } catch (caught) {
      if (controller.signal.aborted) return;
      setError(caught);
    }
  }, [client, personId]);

  useEffect(() => {
    void reload();
  }, [reload]);

  async function onDelete() {
    if (!person) return;
    // 当前是硬删除（spec 与实现一致）；软删除 + 审计追溯属 M8.5 路线图，
    // 文案不得承诺还没有的行为。
    if (!window.confirm(`删除 ${person.family_name}${person.given_name}？删除后不可恢复。`)) {
      return;
    }
    try {
      await client.deletePerson(person.id);
      onChanged();
      onClose();
    } catch (caught) {
      setError(caught);
    }
  }

  if (error && !person) {
    return (
      <Modal title="人物卡" onClose={onClose} width={640}>
        <ErrorBar error={error} />
      </Modal>
    );
  }
  if (!person) {
    return (
      <Modal title="人物卡" onClose={onClose} width={640}>
        <p className="muted">载入中…</p>
      </Modal>
    );
  }

  return (
    <Modal title={`${person.family_name}${person.given_name}`} onClose={onClose} width={720}>
      <ErrorBar error={error} />

      <div className="card-grid">
        <div>
          <h4>基本信息</h4>
          <dl className="props">
            <dt>ID</dt>
            <dd className="mono">{person.id}</dd>
            <dt>性别</dt>
            <dd>{person.gender === "male" ? "男" : person.gender === "female" ? "女" : "未知"}</dd>
            <dt>世代 / 行几</dt>
            <dd>
              {person.generation_no ?? "—"} / {person.birth_order ?? "—"}
            </dd>
            <dt>状态</dt>
            <dd>{person.is_living ? "在世" : "已故"}（{person.privacy_level ?? "—"}）</dd>
            <dt>版本</dt>
            <dd>rev={person.rev ?? "—"}</dd>
          </dl>
          {person.biography && (
            <>
              <h4>小传</h4>
              <p className="bio">{person.biography}</p>
            </>
          )}
        </div>

        <div>
          <h4>父母</h4>
          <ul className="link-list">
            <li>
              父亲：
              {person.father_id ? (
                <button className="link" onClick={() => onSelectPerson(person.father_id!)}>
                  {person.father_id}
                </button>
              ) : (
                <span className="muted">未录</span>
              )}
            </li>
            <li>
              母亲：
              {person.mother_id ? (
                <button className="link" onClick={() => onSelectPerson(person.mother_id!)}>
                  {person.mother_id}
                </button>
              ) : (
                <span className="muted">未录</span>
              )}
            </li>
          </ul>

          <h4>
            婚姻结合
            <button className="link" onClick={() => setAddingSpouse(true)}>
              ＋ 添加
            </button>
          </h4>
          <ul className="link-list">
            {unions.map((union) => (
              <li key={union.id}>
                {unionLabel(union.type)} · {union.status === "active" ? "在婚" : "解除"} ·
                成员 {union.members?.map((member) => member.role).join("/") ?? "—"}
              </li>
            ))}
            {unions.length === 0 && <li className="muted">未登记婚姻</li>}
          </ul>

          <h4>事件</h4>
          <ul className="link-list">
            {(person as Person & { events?: Array<{ id: string; type: string; description?: string }> }).events
              ?.map((event) => (
                <li key={event.id}>
                  {eventLabel(event.type)}
                  {event.description ? `：${event.description}` : ""}
                </li>
              )) ?? <li className="muted">无</li>}
          </ul>
        </div>
      </div>

      <KinshipTools personId={person.id} onSelectPerson={onSelectPerson} />

      <div className="modal-actions">
        <button onClick={() => setEditing(true)}>编辑</button>
        <button className="danger" onClick={() => void onDelete()}>
          删除
        </button>
      </div>

      {editing && (
        <PersonForm
          mode="edit"
          person={person}
          onClose={() => setEditing(false)}
          onSaved={() => {
            setEditing(false);
            void reload();
            onChanged();
          }}
        />
      )}
      {addingSpouse && (
        <SpouseDialog
          person={person}
          onClose={() => setAddingSpouse(false)}
          onCreated={() => {
            setAddingSpouse(false);
            void reload();
            onChanged();
          }}
        />
      )}
    </Modal>
  );
}

function unionLabel(type?: string): string {
  if (type === "marriage") return "婚姻";
  if (type === "concubinage") return "妾";
  if (type === "remarriage") return "再婚";
  return type ?? "—";
}

function eventLabel(type?: string): string {
  const map: Record<string, string> = {
    birth: "出生",
    death: "亡故",
    burial: "安葬",
    marriage: "婚姻",
    migration: "迁徙",
    examination: "科考",
    office: "出仕",
    honor: "褒荣",
    residence: "居止",
    annotation: "注解",
  };
  return (type && map[type]) || type || "—";
}

/** 五服 + 称谓查询：选同族另一人，算从当前人物视角的丧服与称呼。 */
function KinshipTools({
  personId,
  onSelectPerson,
}: {
  personId: string;
  onSelectPerson: (id: string) => void;
}) {
  const { client } = useClient();
  const [target, setTarget] = useState("");
  const [mourning, setMourning] = useState<MourningDegree | null>(null);
  const [appellation, setAppellation] = useState<Appellation | null>(null);
  const [error, setError] = useState<unknown>(null);

  async function onQuery() {
    if (!target.trim()) return;
    try {
      setError(null);
      const [degree, appel] = await Promise.all([
        client.getMourningDegree(personId, target.trim()),
        client.getAppellation(personId, target.trim()),
      ]);
      setMourning(degree);
      setAppellation(appel);
    } catch (caught) {
      setError(caught);
      setMourning(null);
      setAppellation(null);
    }
  }

  return (
    <div className="kinship">
      <h4>五服 / 称谓</h4>
      <div className="field-row">
        <input
          className="mono"
          placeholder="对方人物 ID"
          value={target}
          onChange={(event) => setTarget(event.target.value)}
        />
        <button onClick={() => void onQuery()}>计算</button>
        <button className="link" onClick={() => onSelectPerson(target.trim())}>
          打开对方
        </button>
      </div>
      <ErrorBar error={error} />
      {mourning && (
        <p>
          五服：<strong>{mourning.garment_name ?? mourning.garment}</strong>
          （{mourning.duration_months} 月，规则 {mourning.rule_id ?? "—"}）
        </p>
      )}
      {appellation && (
        <p>
          称谓：书面 <strong>{appellation.standard}</strong>、口语{" "}
          <strong>{appellation.colloquial}</strong>
          {appellation.elder ? "（长辈）" : "（晚辈/平辈）"}
        </p>
      )}
    </div>
  );
}

function SpouseDialog({
  person,
  onClose,
  onCreated,
}: {
  person: Person;
  onClose: () => void;
  onCreated: () => void;
}) {
  const { client } = useClient();
  const [spouseId, setSpouseId] = useState("");
  const [type, setType] = useState<"marriage" | "concubinage" | "remarriage">(
    "marriage",
  );
  const [role, setRole] = useState<"husband" | "wife" | "concubine">("wife");
  const [error, setError] = useState<unknown>(null);

  async function onSubmit() {
    if (!spouseId.trim()) return;
    try {
      await client.createUnion({
        type,
        members: [
          { person_id: person.id, role: role === "wife" ? "husband" : "wife" },
          { person_id: spouseId.trim(), role },
        ],
      });
      onCreated();
    } catch (caught) {
      setError(caught);
    }
  }

  return (
    <Modal title="添加婚姻结合" onClose={onClose} width={460}>
      <ErrorBar error={error} />
      <label className="field">
        <span className="field-label">对方人物 ID</span>
        <input
          className="mono"
          value={spouseId}
          onChange={(event) => setSpouseId(event.target.value)}
        />
      </label>
      <div className="field-row">
        <label className="field">
          <span className="field-label">类型</span>
          <select
            value={type}
            onChange={(event) => setType(event.target.value as typeof type)}
          >
            <option value="marriage">婚姻</option>
            <option value="concubinage">妾</option>
            <option value="remarriage">再婚</option>
          </select>
        </label>
        <label className="field">
          <span className="field-label">对方角色</span>
          <select
            value={role}
            onChange={(event) => setRole(event.target.value as typeof role)}
          >
            <option value="wife">妻</option>
            <option value="concubine">妾</option>
            <option value="husband">夫</option>
          </select>
        </label>
      </div>
      <div className="modal-actions">
        <button onClick={onClose}>取消</button>
        <button
          className="primary"
          disabled={!spouseId.trim()}
          onClick={() => void onSubmit()}
        >
          建立
        </button>
      </div>
    </Modal>
  );
}
