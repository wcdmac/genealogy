// 世系图视图：getPersonTree → SVG 分层渲染。
//
// 布局思路（不引图形库）：以选定人物为根做 BFS，世代决定纵坐标、
// 同代按出生顺序排横坐标；婚姻边用横向虚线接配偶。点击任一节点
// 即以该人为新根重画——「从任何一代俯瞰全支」。
//
// 节点标识一律用人物 ID（server 的 DOT/Mermaid 导出同理）：
// 同姓 + 派语字决定了重名率，用姓名当 key 会把两个人并成一个点。

import { useCallback, useMemo, useState } from "react";

import { useClient } from "../context";
import { ErrorBar } from "../components/Modal";
import type { Person } from "../api/client";

interface GraphEdge {
  from: string;
  to: string;
  type: "birth" | "adoption" | "marriage";
}

interface GraphData {
  mode?: "five_generation" | "full";
  nodes: Person[];
  edges: GraphEdge[];
}

const NODE_W = 120;
const NODE_H = 44;
const ROW_H = 130;

export function TreeView() {
  const { client } = useClient();
  const [rootId, setRootId] = useState("");
  const [mode, setMode] = useState<"five_generation" | "full">("full");
  const [depth, setDepth] = useState(6);
  const [graph, setGraph] = useState<GraphData | null>(null);
  const [error, setError] = useState<unknown>(null);

  const load = useCallback(
    async (id: string) => {
      if (!id.trim()) return;
      try {
        setError(null);
        const data = (await client.getPersonTree(id.trim(), {
          mode,
          depth,
        })) as GraphData;
        setGraph(data);
      } catch (caught) {
        setError(caught);
        setGraph(null);
      }
    },
    [client, mode, depth],
  );

  return (
    <section>
      <div className="toolbar">
        <input
          className="mono"
          placeholder="中心人物 ID"
          value={rootId}
          onChange={(event) => setRootId(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === "Enter") void load(rootId);
          }}
          size={28}
        />
        <select
          aria-label="模式"
          value={mode}
          onChange={(event) => setMode(event.target.value as typeof mode)}
        >
          <option value="full">全量</option>
          <option value="five_generation">五世一图</option>
        </select>
        <label className="inline-field">
          深度
          <input
            type="number"
            min={1}
            max={20}
            value={depth}
            onChange={(event) => setDepth(Number(event.target.value))}
            style={{ width: 60 }}
          />
        </label>
        <button className="primary" onClick={() => void load(rootId)}>
          绘制
        </button>
      </div>

      <ErrorBar error={error} />

      {graph && graph.nodes.length > 0 ? (
        <TreeGraph
          graph={graph}
          onPickRoot={(id) => {
            setRootId(id);
            void load(id);
          }}
        />
      ) : (
        <p className="muted">
          输入中心人物 ID 绘制世系图。ID 可在「人物」视图的列表里复制。
        </p>
      )}
    </section>
  );
}

function TreeGraph({
  graph,
  onPickRoot,
}: {
  graph: GraphData;
  onPickRoot: (id: string) => void;
}) {
  const layout = useMemo(() => layoutTree(graph), [graph]);
  const byId = new Map(graph.nodes.map((node) => [node.id, node]));

  return (
    <div className="tree-scroll">
      <svg
        width={Math.max(layout.width, 320)}
        height={Math.max(layout.height, 200)}
        role="img"
        aria-label="世系图"
      >
        {/* 婚姻边先画，压在亲子边下面 */}
        {layout.marriageEdges.map((edge) => (
          <line
            key={`m-${edge.from}-${edge.to}`}
            x1={edge.x1}
            y1={edge.y1}
            x2={edge.x2}
            y2={edge.y2}
            className="edge-marriage"
          />
        ))}
        {layout.parentEdges.map((edge) => (
          <line
            key={`p-${edge.from}-${edge.to}`}
            x1={edge.x1}
            y1={edge.y1}
            x2={edge.x2}
            y2={edge.y2}
            className="edge-birth"
          />
        ))}
        {layout.placed.map((node) => {
          const person = byId.get(node.id);
          return (
            <g
              key={node.id}
              transform={`translate(${node.x} ${node.y})`}
              onClick={() => onPickRoot(node.id)}
              className="tree-node"
            >
              <rect width={NODE_W} height={NODE_H} rx={6} className="node-box" />
              <text x={10} y={18} className="node-name">
                {person ? `${person.family_name}${person.given_name}` : node.id}
              </text>
              <text x={10} y={34} className="node-meta">
                {person?.generation_no != null
                  ? `${person.generation_no} 世`
                  : ""}
                {person?.is_living ? " · 在世" : ""}
              </text>
            </g>
          );
        })}
      </svg>
      <p className="muted">
        共 {graph.nodes.length} 人、{layout.parentEdges.length + layout.marriageEdges.length} 条关系；
        点击节点以其为中心重绘。
      </p>
    </div>
  );
}

