// Genealogy REST 客户端薄封装（M2）。
//
// 分工：types.ts 由 openapi-typescript 从 spec 生成（类型不漂移），
// 本文件只做运行时几十行的事——拼 URL、带头、解析 problem+json、
// 把 ETag/If-Match 乐观锁包成顺手的方法。不引任何第三方 HTTP 库：
// fetch 在 Node 18+ 与浏览器里都是全局的，多引一个依赖只多一层
// 版本风险。
//
// 用法：
//   const client = new GenealogyClient("http://127.0.0.1:8080/api/v1");
//   await client.login("local", "local");     // 本地免鉴权模式下任意口令可过
//   const person = await client.createPerson({ lineage_id, family_name, ... });
//   await client.updatePerson(person.id, { courtesy_name: "字" }, person.rev);

import type { components } from "./types";

export type Problem = components["schemas"]["Problem"];

// spec 未给实体 schema 标 required，生成的 id/rev 都是可选的；但
// server 对实体必然返回 ULID 与版本号（仓储层保证）。在使用侧收敛为
// 必填——否则每个组件都要跟 undefined 搏斗。
// 注意 event/place 两表无 rev（追加型事实），保持可选。
type Entity<T> = Omit<T, "id" | "rev"> & { id: string; rev?: number };
type Versioned<T> = Omit<T, "id" | "rev"> & { id: string; rev: number };

export type Person = Versioned<components["schemas"]["Person"]>;
export type PersonCreate = components["schemas"]["PersonCreate"];
export type PersonUpdate = components["schemas"]["PersonUpdate"];
export type Lineage = Versioned<components["schemas"]["Lineage"]>;
export type LineageCreate = components["schemas"]["LineageCreate"];
export type Branch = Versioned<components["schemas"]["Branch"]>;
export type Union = Versioned<components["schemas"]["Union"]>;
export type Event = Entity<components["schemas"]["Event"]>;
export type Place = Entity<components["schemas"]["Place"]>;
export type MourningDegree = components["schemas"]["MourningDegree"];
export type Appellation = components["schemas"]["Appellation"];
export type Role = components["schemas"]["Role"];
export type EventNotice = components["schemas"]["EventNotice"];
// Migration 同实体收紧：id 与 subject_id 必有（server 仓储层保证）
export type Migration = Omit<
  components["schemas"]["Migration"],
  "id" | "subject_id"
> & { id: string; subject_id: string };
// Document 无 rev（M7.4 篇章正文）：id 必有，rev 无
export type Document = Entity<components["schemas"]["Document"]>;
export type DocumentCreate = components["schemas"]["DocumentCreate"];
// PersonName 无 rev（台账语义）：id 必有
export type PersonName = Entity<components["schemas"]["PersonName"]>;
export type PersonNameCreate = components["schemas"]["PersonNameCreate"];

/** 列表端点统一返回的分页信封（spec components/responses/Page）。 */
export interface PageEnvelope {
  data?: Record<string, unknown>[];
  page?: {
    next_cursor?: string | null;
    has_more?: boolean;
  };
}

/** 业务错误：HTTP 状态 + RFC 9457 problem 体。 */
export class ApiError extends Error {
  readonly status: number;
  readonly problem?: Problem;

  constructor(status: number, problem?: Problem) {
    super(problem?.detail ?? problem?.title ?? `HTTP ${status}`);
    this.name = "ApiError";
    this.status = status;
    this.problem = problem;
  }

  /** problem.code（如 1001 参数校验、2001 不存在、2009 版本冲突）。 */
  get code(): number | undefined {
    return this.problem?.code;
  }
}

export interface RequestOptions {
  /** 查询参数（undefined/null 的键跳过）。 */
  query?: Record<string, string | number | boolean | undefined | null>;
  /** JSON 请求体。 */
  body?: unknown;
  /** If-Match 乐观锁值（当前 rev）。 */
  ifMatch?: number | string;
  /** 幂等键（写操作推荐带上，网络重试安全）。 */
  idempotencyKey?: string;
  /** 覆盖 Accept（导出端点要 text/plain 时用）。 */
  accept?: string;
  signal?: AbortSignal;
}

