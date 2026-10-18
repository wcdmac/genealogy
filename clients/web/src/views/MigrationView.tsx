// 迁徙视图（M5-4）：迁徙登记 + 迁徙列表 + SVG 迁徙地图。
//
// 地图自绘不引地图库：迁徙是「从甲村到乙县」的谱面事实，
// 精度到点、量级到几十个地点，等距圆柱投影 + 箭头线足够表达，
// 引一整个地图库（及其离线瓦片问题）不划算。
// 无坐标的地点进「待补坐标」清单——地图只是辅助，记录才是事实。

import { useCallback, useEffect, useState } from "react";

import { useClient } from "../context";
import { ErrorBar, Modal } from "../components/Modal";
import type { Migration, Place } from "../api/client";

export function MigrationView() {
  const { client } = useClient();
  const [branchId, setBranchId] = useState("");
  const [subjectId, setSubjectId] = useState("");
  const [migrations, setMigrations] = useState<Migration[]>([]);
  const [places, setPlaces] = useState<Place[]>([]);
  const [error, setError] = useState<unknown>(null);
  const [showCreate, setShowCreate] = useState(false);
  const [showPlaceCreate, setShowPlaceCreate] = useState(false);

  const reload = useCallback(async () => {
    try {
      setError(null);
      const [migrationPage, placePage] = await Promise.all([
        client.listMigrations({
          branch_id: branchId.trim() || undefined,
          subject_id: subjectId.trim() || undefined,
          limit: 100,
        }),
        client.listPlaces({ limit: 200 }),
      ]);
      setMigrations((migrationPage.data ?? []) as unknown as Migration[]);
      setPlaces((placePage.data ?? []) as unknown as Place[]);
    } catch (caught) {
      setError(caught);
    }
  }, [client, branchId, subjectId]);

  useEffect(() => {
    void reload();
  }, [reload]);

  const placeById = new Map(places.map((place) => [place.id, place]));

  return (
    <section className="stack">
      <ErrorBar error={error} />

      <div className="toolbar">
        <input
          className="mono"
          placeholder="支派 ID（含成员迁徙）"
          value={branchId}
          onChange={(event) => setBranchId(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") void reload();
          }}
          size={26}
        />
        <input
          className="mono"
          placeholder="主体 ID（人或支派）"
          value={subjectId}
          onChange={(event) => setSubjectId(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") void reload();
          }}
          size={26}
        />
        <button onClick={() => void reload()}>查询</button>
        <span className="spacer" />
        <button onClick={() => setShowPlaceCreate(true)}>登记地点</button>
        <button className="primary" onClick={() => setShowCreate(true)}>
          登记迁徙
        </button>
      </div>

      <div className="panel">
        <h3>迁徙地图</h3>
        <MigrationMap migrations={migrations} places={places} />
        <p className="muted">
          箭头由迁出地指向迁入地；灰点 = 已登记但缺坐标的地点（记录本身
          不依赖地图，坐标可后补）。
        </p>
        {places.filter((place) => !place.lat || !place.lng).length > 0 && (
          <p className="muted">
            待补坐标：
            {places
              .filter((place) => !place.lat || !place.lng)
              .map((place) => place.name)
              .join("、")}
          </p>
        )}
      </div>

      <div className="panel">
        <h3>迁徙记录（{migrations.length}）</h3>
        <table className="grid">
          <thead>
            <tr>
              <th>主体</th>
              <th>由</th>
              <th>之</th>
              <th>日期</th>
              <th>迁因</th>
              <th>路线</th>
            </tr>
          </thead>
          <tbody>
            {migrations.map((migration) => (
              <tr key={migration.id}>
                <td className="mono">
                  {migration.subject_type}/{migration.subject_id.slice(0, 8)}
                </td>
                <td>{placeName(placeById, migration.from_place_id)}</td>
                <td>{placeName(placeById, migration.to_place_id)}</td>
                <td>{dateText(migration.date)}</td>
                <td>{migration.reason ?? "—"}</td>
                <td>{migration.route ?? "—"}</td>
              </tr>
            ))}
            {migrations.length === 0 && (
              <tr>
                <td colSpan={6} className="empty">
                  暂无迁徙记录。填上部筛选或直接「登记迁徙」。
                </td>
              </tr>
            )}
          </tbody>
        </table>
      </div>

      {showCreate && (
        <MigrationCreateDialog
          places={places}
          defaultSubjectId={subjectId.trim() || undefined}
          onClose={() => setShowCreate(false)}
          onCreated={() => {
            setShowCreate(false);
            void reload();
          }}
        />
      )}
      {showPlaceCreate && (
        <PlaceCreateDialog
          onClose={() => setShowPlaceCreate(false)}
          onCreated={() => {
            setShowPlaceCreate(false);
            void reload();
          }}
        />
      )}
    </section>
  );
}

