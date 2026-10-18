// 人物建档 / 编辑表单（同一组件两种模式）。
//
// create：必填 lineage_id/姓/名/性别；父母用 ID 填写（Web 参考实现的
//   简化——桌面 GUI 提供点选，这里从人物列表复制 ID 即可）。
// edit：只提交可改字段，且必须带 If-Match（当前 rev）；rev 过期时
//   server 返 409，表单提示「请重新打开人物卡再改」。

import { useState } from "react";

import { useClient } from "../context";
import { ErrorBar, Field, Modal } from "../components/Modal";
import type { Person, PersonCreate, PersonUpdate } from "../api/client";

export function PersonForm({
  mode,
  lineageId,
  person,
  onClose,
  onSaved,
}: {
  mode: "create" | "edit";
  lineageId?: string;
  person?: Person;
  onClose: () => void;
  onSaved: (person: Person) => void;
}) {
  const { client } = useClient();
  const [error, setError] = useState<unknown>(null);
  const [saving, setSaving] = useState(false);

  // create 字段（宗族由父视图选定后传入，本表单不提供切换）
  const [familyName, setFamilyName] = useState("");
  const [givenName, setGivenName] = useState("");
  const [courtesyName, setCourtesyName] = useState("");
  const [gender, setGender] = useState<"male" | "female" | "unknown">("male");
  const [generationNo, setGenerationNo] = useState("");
  const [birthOrder, setBirthOrder] = useState("");
  const [fatherId, setFatherId] = useState("");
  const [motherId, setMotherId] = useState("");
  const [birthDate, setBirthDate] = useState("");
  const [isLiving, setIsLiving] = useState(true);

  // edit 字段（初始值来自 person）
  const [editCourtesy, setEditCourtesy] = useState(person?.courtesy_name ?? "");
  const [editGeneration, setEditGeneration] = useState(
    person?.generation_no != null ? String(person.generation_no) : "",
  );
  const [editBirthOrder, setEditBirthOrder] = useState(
    person?.birth_order != null ? String(person.birth_order) : "",
  );
  const [editBiography, setEditBiography] = useState(person?.biography ?? "");
  const [editLiving, setEditLiving] = useState(person?.is_living ?? true);
  const [editPrivacy, setEditPrivacy] = useState(
    person?.privacy_level ?? "kin",
  );

  async function onCreate() {
    const body: PersonCreate = {
      lineage_id: lineageId ?? "",
      family_name: familyName,
      given_name: givenName,
      gender,
      is_living: isLiving,
    };
    if (courtesyName) body.courtesy_name = courtesyName;
    if (generationNo) body.generation_no = Number(generationNo);
    if (birthOrder) body.birth_order = Number(birthOrder);
    if (fatherId) body.father_id = fatherId;
    if (motherId) body.mother_id = motherId;
    if (birthDate) {
      body.birth_date = { calendar: "gregorian", gregorian: birthDate, precision: "day" };
    }
    setSaving(true);
    try {
      const saved = await client.createPerson(body);
      onSaved(saved);
    } catch (caught) {
      setError(caught);
    } finally {
      setSaving(false);
    }
  }

  async function onEdit() {
    if (!person) return;
    const patch: PersonUpdate = {};
    if (editCourtesy !== (person.courtesy_name ?? "")) {
      patch.courtesy_name = editCourtesy;
    }
    if (editGeneration) patch.generation_no = Number(editGeneration);
    if (editBirthOrder) patch.birth_order = Number(editBirthOrder);
    if (editBiography !== (person.biography ?? "")) {
      patch.biography = editBiography;
    }
    patch.is_living = editLiving;
    if (editPrivacy !== person.privacy_level) {
      patch.privacy_level = editPrivacy as PersonUpdate["privacy_level"];
    }
    setSaving(true);
    try {
      const saved = await client.updatePerson(person.id, patch, person.rev ?? 0);
      onSaved(saved);
    } catch (caught) {
      setError(caught);
    } finally {
      setSaving(false);
    }
  }

  return (
    <Modal
      title={mode === "create" ? "新建人物" : `编辑 ${person?.family_name ?? ""}${person?.given_name ?? ""}`}
      onClose={onClose}
      width={560}
    >
      <ErrorBar error={error} />

      {mode === "create" ? (
        <>
          <Field label="姓">
            <input
              value={familyName}
              onChange={(event) => setFamilyName(event.target.value)}
              placeholder="姓"
            />
          </Field>
          <Field label="名">
            <input
              value={givenName}
              onChange={(event) => setGivenName(event.target.value)}
              placeholder="名"
            />
          </Field>
          <Field label="字" hint="表字，可空">
            <input
              value={courtesyName}
              onChange={(event) => setCourtesyName(event.target.value)}
              placeholder="字"
            />
          </Field>
          <Field label="性别">
            <select
              value={gender}
              onChange={(event) =>
                setGender(event.target.value as typeof gender)
              }
            >
              <option value="male">男</option>
              <option value="female">女</option>
              <option value="unknown">未知</option>
            </select>
          </Field>
          <div className="field-row">
            <Field label="世代">
              <input
                value={generationNo}
                inputMode="numeric"
                onChange={(event) => setGenerationNo(event.target.value)}
              />
            </Field>
            <Field label="行几">
              <input
                value={birthOrder}
                inputMode="numeric"
                onChange={(event) => setBirthOrder(event.target.value)}
              />
            </Field>
          </div>
          <Field label="父亲 ID" hint="从人物列表复制，可空">
            <input
              className="mono"
              value={fatherId}
              onChange={(event) => setFatherId(event.target.value)}
            />
          </Field>
          <Field label="母亲 ID" hint="从人物列表复制，可空">
            <input
              className="mono"
              value={motherId}
              onChange={(event) => setMotherId(event.target.value)}
            />
          </Field>
          <Field label="公历生日" hint="留空则不登记">
            <input
              type="date"
              value={birthDate}
              onChange={(event) => setBirthDate(event.target.value)}
            />
          </Field>
          <Field label="在世">
            <input
              type="checkbox"
              checked={isLiving}
              onChange={(event) => setIsLiving(event.target.checked)}
            />
          </Field>
        </>
      ) : (
        <>
          <Field label="字">
            <input
              value={editCourtesy}
              onChange={(event) => setEditCourtesy(event.target.value)}
            />
          </Field>
          <div className="field-row">
            <Field label="世代">
              <input
                value={editGeneration}
                inputMode="numeric"
                onChange={(event) => setEditGeneration(event.target.value)}
              />
            </Field>
            <Field label="行几">
              <input
                value={editBirthOrder}
                inputMode="numeric"
                onChange={(event) => setEditBirthOrder(event.target.value)}
              />
            </Field>
          </div>
          <Field label="小传">
            <textarea
              rows={4}
              value={editBiography}
              onChange={(event) => setEditBiography(event.target.value)}
            />
          </Field>
          <div className="field-row">
            <Field label="在世">
              <input
                type="checkbox"
                checked={editLiving}
                onChange={(event) => setEditLiving(event.target.checked)}
              />
            </Field>
            <Field label="隐私级别">
              <select
                value={editPrivacy}
                onChange={(event) =>
                  setEditPrivacy(event.target.value as typeof editPrivacy)
                }
              >
                <option value="public">公开</option>
                <option value="kin">族内</option>
                <option value="private">私密</option>
              </select>
            </Field>
          </div>
          <p className="muted">
            更新将携带 If-Match（当前 rev=
            {person?.rev ?? "?"}）；若他人已修改，server 返回 409，
            重新打开人物卡即可拿到最新版本。
          </p>
        </>
      )}

      <div className="modal-actions">
        <button onClick={onClose}>取消</button>
        <button
          className="primary"
          disabled={
            saving ||
            (mode === "create" && (!lineageId || !familyName || !givenName))
          }
          onClick={() => void (mode === "create" ? onCreate() : onEdit())}
        >
          {saving ? "保存中…" : "保存"}
        </button>
      </div>
    </Modal>
  );
}