export class GenealogyClient {
  private readonly baseUrl: string;
  /** 401 时回调（context 用来置断连态）；令牌已在此前被清空。 */
  onUnauthorized: (() => void) | null = null;
  private token: string | null = null;

  constructor(baseUrl: string, token?: string) {
    // 去掉末尾斜杠，避免 path 拼出 //
    this.baseUrl = baseUrl.replace(/\/+$/, "");
    this.token = token ?? null;
  }

  /** 当前令牌（登录后由 login 设置，也可构造时传入）。 */
  get accessToken(): string | null {
    return this.token;
  }

  /**
   * 显式清空令牌（M13 轻量源边界：换址重连/断开时调用）。替换 client
   * 实例虽会丢弃旧令牌，但显式清空不依赖 GC 时序，杜绝旧 Bearer 以
   * 任何路径发往新地址。
   */
  clearToken(): void {
    this.token = null;
  }

  // ---------------------------------------------------------------- 基础

  /**
   * 拼绝对 URL。同源模式（baseUrl 形如 /api/v1）在浏览器里是合法的
   * 相对地址，但 new URL 只接受绝对地址——以当前页面 origin 为基解出。
   * Node 冒烟传的是绝对地址，base 不参与解析（fetch 忽略多余 base）。
   * 此前直接 new URL(baseUrl + path)，同源模式（README 宣称的默认
   * 用法）当场抛 Invalid URL——最高频路径不可用。
   */
  private resolveUrl(path: string): URL {
    const base =
      typeof window !== "undefined" ? window.location.origin : "http://localhost";
    return new URL(this.baseUrl + path, base);
  }

  /**
   * 401 统一处理（复核 2.12）：令牌失效/过期——清令牌并通知宿主
   * （context 置断连态，提示重新登录）。request / requestWithHeaders /
   * importGedcom / subscribe 四条请求路径共用：此前 multipart 导入与
   * SSE 订阅绕过这里，过期后导入只报错、SSE 空转重连，顶栏仍显示
   * 「已连接」，要等某个普通 request 撞上 401 才提示重登。
   */
  private handleUnauthorized(status: number): void {
    if (status === 401) {
      this.token = null;
      this.onUnauthorized?.();
    }
  }

  /**
   * 发一次请求并解出业务数据。
   * 2xx → 解析体（204 → undefined，text/* → 原文）；
   * 非 2xx → 抛 ApiError（problem+json 已解析）。
   */
  async request<T = unknown>(
    method: string,
    path: string,
    options: RequestOptions = {},
  ): Promise<T> {
    const url = this.resolveUrl(path);
    for (const [key, value] of Object.entries(options.query ?? {})) {
      if (value === undefined || value === null) continue;
      url.searchParams.set(key, String(value));
    }

    const headers: Record<string, string> = {};
    if (options.body !== undefined) headers["Content-Type"] = "application/json";
    if (options.ifMatch !== undefined) headers["If-Match"] = String(options.ifMatch);
    if (options.idempotencyKey) {
      headers["Idempotency-Key"] = options.idempotencyKey;
    }
    if (options.accept) headers["Accept"] = options.accept;
    if (this.token) headers["Authorization"] = `Bearer ${this.token}`;

    const response = await fetch(url, {
      method,
      headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
      signal: options.signal,
    });

    if (!response.ok) {
      this.handleUnauthorized(response.status);
      // 错误体一律 problem+json（server 连默认 404 都走它）；
      // 解析失败时退化成裸状态码，不吞原始信息。
      let problem: Problem | undefined;
      try {
        problem = (await response.json()) as Problem;
      } catch {
        // 非 JSON 错误体（如代理插入的 HTML）：留给 status 说话
      }
      throw new ApiError(response.status, problem);
    }

    if (response.status === 204) return undefined as T;
    const contentType = response.headers.get("Content-Type") ?? "";
    if (contentType.includes("application/json")) {
      return (await response.json()) as T;
    }
    return (await response.text()) as T;
  }

