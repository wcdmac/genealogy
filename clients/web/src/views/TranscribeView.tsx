// 谱师转录工作台：老谱扫描件 → 结构化人物数据。
//
// 面向任何竖排线装家谱的欧式行传转录（不绑定特定谱）——谱的特异性
// （派语、房支、人物）全部是录入数据，不是代码。
//
// 三块能力：
//   1. 文档查看：PDF（pdfjs）或图片，页导航/缩放/旋转，列裁切放大
//      ——竖排古籍的列级目验是可靠转录的前提（试修方法边界实证）；
//   2. 行传条目表单：世/名/字/号/生卒原文/配氏(妻·侧室)/生子队列，
//      「生子」自动预填下一条的父亲（欧式支连书规律）；出嗣/存疑/
//      出处页码强制留痕；
//   3. 暂存队列 → 批量入库（REST），重复名预警，全库校验回显。
//
// 录入纪律（源自高阳李氏试修）：原文照录不猜（日期进 lunar 字段）、
// 推定项显式标注、每条必带出处页码。

import { useEffect, useRef, useState } from "react";
import * as pdfjsLib from "pdfjs-dist";
import workerUrl from "pdfjs-dist/build/pdf.worker.min.mjs?url";

import { useClient } from "../context";
import { ErrorBar } from "../components/Modal";
import type { Lineage } from "../api/client";

pdfjsLib.GlobalWorkerOptions.workerSrc = workerUrl;

// ---------------------------------------------------------------- 类型

interface WifeRow {
  surname: string;                       // 妻之姓（录入后建「X氏」）
  role: "wife" | "concubine";            // 妻 / 侧室
}

interface SonRow {
  name: string;                          // 名讳
  motherSurname: string;                 // 所出（配某氏），可空
}

interface Staged {
  key: string;
  generation_no: number;
  given_name: string;
  courtesy: string;                      // 字
  art: string;                           // 号
  birth_lunar: string;                   // 谱面原文照录
  death_lunar: string;
  fatherKey: string | null;              // 暂存条目 key（欧式连书）
  fatherName: string;                    // 显示用（含自由跳录输入）
  page: number;                          // 出处页码（当前查看页自动带入）
  doubtful: boolean;                     // 存疑
  chuSi: boolean;                        // 出嗣
  noteExtra: string;
  wives: WifeRow[];
  sons: SonRow[];
  status: "staged" | "committed" | "failed";
  committedId?: string;
  message?: string;
}

let STAGED_SEQ = 0;
function nextKey(): string {
  STAGED_SEQ += 1;
  return `s${Date.now()}-${STAGED_SEQ}`;
}

// ---------------------------------------------------------------- 组件