interface PlacedNode {
  id: string;
  x: number;
  y: number;
}
interface LayoutEdge {
  from: string;
  to: string;
  x1: number;
  y1: number;
  x2: number;
  y2: number;
}
interface TreeLayout {
  placed: PlacedNode[];
  parentEdges: LayoutEdge[];
  marriageEdges: LayoutEdge[];
  width: number;
  height: number;
}

function layoutTree(graph: GraphData): TreeLayout {
  const nodes = graph.nodes;
  if (nodes.length === 0) {
    return { placed: [], parentEdges: [], marriageEdges: [], width: 0, height: 0 };
  }
  const idSet = new Set(nodes.map((node) => node.id));

  // 亲子边（出生/收养）：from=父, to=子
  const parentEdges: LayoutEdge[] = [];
  const marriageEdges: LayoutEdge[] = [];
  for (const edge of graph.edges) {
    if (edge.type === "marriage") {
      if (idSet.has(edge.from) && idSet.has(edge.to)) {
        marriageEdges.push({ ...edge, x1: 0, y1: 0, x2: 0, y2: 0 });
      }
      continue;
    }
    if (idSet.has(edge.from) && idSet.has(edge.to)) {
      parentEdges.push({ ...edge, x1: 0, y1: 0, x2: 0, y2: 0 });
    }
  }

  // BFS 分层：根 = nodes[0]（server 以请求的人物为根返回）
  const depthOf = new Map<string, number>();
  const childrenOf = new Map<string, string[]>();
  for (const edge of parentEdges) {
    const list = childrenOf.get(edge.from) ?? [];
    list.push(edge.to);
    childrenOf.set(edge.from, list);
  }
  const root = nodes[0].id;
  depthOf.set(root, 0);
  const queue = [root];
  while (queue.length > 0) {
    const current = queue.shift()!;
    const depth = depthOf.get(current)!;
    for (const child of childrenOf.get(current) ?? []) {
      if (!depthOf.has(child)) {
        depthOf.set(child, depth + 1);
        queue.push(child);
      }
    }
  }
  // 未连通节点（理论上 server 不会返回）挂到根旁一层
  for (const node of nodes) {
    if (!depthOf.has(node.id)) depthOf.set(node.id, 1);
  }

  const rows = new Map<number, Person[]>();
  for (const node of nodes) {
    const depth = depthOf.get(node.id)!;
    const row = rows.get(depth) ?? [];
    row.push(node);
    rows.set(depth, row);
  }

  const placed: PlacedNode[] = [];
  const posOf = new Map<string, { x: number; y: number }>();
  const rowWidths = [...rows.entries()].sort((a, b) => a[0] - b[0]);
  const maxRowSize = Math.max(...rowWidths.map(([, row]) => row.length), 1);
  const width = maxRowSize * (NODE_W + 40) + 40;
  const height = rowWidths.length * ROW_H + NODE_H + 20;

  for (const [depth, row] of rowWidths) {
    const rowWidth = row.length * (NODE_W + 40) - 40;
    const startX = (width - rowWidth) / 2;
    row.forEach((person, index) => {
      const x = startX + index * (NODE_W + 40);
      const y = depth * ROW_H + 20;
      placed.push({ id: person.id, x, y });
      posOf.set(person.id, { x: x + NODE_W / 2, y: y + NODE_H / 2 });
    });
  }

  for (const edge of parentEdges) {
    const from = posOf.get(edge.from);
    const to = posOf.get(edge.to);
    if (!from || !to) continue;
    edge.x1 = from.x;
    edge.y1 = from.y + NODE_H / 2;
    edge.x2 = to.x;
    edge.y2 = to.y - NODE_H / 2;
  }
  for (const edge of marriageEdges) {
    const from = posOf.get(edge.from);
    const to = posOf.get(edge.to);
    if (!from || !to) continue;
    edge.x1 = from.x + NODE_W / 2;
    edge.y1 = from.y;
    edge.x2 = to.x - NODE_W / 2;
    edge.y2 = to.y;
  }

  return { placed, parentEdges, marriageEdges, width, height };
}