  /** 需要读响应头（ETag 等）的场景。 */
  async requestWithHeaders<T = unknown>(
    method: string,
    path: string,
    options: RequestOptions = {},
  ): Promise<{ data: T; etag?: string }> {
    const url = this.resolveUrl(path);
    for (const [key, value] of Object.entries(options.query ?? {})) {
      if (value === undefined || value === null) continue;
      url.searchParams.set(key, String(value));
    }
    const headers: Record<string, string> = {};
    if (options.body !== undefined) headers["Content-Type"] = "application/json";
    if (options.ifMatch !== undefined) headers["If-Match"] = String(options.ifMatch);
    if (this.token) headers["Authorization"] = `Bearer ${this.token}`;

    const response = await fetch(url, {
      method,
      headers,
      body: options.body === undefined ? undefined : JSON.stringify(options.body),
    });
    if (!response.ok) {
      this.handleUnauthorized(response.status);
      let problem: Problem | undefined;
      try {
        problem = (await response.json()) as Problem;
      } catch {
        // 同上：非 JSON 错误体不吞
      }
      throw new ApiError(response.status, problem);
    }
    const data = response.status === 204
      ? (undefined as T)
      : ((await response.json()) as T);
    return { data, etag: response.headers.get("ETag") ?? undefined };
  }

  // ---------------------------------------------------------------- 鉴权

  /** 登录拿 JWT。本地免鉴权模式下 server 不校验口令，但端点仍要调。 */
  async login(username: string, password: string): Promise<Role> {
    const out = await this.request<{ access_token?: string; role?: Role }>(
      "POST",
      "/auth/token",
      { body: { username, password } },
    );
    if (!out.access_token) {
      throw new Error("登录响应缺少 access_token");
    }
    this.token = out.access_token;
    return out.role ?? "viewer";
  }

  async me(): Promise<{ username?: string; role?: Role; lineage_ids?: string[] }> {
    return this.request("GET", "/me");
  }

  async health(): Promise<{ status?: "ok" | "degraded"; version?: string }> {
    return this.request("GET", "/health");
  }

  // ---------------------------------------------------------------- 宗族

  async listLineages(
    params?: { limit?: number; cursor?: string },
    options?: { signal?: AbortSignal },
  ): Promise<PageEnvelope> {
    return this.request("GET", "/lineages", {
      query: { ...params },
      signal: options?.signal,
    });
  }

  async createLineage(body: LineageCreate): Promise<Lineage> {
    return this.request("POST", "/lineages", { body });
  }

  async getLineage(id: string): Promise<Lineage> {
    return this.request("GET", `/lineages/${encodeURIComponent(id)}`);
  }

  /** 更新宗族；rev 传当前版本（If-Match），不符抛 409。 */
  async updateLineage(
    id: string,
    patch: Partial<LineageCreate>,
    rev: number,
  ): Promise<Lineage> {
    return this.request("PATCH", `/lineages/${encodeURIComponent(id)}`, {
      body: patch,
      ifMatch: rev,
    });
  }

  async deleteLineage(id: string): Promise<void> {
    return this.request("DELETE", `/lineages/${encodeURIComponent(id)}`);
  }

  // ---------------------------------------------------------------- 支派

  async listBranches(
    params?: {
      lineage_id?: string;
      limit?: number;
      cursor?: string;
    },
    options?: { signal?: AbortSignal },
  ): Promise<PageEnvelope> {
    return this.request("GET", "/branches", {
      query: { ...params },
      signal: options?.signal,
    });
  }