function placeName(
  byId: Map<string, Place>,
  id?: string | null,
): string {
  if (!id) return "—";
  return byId.get(id)?.name ?? `${id.slice(0, 8)}…`;
}

// date 现在是 GenealogyDate 对象（与 event 同口径），取原文展示
function dateText(date?: {
  lunar?: string;
  gregorian?: string;
} | null): string {
  if (!date) return "—";
  return date.lunar ?? date.gregorian ?? "—";
}

/** SVG 迁徙地图：等距圆柱投影 + 箭头。 */
function MigrationMap({
  migrations,
  places,
}: {
  migrations: Migration[];
  places: Place[];
}) {
  const WIDTH = 720;
  const HEIGHT = 360;

  const located = places.filter(
    (place) => place.lat != null && place.lng != null,
  ) as Array<Place & { lat: number; lng: number }>;
  if (located.length === 0) {
    return (
      <p className="muted">
        没有带坐标的地点。先「登记地点」并填经纬度（谱牒常用故里的
        乡镇坐标即可，精度到分也不影响观感）。
      </p>
    );
  }

  const lats = located.map((place) => place.lat);
  const lngs = located.map((place) => place.lng);
  const minLat = Math.min(...lats);
  const maxLat = Math.max(...lats);
  const minLng = Math.min(...lngs);
  const maxLng = Math.max(...lngs);
  // 投影：x 正比于经度、y 反比于纬度；留 10% 边距，正方形保持比例
  const pad = 36;
  const spanX = Math.max(maxLng - minLng, 0.001);
  const spanY = Math.max(maxLat - minLat, 0.001);
  const scale = Math.min((WIDTH - 2 * pad) / spanX, (HEIGHT - 2 * pad) / spanY);
  const offsetX = (WIDTH - spanX * scale) / 2;
  const offsetY = (HEIGHT - spanY * scale) / 2;
  const project = (lat: number, lng: number) => ({
    x: offsetX + (lng - minLng) * scale,
    y: offsetY + (maxLat - lat) * scale,
  });

  const byId = new Map(located.map((place) => [place.id, place]));

  return (
    <svg width={WIDTH} height={HEIGHT} className="migration-map" role="img" aria-label="迁徙地图">
      <defs>
        <marker
          id="migration-arrowhead"
          markerWidth="8"
          markerHeight="8"
          refX="6"
          refY="3"
          orient="auto"
        >
          <path d="M0,0 L6,3 L0,6 Z" fill="var(--accent)" />
        </marker>
      </defs>
      {/* 简易经纬网：横向每 5 度一条纬线，只作方位参照 */}
      {Array.from({ length: 12 }, (_, index) => index * 5).map((step) => {
        const y = offsetY + (step / 60) * spanY * scale;
        return (
          <line key={`h${step}`} x1={pad / 2} y1={y} x2={WIDTH - pad / 2} y2={y} className="map-grid" />
        );
      })}
      {migrations.map((migration) => {
        const from = migration.from_place_id ? byId.get(migration.from_place_id) : undefined;
        const to = migration.to_place_id ? byId.get(migration.to_place_id) : undefined;
        if (!from || !to) return null;  // 缺坐标的端不画（列表里仍有记录）
        const a = project(from.lat, from.lng);
        const b = project(to.lat, to.lng);
        return (
          <g key={migration.id}>
            <line
              x1={a.x}
              y1={a.y}
              x2={b.x}
              y2={b.y}
              className="map-arrow"
              markerEnd="url(#migration-arrowhead)"
            >
              <title>{`${from.name} → ${to.name}`}</title>
            </line>
          </g>
        );
      })}
      {located.map((place) => {
        const point = project(place.lat, place.lng);
        return (
          <g key={place.id}>
            <circle cx={point.x} cy={point.y} r={4} className="map-place" />
            <text x={point.x + 7} y={point.y + 4} className="map-label">
              {place.name}
            </text>
          </g>
        );
      })}
    </svg>
  );
}