export function TranscribeView() {
  const { client } = useClient();

  // ---- 宗族 / 支派选择
  const [lineages, setLineages] = useState<Lineage[]>([]);
  const [lineageId, setLineageId] = useState("");
  const [branches, setBranches] = useState<
    Array<{ id: string; name: string }>
  >([]);
  const [branchId, setBranchId] = useState("");

  // ---- 文档查看
  const [pdfDoc, setPdfDoc] = useState<pdfjsLib.PDFDocumentProxy | null>(null);
  const [imgSrc, setImgSrc] = useState<string | null>(null);
  const [pageNo, setPageNo] = useState(1);
  const [totalPages, setTotalPages] = useState(0);
  const [zoom, setZoom] = useState(1.1);
  const [rotation, setRotation] = useState(0);
  const [viewerError, setViewerError] = useState<string | null>(null);
  const [rendering, setRendering] = useState(false);
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const cropRef = useRef<HTMLCanvasElement>(null);
  // pdfjs 渲染任务句柄：连续翻页时取消在途渲染——两个 render 叠写同一
  // canvas 会得到白页（快进 170 页实测），必须串行化。
  const renderTaskRef = useRef<{ cancel: () => void } | null>(null);
  const dragRef = useRef<{ x: number; y: number } | null>(null);
  const [sel, setSel] = useState<{
    x: number; y: number; w: number; h: number;
  } | null>(null);

  // ---- 条目表单
  const [form, setForm] = useState<{
    generation_no: number;
    given_name: string;
    courtesy: string;
    art: string;
    birth_lunar: string;
    death_lunar: string;
    fatherName: string;
    doubtful: boolean;
    chuSi: boolean;
    noteExtra: string;
    pageOverride: string;
  }>({
    generation_no: 1,
    given_name: "",
    courtesy: "",
    art: "",
    birth_lunar: "",
    death_lunar: "",
    fatherName: "",
    doubtful: false,
    chuSi: false,
    noteExtra: "",
    pageOverride: "",
  });
  const [wives, setWives] = useState<WifeRow[]>([]);
  const [sons, setSons] = useState<SonRow[]>([]);
  const [staged, setStaged] = useState<Staged[]>([]);
  // 欧式连书链（2026-11 静态审查 H9）：formKeyRef = 当前表单人入库时
  // 将使用的 key（「生子」要在此人暂存前引用它）；formFather = 表单人
  // 的父亲（key + 名，用于暂存时挂 fatherKey）。此前暂存路径 fatherKey
  // 恒为 null、「生子」按钮传伪 key "local"——父子关系整条断链。
  const formKeyRef = useRef(nextKey());
  const [formFather, setFormFather] = useState<{
    key: string | null;
    name: string;
  }>({ key: null, name: "" });

  // ---- 提交 / 校验
  const [committing, setCommitting] = useState(false);
  const [formError, setFormError] = useState<string | null>(null);
  const [validateOut, setValidateOut] = useState<
    Awaited<ReturnType<typeof client.runValidation>> | null
  >(null);
  const [validating, setValidating] = useState(false);

  const committedIds = new Map<string, string>();
  for (const s of staged) {
    if (s.committedId) committedIds.set(s.key, s.committedId);
  }

  // ---- 宗族列表
  useEffect(() => {
    // signal + aborted 守卫（M13 stale-response guard）：在途响应不再
    // 覆盖新状态；abort 引起的失败静默
    const controller = new AbortController();
    client
      .listLineages({ limit: 50 }, { signal: controller.signal })
      .then((page) => {
        if (controller.signal.aborted) return;
        const rows = (page.data ?? []) as unknown as Lineage[];
        setLineages(rows);
        if (rows.length > 0 && !lineageId) setLineageId(rows[0].id);
      })
      .catch(() => undefined);
    return () => controller.abort();
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [client]);

  // useClient 的 connected 状态由外层 App 保证进入本视图时已连接
  //（原 connectionReady 前置判断恒为 true，effect 改造后已无存在必要）

  // ---- 支派列表（选定宗族后拉取）
  useEffect(() => {
    if (!lineageId) {
      setBranches([]);
      return;
    }
    // signal + aborted 守卫：切换宗族时在途响应作废
    const controller = new AbortController();
    client
      .request("GET", "/branches", {
        query: { limit: 100 },
        signal: controller.signal,
      })
      .then((page: unknown) => {
        if (controller.signal.aborted) return;
        const rows =
          (page as { data?: Array<{ id: string; name: string }> }).data ?? [];
        setBranches(rows);
      })
      .catch(() => {
        // abort 引起的失败不清空（已切到别的宗族，别覆盖新请求）
        if (!controller.signal.aborted) setBranches([]);
      });
    return () => controller.abort();
  }, [client, lineageId]);

  // ---- 文档渲染（PDF 页 / 图片）
  useEffect(() => {
    let cancelled = false;
    (async () => {
      const canvas = canvasRef.current;
      if (!canvas) return;
      const ctx = canvas.getContext("2d");
      if (!ctx) return;
      setRendering(true);
      setViewerError(null);
      try {
        if (pdfDoc) {
          if (renderTaskRef.current) {
            try {
              renderTaskRef.current.cancel();
            } catch {
              // 已完成的任务取消会抛——忽略
            }
            renderTaskRef.current = null;
          }
          const page = await pdfDoc.getPage(pageNo);
          const viewport = page.getViewport({ scale: zoom, rotation });
          canvas.width = Math.floor(viewport.width);
          canvas.height = Math.floor(viewport.height);
          ctx.fillStyle = "#ffffff";
          ctx.fillRect(0, 0, canvas.width, canvas.height);
          const task = page.render({ canvas, viewport });
          renderTaskRef.current = task;
          await task.promise;
          renderTaskRef.current = null;
        } else if (imgSrc) {
          const img = new Image();
          img.src = imgSrc;
          await img.decode();
          const rot = ((rotation % 360) + 360) % 360;
          const swap = rot === 90 || rot === 270;
          canvas.width = Math.floor(
            (swap ? img.height : img.width) * zoom,
          );
          canvas.height = Math.floor(
            (swap ? img.width : img.height) * zoom,
          );
          ctx.fillStyle = "#ffffff";
          ctx.fillRect(0, 0, canvas.width, canvas.height);
          ctx.save();
          ctx.translate(canvas.width / 2, canvas.height / 2);
          ctx.rotate((rot * Math.PI) / 180);
          ctx.drawImage(
            img,
            (-img.width * zoom) / 2,
            (-img.height * zoom) / 2,
            img.width * zoom,
            img.height * zoom,
          );
          ctx.restore();
        }
        // 新页渲染后清掉旧选区/裁切
        setSel(null);
        const crop = cropRef.current;
        if (crop) {
          const cctx = crop.getContext("2d");
          cctx?.clearRect(0, 0, crop.width, crop.height);
        }
      } catch (e) {
        if (!cancelled) setViewerError(e instanceof Error ? e.message : String(e));
      } finally {
        if (!cancelled) setRendering(false);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [pdfDoc, imgSrc, pageNo, zoom, rotation]);

  // ---- 资源释放（第三轮新-W7）：换文件/卸载时释放旧 PDF 文档句柄与
  // blob URL——此前换一次文件泄漏一个 blob，长会话转录几十页后内存
  // 与句柄持续攀升
  useEffect(() => {
    return () => {
      if (imgSrc) URL.revokeObjectURL(imgSrc);
      // v6 的销毁入口在 loadingTask 上（PDFDocumentProxy 本体只有 cleanup）
      if (pdfDoc) void pdfDoc.loadingTask.destroy().catch(() => undefined);
    };
  }, [pdfDoc, imgSrc]);

  // ---- 文件加载
  async function onFile(file: File) {
    setViewerError(null);
    setPdfDoc(null);
    setImgSrc(null);
    setSel(null);
    if (file.type === "application/pdf" || file.name.endsWith(".pdf")) {
      const buf = await file.arrayBuffer();
      const doc = await pdfjsLib.getDocument({
        data: buf,
        // JPEG2000/JBIG2 解码需要 OpenJPEG/JBIG2 WASM（上图扫描件即
        // JPEG2000——缺 wasmUrl 会整页白板，实测）。public/pdfjs-wasm/
        // 由 node_modules/pdfjs-dist/wasm 原样拷贝，升级依赖时需同步。
        wasmUrl: new URL("pdfjs-wasm/", document.baseURI).href,
      }).promise;
      setPdfDoc(doc);
      setTotalPages(doc.numPages);
      setPageNo(1);
    } else if (file.type.startsWith("image/")) {
      const url = URL.createObjectURL(file);
      const img = new Image();
      img.src = url;
      await img.decode();
      setImgSrc(url);
      setTotalPages(1);
      setPageNo(1);
    } else {
      setViewerError("仅支持 PDF 或图片文件");
    }
  }

  // ---- 列裁切：拖选 → 放大窗
  function toCanvasXY(event: React.PointerEvent<HTMLCanvasElement>) {
    const canvas = canvasRef.current;
    if (!canvas) return { x: 0, y: 0 };
    const rect = canvas.getBoundingClientRect();
    return {
      x: ((event.clientX - rect.left) * canvas.width) / rect.width,
      y: ((event.clientY - rect.top) * canvas.height) / rect.height,
    };
  }

  function onPointerDown(event: React.PointerEvent<HTMLCanvasElement>) {
    const p = toCanvasXY(event);
    dragRef.current = p;
    setSel({ x: p.x, y: p.y, w: 0, h: 0 });
    event.currentTarget.setPointerCapture(event.pointerId);
  }

  function onPointerMove(event: React.PointerEvent<HTMLCanvasElement>) {
    if (!dragRef.current) return;
    const p = toCanvasXY(event);
    const start = dragRef.current;
    setSel({
      x: Math.min(start.x, p.x),
      y: Math.min(start.y, p.y),
      w: Math.abs(p.x - start.x),
      h: Math.abs(p.y - start.y),
    });
  }

  function onPointerUp() {
    dragRef.current = null;
    updateCrop();
  }

  function updateCrop() {
    const src = canvasRef.current;
    const dst = cropRef.current;
    if (!src || !dst || !sel || sel.w < 8 || sel.h < 8) return;
    const ctx = dst.getContext("2d");
    if (!ctx) return;
    const factor = 3;
    dst.width = Math.floor(Math.min(sel.w * factor, 1600));
    dst.height = Math.floor((sel.h * dst.width) / sel.w);
    ctx.imageSmoothingEnabled = false;
    ctx.drawImage(src, sel.x, sel.y, sel.w, sel.h, 0, 0, dst.width, dst.height);
  }

  // ---- 条目暂存
  function currentPage(): number {
    const override = Number(form.pageOverride);
    return Number.isFinite(override) && override > 0 ? override : pageNo;
  }

  function stageCurrent() {
    if (!form.given_name.trim()) {
      setFormError("名讳必填");
      return;
    }
    if (!lineageId) {
      setFormError("先选择宗族");
      return;
    }
    const stagedKey = formKeyRef.current;
    // 父亲解析：表单父亲名与链上父亲一致 → 用链上 key；手动改过名字
    // （自由跳录）→ 置 null，提交时按名回查（本批暂存 → 库内检索，
    // commitAll 里实现）
    const fatherName = form.fatherName.trim();
    const fatherKey =
      fatherName !== "" && formFather.name === fatherName
        ? formFather.key
        : null;
    const entry: Staged = {
      key: stagedKey,
      generation_no: form.generation_no,
      given_name: form.given_name.trim(),
      courtesy: form.courtesy.trim(),
      art: form.art.trim(),
      birth_lunar: form.birth_lunar.trim(),
      death_lunar: form.death_lunar.trim(),
      fatherKey,
      fatherName,
      page: currentPage(),
      doubtful: form.doubtful,
      chuSi: form.chuSi,
      noteExtra: form.noteExtra.trim(),
      wives: wives.filter((w) => w.surname.trim() !== ""),
      sons: sons.filter((s) => s.name.trim() !== ""),
      status: "staged",
    };
    setStaged((prev) => [...prev, entry]);
    // 欧式连书：默认以本条第一条「生子」续录——表单钥匙换新、父亲
    // 指向刚暂存的这条
    formKeyRef.current = nextKey();
    setFormFather({ key: stagedKey, name: entry.given_name });
    const firstSon = entry.sons[0];
    setForm((f) => ({
      ...f,
      given_name: firstSon?.name ?? "",
      courtesy: "",
      art: "",
      birth_lunar: "",
      death_lunar: "",
      fatherName: entry.given_name,
      generation_no: entry.generation_no + 1,
    }));
    setWives([]);
    setSons([]);
    setFormError(null);
  }

  // 「生子」名 → 直接生成下一条暂存（父亲预填）
  function spawnSon(father: Staged, son: SonRow) {
    const entry: Staged = {
      key: nextKey(),
      generation_no: father.generation_no + 1,
      given_name: son.name,
      courtesy: "",
      art: "",
      birth_lunar: "",
      death_lunar: "",
      fatherKey: father.key,
      fatherName: father.given_name,
      page: father.page,
      doubtful: false,
      chuSi: false,
      noteExtra: son.motherSurname ? `${son.motherSurname}氏所出` : "",
      wives: [],
      sons: [],
      status: "staged",
    };
    setStaged((prev) => [...prev, entry]);
  }

  function removeStaged(key: string) {
    setStaged((prev) => prev.filter((e) => e.key !== key));
  }

  // ---- 批量入库
  async function commitAll() {
    setCommitting(true);
    setFormError(null);
    try {
      // 父条目必须先入库（欧式连书）：按世代号升序提交——「生子」在
      // 其父暂存前生成，插入序不能保证父子先后
      const pending = staged
        .filter((e) => e.status === "staged")
        .map((e, i) => ({ e, i }))
        .sort((a, b) => a.e.generation_no - b.e.generation_no || a.i - b.i)
        .map((x) => x.e);
      const failedKeys = new Set<string>();
      // 失败跳过的统一出口：标记条目 + 记入 failedKeys（依赖它的子孙
      // 由父亲守卫拦下）
      const failEntry = (victim: Staged, why: string) => {
        failedKeys.add(victim.key);
        setStaged((prev) =>
          prev.map((e) =>
            e.key === victim.key ? { ...e, status: "failed", message: why } : e,
          ),
        );
      };
      for (const entry of pending) {
        // 父亲回查（复核 2.11）：手动改过父亲名的条目（fatherKey = null、
        // fatherName 非空）提交时按名回查——先本批暂存（连书改名的父亲
        // 通常就是前一条；同名者优先取上一世；此前批次已入库的条目带
        // committedId，committedIds 同样按 key 收录），再查库。命中多条
        // 或两边都没有都显式失败跳过，绝不静默置 null 丢父子关系。
        let fatherKey = entry.fatherKey;
        let fatherIdDirect: string | null = null;
        if (fatherKey == null && entry.fatherName !== "") {
          const candidates = staged.filter(
            (e) => e.given_name === entry.fatherName && e.key !== entry.key,
          );
          const prevGen = candidates.filter(
            (e) => e.generation_no === entry.generation_no - 1,
          );
          const pool = prevGen.length > 0 ? prevGen : candidates;
          if (pool.length === 1) {
            fatherKey = pool[0].key;
          } else if (pool.length > 1) {
            failEntry(
              entry,
              `父亲名「${entry.fatherName}」在本批命中 ${pool.length} 条，请修正名字或改用连书链`,
            );
            continue;
          } else {
            // 暂存没有 → 库内回查（与重复名预警同款检索，精确比对名讳）
            try {
              const found = (await client.search({
                q: entry.fatherName,
                lineage_id: lineageId ?? undefined,
                limit: 5,
              })) as { data?: Array<{ given_name?: string; id?: string }> };
              const hits = (found.data ?? []).filter(
                (p) => p.given_name === entry.fatherName,
              );
              if (hits.length === 1 && hits[0].id) {
                fatherIdDirect = hits[0].id;
              } else if (hits.length > 1) {
                failEntry(
                  entry,
                  `父亲名「${entry.fatherName}」在库中命中 ${hits.length} 人，父子关系未建立——请先确认是哪一位`,
                );
                continue;
              } else {
                failEntry(
                  entry,
                  `父亲「${entry.fatherName}」不在暂存与库中，父子关系未建立`,
                );
                continue;
              }
            } catch {
              failEntry(
                entry,
                `父名「${entry.fatherName}」检索失败，请重试或改用连书链`,
              );
              continue;
            }
          }
        }
        // 父条目尚未入库：标记失败并跳过（不再中断整批）——前序失败
        // 的子孙同样跳过，绝不造孤儿（2026-11 静态审查 H9）。回查命中
        // 的 key 走同一条守卫。
        if (fatherKey && !committedIds.get(fatherKey)) {
          failEntry(
            entry,
            failedKeys.has(fatherKey)
              ? "父条目未入库（前序失败），跳过"
              : "父条目尚未入库",
          );
          continue;
        }
        // 重复名预警（同宗族同名）：不阻断，附在条目消息里
        let dupWarn = "";
        try {
          // 重复名预警限定本谱：多谱同库下别家的同名者不是重复
          //（2026-11-21 多谱隔离审计）
          const found = (await client.search({
            q: entry.given_name,
            lineage_id: lineageId ?? undefined,
            limit: 5,
          })) as { data?: Array<{ given_name?: string }> };
          const hits = (found.data ?? []).filter(
            (p) => p.given_name === entry.given_name,
          );
          if (hits.length > 0) {
            dupWarn = `（同库已有 ${hits.length} 位同名「${entry.given_name}」，确认非同一人再提交）`;
          }
        } catch {
          // 检索失败不阻断录入
        }
        try {
          const noteBits: string[] = [];
          noteBits.push(`谱p${entry.page}`);
          if (entry.doubtful) noteBits.push("[存疑，待核原卷]");
          if (entry.chuSi) noteBits.push("[出嗣]");
          if (entry.noteExtra) noteBits.push(entry.noteExtra);
          if (dupWarn) noteBits.push(dupWarn);
          // 库内回查直接命中的 id 优先（该父亲不在本批暂存里）；
          // 其余走 fatherKey → committedIds 常规链
          const fatherId =
            fatherIdDirect ??
            (fatherKey != null
              ? (committedIds.get(fatherKey) ?? null)
              : null);
          const created = await client.createPerson({
            lineage_id: lineageId,
            branch_id: branchId || undefined,
            family_name: familyName(),
            given_name: entry.given_name,
            gender: "male",
            generation_no: entry.generation_no,
            father_id: fatherId ?? undefined,
            courtesy_name: entry.courtesy || undefined,
            art_name: entry.art || undefined,
            biography: [entry.birth_lunar && `生${entry.birth_lunar}`,
                        entry.death_lunar && `卒${entry.death_lunar}`]
              .filter(Boolean)
              .join("，") || undefined,
            note: noteBits.join(" ") || undefined,
            birth_date: entry.birth_lunar
              ? { lunar: entry.birth_lunar }
              : undefined,
            death_date: entry.death_lunar
              ? { lunar: entry.death_lunar }
              : undefined,
          } as Parameters<typeof client.createPerson>[0]);
          committedIds.set(entry.key, created.id);
          // 配氏：建人 + 结合（妻 → marriage；侧室 → concubinage）
          let seq = 1;
          for (const w of entry.wives) {
            const wife = await client.createPerson({
              lineage_id: lineageId,
              family_name: w.surname,
              given_name: "氏",
              gender: "female",
              note: `谱p${entry.page}`,
            });
            await client.createUnion({
              type: w.role === "concubine" ? "concubinage" : "marriage",
              sequence: w.role === "wife" ? seq : undefined,
              members: [
                { person_id: created.id, role: "husband" },
                { person_id: wife.id, role: w.role === "concubine" ? "concubine" : "wife" },
              ],
            });
            if (w.role === "wife") seq += 1;
          }
          setStaged((prev) =>
            prev.map((e) =>
              e.key === entry.key
                ? {
                    ...e,
                    status: "committed",
                    committedId: created.id,
                    message: dupWarn || undefined,
                  }
                : e,
            ),
          );
        } catch (e) {
          const message = e instanceof Error ? e.message : String(e);
          // 失败跳过不中断：后续无依赖条目照常入库，依赖它的子孙由
          // failedKeys 守卫拦下（此前 break 会丢弃整批后半段）
          failEntry(entry, message);
        }
      }
    } finally {
      setCommitting(false);
    }
  }

  // 姓氏：本谱男丁同姓（宗族姓氏），从宗族名取尾字或手填
  const [familyNameInput, setFamilyNameInput] = useState("");
  function familyName(): string {
    if (familyNameInput.trim()) return familyNameInput.trim();
    const lin = lineages.find((l) => l.id === lineageId) as
      | (Lineage & { surname?: string })
      | undefined;
    return lin?.surname ?? "";
  }

  async function runFullValidation() {
    setValidating(true);
    try {
      const out = await client.runValidation();
      setValidateOut(out);
    } catch (e) {
      setValidateOut({
        summary: { error: -1 },
        issues: [{ message: e instanceof Error ? e.message : String(e) }],
      });
    } finally {
      setValidating(false);
    }
  }

  const pendingCount = staged.filter((e) => e.status === "staged").length;
  const committedCount = staged.filter((e) => e.status === "committed").length;

  return (
    <div className="wb">
      <div className="wb-head">
        <h2>谱师转录工作台</h2>
        <div className="wb-note">
          老谱扫描件 → 结构化人物。原文照录（农历/年号进原文字段）、
          推定必标注、每条必带出处页码。谱特异性（派语/房支）全部是数据。
        </div>
      </div>

      <ErrorBar error={formError} />

      <div className="wb-settings">
        <label>
          宗族
          <select
            value={lineageId}
            onChange={(e) => setLineageId(e.target.value)}
          >
            <option value="">— 选择 —</option>
            {lineages.map((l) => (
              <option key={l.id} value={l.id}>
                {l.name}
              </option>
            ))}
          </select>
        </label>
        <label>
          支派（可选）
          <select value={branchId} onChange={(e) => setBranchId(e.target.value)}>
            <option value="">— 无 —</option>
            {branches.map((b) => (
              <option key={b.id} value={b.id}>
                {b.name}
              </option>
            ))}
          </select>
        </label>
        <label>
          谱中姓氏
          <input
            value={familyNameInput}
            onChange={(e) => setFamilyNameInput(e.target.value)}
            placeholder="缺省取宗族 surname"
            size={8}
          />
        </label>
        <button
          className="wb-validate"
          onClick={runFullValidation}
          disabled={validating || !lineageId}
        >
          {validating ? "校验中…" : "全库校验"}
        </button>
      </div>

      {validateOut && (
        <div className="wb-validate-out">
          校验：error {validateOut.summary?.error ?? 0} · warning{" "}
          {validateOut.summary?.warning ?? 0} · info{" "}
          {validateOut.summary?.info ?? 0}
          <ul>
            {(validateOut.issues ?? []).slice(0, 10).map((i, idx) => (
              <li key={idx}>
                {i.rule_id ? `[${i.rule_id}] ` : ""}
                {i.message}
              </li>
            ))}
          </ul>
        </div>
      )}

      <div className="wb-panes">
        {/* ---------- 左：文档查看 ---------- */}
        <section className="wb-viewer">
          <div className="wb-file">
            <input
              type="file"
              accept="application/pdf,image/*"
              onChange={(e) => {
                const f = e.target.files?.[0];
                if (f) void onFile(f);
              }}
            />
            {pdfDoc && (
              <>
                <button onClick={() => setPageNo((n) => Math.max(1, n - 1))}>
                  ◀
                </button>
                <span>
                  {pageNo} / {totalPages}
                </span>
                <button
                  onClick={() =>
                    setPageNo((n) => Math.min(totalPages, n + 1))
                  }
                >
                  ▶
                </button>
              </>
            )}
            <button onClick={() => setZoom((z) => Math.max(0.4, z - 0.2))}>
              缩小
            </button>
            <span>{Math.round(zoom * 100)}%</span>
            <button onClick={() => setZoom((z) => Math.min(4, z + 0.2))}>
              放大
            </button>
            <button onClick={() => setRotation((r) => (r + 90) % 360)}>
              旋转
            </button>
          </div>
          {viewerError && <div className="banner banner-error">{viewerError}</div>}
          <div className="wb-canvas-wrap">
            <canvas
              ref={canvasRef}
              className="wb-canvas"
              onPointerDown={onPointerDown}
              onPointerMove={onPointerMove}
              onPointerUp={onPointerUp}
            />
            {rendering && <div className="wb-rendering">渲染中…</div>}
          </div>
          <div className="wb-hint">
            在页面上拖选一块竖列区域，下方放大镜即显示放大图——逐列目验。
          </div>
          <canvas ref={cropRef} className="wb-crop" />
        </section>

        {/* ---------- 右：条目表单 + 队列 ---------- */}
        <section className="wb-entry">
          <h3>行传条目</h3>
          <div className="wb-form">
            <label>
              世代
              <input
                type="number"
                min={1}
                value={form.generation_no}
                onChange={(e) =>
                  setForm((f) => ({
                    ...f,
                    generation_no: Number(e.target.value) || 1,
                  }))
                }
                size={3}
              />
            </label>
            <label>
              名讳
              <input
                value={form.given_name}
                onChange={(e) => setForm((f) => ({ ...f, given_name: e.target.value }))}
                size={10}
              />
            </label>
            <label>
              字
              <input
                value={form.courtesy}
                onChange={(e) => setForm((f) => ({ ...f, courtesy: e.target.value }))}
                size={8}
              />
            </label>
            <label>
              号
              <input
                value={form.art}
                onChange={(e) => setForm((f) => ({ ...f, art: e.target.value }))}
                size={8}
              />
            </label>
            <label>
              父（显示）
              <input
                value={form.fatherName}
                onChange={(e) => setForm((f) => ({ ...f, fatherName: e.target.value }))}
                size={10}
              />
            </label>
            <label>
              生（原文）
              <input
                value={form.birth_lunar}
                onChange={(e) => setForm((f) => ({ ...f, birth_lunar: e.target.value }))}
                size={14}
                placeholder="如 康熙甲子年六月十一日"
              />
            </label>
            <label>
              卒（原文）
              <input
                value={form.death_lunar}
                onChange={(e) => setForm((f) => ({ ...f, death_lunar: e.target.value }))}
                size={14}
                placeholder="如 卒乾隆五年七月十七日"
              />
            </label>
            <label>
              出处页
              <input
                value={form.pageOverride}
                onChange={(e) => setForm((f) => ({ ...f, pageOverride: e.target.value }))}
                size={4}
                placeholder={String(pageNo)}
              />
            </label>
            <label className="wb-check">
              <input
                type="checkbox"
                checked={form.chuSi}
                onChange={(e) => setForm((f) => ({ ...f, chuSi: e.target.checked }))}
              />
              出嗣
            </label>
            <label className="wb-check">
              <input
                type="checkbox"
                checked={form.doubtful}
                onChange={(e) => setForm((f) => ({ ...f, doubtful: e.target.checked }))}
              />
              存疑
            </label>
          </div>
          <input
            className="wb-wide"
            value={form.noteExtra}
            onChange={(e) => setForm((f) => ({ ...f, noteExtra: e.target.value }))}
            placeholder="备注（官职/葬地/仕历等，谱面原文照录）"
          />

          <h4>配氏（妻 / 侧室）</h4>
          {wives.map((w, i) => (
            <div key={i} className="wb-wife">
              <input
                value={w.surname}
                onChange={(e) =>
                  setWives((prev) =>
                    prev.map((x, j) =>
                      j === i ? { ...x, surname: e.target.value } : x,
                    ),
                  )
                }
                placeholder="姓（如 王）"
                size={6}
              />
              <select
                value={w.role}
                onChange={(e) =>
                  setWives((prev) =>
                    prev.map((x, j) =>
                      j === i
                        ? { ...x, role: e.target.value as WifeRow["role"] }
                        : x,
                    ),
                  )
                }
              >
                <option value="wife">妻</option>
                <option value="concubine">侧室</option>
              </select>
              <button onClick={() => setWives((prev) => prev.filter((_, j) => j !== i))}>
                删
              </button>
            </div>
          ))}
          <button onClick={() => setWives((prev) => [...prev, { surname: "", role: "wife" }])}>
            + 配氏
          </button>

          <h4>生子（欧式连书：将作为后续条目之父）</h4>
          {sons.map((s, i) => (
            <div key={i} className="wb-son">
              <input
                value={s.name}
                onChange={(e) =>
                  setSons((prev) =>
                    prev.map((x, j) =>
                      j === i ? { ...x, name: e.target.value } : x,
                    ),
                  )
                }
                placeholder="名讳"
                size={8}
              />
              <input
                value={s.motherSurname}
                onChange={(e) =>
                  setSons((prev) =>
                    prev.map((x, j) =>
                      j === i ? { ...x, motherSurname: e.target.value } : x,
                    ),
                  )
                }
                placeholder="所出母姓（可空）"
                size={10}
              />
              <button
                onClick={() => {
                  if (!s.name.trim()) return;
                  spawnSon(
                    {
                      key: formKeyRef.current,
                      generation_no: form.generation_no,
                      given_name: form.given_name,
                      page: currentPage(),
                    } as Staged,
                    s,
                  );
                  setSons((prev) => prev.filter((_, j) => j !== i));
                }}
                disabled={!form.given_name.trim()}
                title="生成本子条目并预填父亲"
              >
                → 续录此子
              </button>
              <button onClick={() => setSons((prev) => prev.filter((_, j) => j !== i))}>
                删
              </button>
            </div>
          ))}
          <button onClick={() => setSons((prev) => [...prev, { name: "", motherSurname: "" }])}>
            + 生子
          </button>

          <div className="wb-actions">
            <button className="primary" onClick={stageCurrent}>
              暂存条目
            </button>
            <button
              className="primary"
              onClick={commitAll}
              disabled={committing || pendingCount === 0}
            >
              {committing ? "入库中…" : `批量入库（${pendingCount} 条待提交）`}
            </button>
          </div>

          <h4>
            暂存队列（待提交 {pendingCount} · 已入库 {committedCount}）
          </h4>
          <ul className="wb-queue">
            {staged.map((e) => (
              <li
                key={e.key}
                className={
                  e.status === "committed"
                    ? "wb-item ok"
                    : e.status === "failed"
                      ? "wb-item bad"
                      : "wb-item"
                }
              >
                <span className="wb-item-main">
                  第{e.generation_no}世 {e.given_name}
                  {e.fatherName ? `（父：${e.fatherName}）` : ""}
                  {e.chuSi ? " [出嗣]" : ""}
                  {e.doubtful ? " [存疑]" : ""} · 谱p{e.page}
                  {e.wives.length > 0 && ` · 配${e.wives.length}`}
                  {e.sons.length > 0 && ` · 生子${e.sons.length}`}
                </span>
                {e.message && <span className="wb-item-msg">{e.message}</span>}
                {e.status === "committed" && (
                  <span className="wb-item-id">✓ {e.committedId?.slice(0, 10)}…</span>
                )}
                {e.status === "staged" && (
                  <button onClick={() => removeStaged(e.key)}>删</button>
                )}
                {e.status === "failed" && (
                  <span className="wb-item-msg">{e.message}</span>
                )}
              </li>
            ))}
          </ul>
        </section>
      </div>
    </div>
  );
}