  async createBranch(body: {
    lineage_id: string;
    name: string;
    founder_person_id?: string;
    parent_branch_id?: string;
    generation_char?: string;
  }): Promise<Branch> {
    return this.request("POST", "/branches", { body });
  }

  async getBranch(id: string): Promise<Branch> {
    return this.request("GET", `/branches/${encodeURIComponent(id)}`);
  }

  async updateBranch(
    id: string,
    patch: {
      name?: string;
      founder_person_id?: string;
      parent_branch_id?: string;
      generation_char?: string;
    },
    rev: number,
  ): Promise<Branch> {
    return this.request("PATCH", `/branches/${encodeURIComponent(id)}`, {
      body: patch,
      ifMatch: rev,
    });
  }

  async deleteBranch(id: string): Promise<void> {
    return this.request("DELETE", `/branches/${encodeURIComponent(id)}`);
  }

  async getBranchTree(id: string): Promise<{ root?: unknown }> {
    return this.request("GET", `/branches/${encodeURIComponent(id)}/tree`);
  }

  // ---------------------------------------------------------------- 人物

  async listPersons(params?: {
    q?: string;
    branch_id?: string;
    lineage_id?: string;
    generation_from?: number;
    generation_to?: number;
    gender?: "male" | "female" | "unknown";
    is_living?: boolean;
    has_citation?: boolean;
    birth_year_from?: number;
    birth_year_to?: number;
    limit?: number;
    cursor?: string;
  }, options?: { signal?: AbortSignal }): Promise<PageEnvelope> {
    return this.request("GET", "/persons", {
      query: { ...params },
      signal: options?.signal,
    });
  }

  async createPerson(body: PersonCreate): Promise<Person> {
    return this.request("POST", "/persons", { body });
  }

  async getPerson(
    id: string,
    // 与 spec 的 expand 枚举一致（events/unions/migrations 已实现；
    // citations/names 属 M7，实现后再加入）。
    expand?: Array<"events" | "unions" | "migrations" | "names">,
  ): Promise<Person> {
    return this.request("GET", `/persons/${encodeURIComponent(id)}`, {
      query: { expand: expand?.length ? expand.join(",") : undefined },
    });
  }

  /** 更新人物；rev 为当前版本，过期先重新 getPerson 再改。 */
  async updatePerson(
    id: string,
    patch: PersonUpdate,
    rev: number,
  ): Promise<Person> {
    return this.request("PATCH", `/persons/${encodeURIComponent(id)}`, {
      body: patch,
      ifMatch: rev,
    });
  }

  async deletePerson(id: string): Promise<void> {
    return this.request("DELETE", `/persons/${encodeURIComponent(id)}`);
  }

  // ------------------------------------------------------- 名号（M7.1）

  async listPersonNames(personId: string): Promise<PageEnvelope> {
    return this.request(
      "GET",
      `/persons/${encodeURIComponent(personId)}/names`,
    );
  }

  async createPersonName(
    personId: string,
    body: PersonNameCreate,
  ): Promise<PersonName> {
    return this.request(
      "POST",
      `/persons/${encodeURIComponent(personId)}/names`,
      { body },
    );
  }

  async deletePersonName(personId: string, nameId: string): Promise<void> {
    await this.request(
      "DELETE",
      `/persons/${encodeURIComponent(personId)}/names/${encodeURIComponent(nameId)}`,
    );
  }

  // ------------------------------------------------------- 世系算法端点

  async getPersonAncestors(id: string, depth = 10): Promise<unknown> {
    return this.request("GET", `/persons/${encodeURIComponent(id)}/ancestors`, {
      query: { depth },
    });
  }

  async getPersonDescendants(id: string, depth = 10): Promise<unknown> {
    return this.request("GET", `/persons/${encodeURIComponent(id)}/descendants`, {
      query: { depth },
    });
  }