function MigrationCreateDialog({
  places,
  defaultSubjectId,
  onClose,
  onCreated,
}: {
  places: Place[];
  defaultSubjectId?: string;
  onClose: () => void;
  onCreated: () => void;
}) {
  const { client } = useClient();
  const [subjectType, setSubjectType] = useState<"person" | "branch">("person");
  const [subjectId, setSubjectId] = useState(defaultSubjectId ?? "");
  const [toPlaceId, setToPlaceId] = useState("");
  const [fromPlaceId, setFromPlaceId] = useState("");
  const [reason, setReason] = useState("");
  const [route, setRoute] = useState("");
  const [error, setError] = useState<unknown>(null);

  async function onSubmit() {
    try {
      await client.createMigration({
        subject_type: subjectType,
        subject_id: subjectId.trim(),
        to_place_id: toPlaceId,
        from_place_id: fromPlaceId || undefined,
        reason: reason || undefined,
        route: route || undefined,
      });
      onCreated();
    } catch (caught) {
      setError(caught);
    }
  }

  return (
    <Modal title="登记迁徙" onClose={onClose} width={520}>
      <ErrorBar error={error} />
      <div className="field-row">
        <label className="field">
          <span className="field-label">主体类型</span>
          <select
            value={subjectType}
            onChange={(event) => setSubjectType(event.target.value as typeof subjectType)}
          >
            <option value="person">个人</option>
            <option value="branch">支派</option>
          </select>
        </label>
        <label className="field">
          <span className="field-label">主体 ID</span>
          <input
            className="mono"
            value={subjectId}
            onChange={(event) => setSubjectId(event.target.value)}
          />
        </label>
      </div>
      <label className="field">
        <span className="field-label">迁入地</span>
        <select value={toPlaceId} onChange={(event) => setToPlaceId(event.target.value)}>
          <option value="">（选择地点）</option>
          {places.map((place) => (
            <option key={place.id} value={place.id}>
              {place.name}
              {place.modern_admin ? `（${place.modern_admin}）` : ""}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span className="field-label">迁出地<span className="field-hint">可空</span></span>
        <select value={fromPlaceId} onChange={(event) => setFromPlaceId(event.target.value)}>
          <option value="">（不填）</option>
          {places.map((place) => (
            <option key={place.id} value={place.id}>
              {place.name}
            </option>
          ))}
        </select>
      </label>
      <label className="field">
        <span className="field-label">迁因</span>
        <input
          value={reason}
          onChange={(event) => setReason(event.target.value)}
          placeholder="如：避乱 / 营商 / 仕进 / 投亲"
        />
      </label>
      <label className="field">
        <span className="field-label">路线<span className="field-hint">文字描述</span></span>
        <input
          value={route}
          onChange={(event) => setRoute(event.target.value)}
          placeholder="如：中湘 → 洞庭 → 渝州"
        />
      </label>
      <div className="modal-actions">
        <button onClick={onClose}>取消</button>
        <button
          className="primary"
          disabled={!subjectId.trim() || !toPlaceId}
          onClick={() => void onSubmit()}
        >
          登记
        </button>
      </div>
    </Modal>
  );
}

function PlaceCreateDialog({
  onClose,
  onCreated,
}: {
  onClose: () => void;
  onCreated: () => void;
}) {
  const { client } = useClient();
  const [name, setName] = useState("");
  const [modernAdmin, setModernAdmin] = useState("");
  const [historicalName, setHistoricalName] = useState("");
  const [lat, setLat] = useState("");
  const [lng, setLng] = useState("");
  const [error, setError] = useState<unknown>(null);

  async function onSubmit() {
    try {
      await client.createPlace({
        name,
        modern_admin: modernAdmin || undefined,
        historical_name: historicalName || undefined,
        lat: lat ? Number(lat) : undefined,
        lng: lng ? Number(lng) : undefined,
      });
      onCreated();
    } catch (caught) {
      setError(caught);
    }
  }

  return (
    <Modal title="登记地点" onClose={onClose} width={460}>
      <ErrorBar error={error} />
      <label className="field">
        <span className="field-label">地名</span>
        <input value={name} onChange={(event) => setName(event.target.value)} />
      </label>
      <label className="field">
        <span className="field-label">现代政区<span className="field-hint">对照用</span></span>
        <input
          value={modernAdmin}
          onChange={(event) => setModernAdmin(event.target.value)}
          placeholder="如：湖南省湘潭县"
        />
      </label>
      <label className="field">
        <span className="field-label">历史别名<span className="field-hint">历代沿革名</span></span>
        <input
          value={historicalName}
          onChange={(event) => setHistoricalName(event.target.value)}
        />
      </label>
      <div className="field-row">
        <label className="field">
          <span className="field-label">纬度 lat</span>
          <input
            value={lat}
            inputMode="decimal"
            onChange={(event) => setLat(event.target.value)}
            placeholder="27.83"
          />
        </label>
        <label className="field">
          <span className="field-label">经度 lng</span>
          <input
            value={lng}
            inputMode="decimal"
            onChange={(event) => setLng(event.target.value)}
            placeholder="112.94"
          />
        </label>
      </div>
      <div className="modal-actions">
        <button onClick={onClose}>取消</button>
        <button className="primary" disabled={!name} onClick={() => void onSubmit()}>
          登记
        </button>
      </div>
    </Modal>
  );
}