  async getPersonTree(
    id: string,
    params?: { mode?: "five_generation" | "full"; depth?: number },
  ): Promise<unknown> {
    return this.request("GET", `/persons/${encodeURIComponent(id)}/tree`, {
      query: { ...params },
    });
  }

  /** 五服计算：from → to 的丧服等级（明清定型规则集）。 */
  async getMourningDegree(
    fromId: string,
    toId: string,
    ruleset: "ming_qing" = "ming_qing",
  ): Promise<MourningDegree> {
    return this.request(
      "GET",
      `/persons/${encodeURIComponent(fromId)}/mourning-degree`,
      { query: { to: toId, ruleset } },
    );
  }

  /** 亲属称谓：from 应称 to 什么。 */
  async getAppellation(fromId: string, toId: string): Promise<Appellation> {
    return this.request(
      "GET",
      `/persons/${encodeURIComponent(fromId)}/appellation`,
      { query: { to: toId } },
    );
  }

  // ---------------------------------------------------------------- 婚姻

  async listUnions(params?: {
    person_id?: string;
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/unions", { query: { ...params } });
  }

  async createUnion(body: {
    type: "marriage" | "concubinage" | "remarriage";
    members: Array<{ person_id: string; role: "husband" | "wife" | "concubine" }>;
    sequence?: number;
    start_date?: unknown;
    place_id?: string;
  }): Promise<Union> {
    return this.request("POST", "/unions", { body });
  }

  async getUnion(id: string): Promise<Union> {
    return this.request("GET", `/unions/${encodeURIComponent(id)}`);
  }

  async updateUnion(
    id: string,
    patch: {
      type?: "marriage" | "concubinage" | "remarriage";
      sequence?: number;
      start_date?: unknown;
      end_date?: unknown;
      place_id?: string;
    },
    rev: number,
  ): Promise<Union> {
    return this.request("PATCH", `/unions/${encodeURIComponent(id)}`, {
      body: patch,
      ifMatch: rev,
    });
  }

  async deleteUnion(id: string): Promise<void> {
    return this.request("DELETE", `/unions/${encodeURIComponent(id)}`);
  }

  // ---------------------------------------------------------------- 事件

  async listEvents(params?: {
    subject_type?: "person" | "union" | "branch" | "lineage";
    subject_id?: string;
    type?: string;
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/events", { query: { ...params } });
  }

  async createEvent(body: {
    subject_type: "person" | "union" | "branch" | "lineage";
    subject_id: string;
    type: string;
    date?: unknown;
    place_id?: string;
    description?: string;
  }): Promise<Event> {
    return this.request("POST", "/events", { body });
  }

  async getEvent(id: string): Promise<Event> {
    return this.request("GET", `/events/${encodeURIComponent(id)}`);
  }

  async deleteEvent(id: string): Promise<void> {
    return this.request("DELETE", `/events/${encodeURIComponent(id)}`);
  }

  // ---------------------------------------------------------------- 地点

  async listPlaces(params?: {
    q?: string;
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/places", { query: { ...params } });
  }

  async createPlace(body: {
    name: string;
    type?: string;
    modern_admin?: string;
    historical_name?: string;
    lat?: number;
    lng?: number;
  }): Promise<Place> {
    return this.request("POST", "/places", { body });
  }

  async getPlace(id: string): Promise<Place> {
    return this.request("GET", `/places/${encodeURIComponent(id)}`);
  }

  /** 地点无 rev，不走乐观锁（实现如此，勿传 If-Match）。 */
  async updatePlace(
    id: string,
    patch: {
      name?: string;
      modern_admin?: string;
      historical_name?: string;
      lat?: number;
      lng?: number;
    },
  ): Promise<Place> {
    return this.request("PATCH", `/places/${encodeURIComponent(id)}`, {
      body: patch,
    });
  }

  async deletePlace(id: string): Promise<void> {
    return this.request("DELETE", `/places/${encodeURIComponent(id)}`);
  }

  // ------------------------------------------------------- 检索/统计/校验

  async search(
    params: {
      q: string;
      scope?: "person" | "document" | "all";
      limit?: number;
      cursor?: string;
    },
    options?: { signal?: AbortSignal },
  ): Promise<PageEnvelope> {
    return this.request("GET", "/search", {
      query: { ...params },
      signal: options?.signal,
    });
  }

  async getStats(): Promise<{
    person_count?: number;
    event_count?: number;
    branch_count?: number;
    living_count?: number;
    uncited_facts?: number;
    generation_distribution?: Array<{ generation_no?: number; count?: number }>;
  }> {
    return this.request("GET", "/stats");
  }

  async runValidation(ruleIds?: string[]): Promise<{
    issues?: Array<{
      rule_id?: string;
      severity?: "error" | "warning" | "info";
      entity_type?: string;
      entity_id?: string;
      message?: string;
      suggestion?: string;
    }>;
    summary?: { error?: number; warning?: number; info?: number };
  }> {
    return this.request("POST", "/validate", {
      body: ruleIds ? { rule_ids: ruleIds } : {},
    });
  }

  // ---------------------------------------------------------------- 导入导出

  /** GEDCOM 导入（multipart）。file 传 Blob/File（浏览器）或 Buffer 源数据。 */
  async importGedcom(
    file: Blob,
    params?: { lineage_id?: string; filename?: string },
  ): Promise<{ imported?: number; skipped?: number; warnings?: string[] }> {
    const form = new FormData();
    form.append("file", file, params?.filename ?? "import.ged");
    if (params?.lineage_id) form.append("lineage_id", params.lineage_id);

    const url = this.resolveUrl("/imports/gedcom");
    const headers: Record<string, string> = {};
    if (this.token) headers["Authorization"] = `Bearer ${this.token}`;
    // Content-Type 不设：fetch 会按 FormData 自动带 boundary
    const response = await fetch(url, { method: "POST", headers, body: form });
    if (!response.ok) {
      this.handleUnauthorized(response.status);
      let problem: Problem | undefined;
      try {
        problem = (await response.json()) as Problem;
      } catch {
        // 非 JSON 错误体
      }
      throw new ApiError(response.status, problem);
    }
    return (await response.json()) as {
      imported?: number;
      skipped?: number;
      warnings?: string[];
    };
  }

  async exportGedcom(lineageId: string): Promise<string> {
    return this.request("GET", "/exports/gedcom", {
      query: { lineage_id: lineageId },
      accept: "text/plain",
    });
  }

  async exportTree(
    personId: string,
    format: "dot" | "mermaid" = "dot",
  ): Promise<string> {
    return this.request("GET", "/exports/tree", {
      query: { person_id: personId, format },
      accept: "text/plain",
    });
  }

  // ---------------------------------------------------------------- 文档

  async listDocuments(params?: {
    type?:
      | "preface"
      | "rule"
      | "regulation"
      | "literature"
      | "contract"
      | "edition_note";
    lineage_id?: string;
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/documents", { query: { ...params } });
  }

  async getDocument(id: string): Promise<Document> {
    return this.request("GET", `/documents/${encodeURIComponent(id)}`);
  }

  async createDocument(body: DocumentCreate): Promise<Document> {
    return this.request("POST", "/documents", { body });
  }

  /** merge 语义（absent=保留）：content 是大段正文，改标题不会误清正文 */
  async updateDocument(id: string, body: DocumentCreate): Promise<Document> {
    return this.request("PATCH", `/documents/${encodeURIComponent(id)}`, {
      body,
    });
  }

  async deleteDocument(id: string): Promise<void> {
    await this.request("DELETE", `/documents/${encodeURIComponent(id)}`);
  }

  // ---------------------------------------------------------------- 迁徙

  async listMigrations(params?: {
    branch_id?: string;
    subject_id?: string;
    subject_type?: "person" | "branch";
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/migrations", { query: { ...params } });
  }

  /** 登记迁徙。date 传 GenealogyDate 对象（或省略）。 */
  async createMigration(body: {
    subject_type: "person" | "branch";
    subject_id: string;
    to_place_id: string;
    from_place_id?: string;
    date?: unknown;
    reason?: string;
    route?: string;
    note?: string;
  }): Promise<Migration> {
    return this.request("POST", "/migrations", { body });
  }

  // ---------------------------------------------------------------- SSE

  /**
   * 订阅 SSE 变更流（GET /events/stream，M5-3）。
   *
   * 用 fetch 流而不是 EventSource：后者不发自定义头，带不上
   * Bearer 令牌（--require-auth 部署下必须）。
   * 帧解析按 SSE 协议：空行分帧、data: 行取载荷；注释帧（: 开头）
   * 与 retry/id/event 帧按需忽略。
   *
   * 返回取消函数；断线不自动重连（调用方决定何时重订——
   * 通常配合视图刷新即可，见 App.tsx 的实时条）。
   */
  subscribe(
    onEvent: (notice: EventNotice) => void,
    onError?: (error: unknown) => void,
  ): () => void {
    const controller = new AbortController();
    const url = this.resolveUrl("/events/stream");
    const headers: Record<string, string> = {};
    if (this.token) headers["Authorization"] = `Bearer ${this.token}`;

    (async () => {
      const response = await fetch(url, { headers, signal: controller.signal });
      if (!response.ok || !response.body) {
        // 令牌过期也会走到这里（复核 2.12）：先走统一的 401 处理
        // （清令牌 + 触发重登提示），再照常报错——否则 SSE 每 3 秒
        // 空转重连、顶栏一直「已连接」，用户得不到重登信号。
        this.handleUnauthorized(response.status);
        throw new ApiError(response.status);
      }
      const reader = response.body.getReader();
      const decoder = new TextDecoder();
      let buffer = "";
      for (;;) {
        const { done, value } = await reader.read();
        if (done) break;
        buffer += decoder.decode(value, { stream: true });
        let separator = buffer.indexOf("\n\n");
        while (separator >= 0) {
          const frame = buffer.slice(0, separator);
          buffer = buffer.slice(separator + 2);
          for (const line of frame.split("\n")) {
            if (line.startsWith("data: ")) {
              try {
                onEvent(JSON.parse(line.slice(6)) as EventNotice);
              } catch {
                // 单帧解析失败不致命：跳过，等下一帧
              }
            }
          }
          separator = buffer.indexOf("\n\n");
        }
      }
      // 服务端正常关闭流（重启/停机）也算断线：不回调的话订阅方
      // 会停在「在线」假状态、永不重连——此前 done 后直接静默返回。
      if (!controller.signal.aborted) {
        onError?.(new Error("SSE 连接被服务端关闭"));
      }
    })().catch((error) => {
      if (!controller.signal.aborted) onError?.(error);
    });

    return () => controller.abort();
  }
}

/**
 * 游标分页遍历：把「limit + cursor」的列表端点拉成异步迭代器，
 * 调用方 `for await (const item of paginate(...))` 即可，不用记翻页。
 */
export async function* paginate(
  fetchPage: (cursor?: string) => Promise<PageEnvelope>,
): AsyncGenerator<Record<string, unknown>> {
  let cursor: string | undefined;
  // 兜底轮数：正常一页 50 条，1e6 条数据也就 2 万轮；
  // 真到了这个量级早该走 CLI 灌库，这里是防「服务端不回 has_more=false」死循环。
  for (let guard = 0; guard < 100_000; ++guard) {
    const page = await fetchPage(cursor);
    for (const item of page.data ?? []) yield item;
    const next = page.page?.next_cursor ?? undefined;
    if (!page.page?.has_more || !next) return;
    cursor = next;
  }
  throw new Error("分页超过 10 万轮仍未结束，疑似服务端游标异常");
}
