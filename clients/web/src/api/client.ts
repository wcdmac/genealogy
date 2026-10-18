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

  // ---------------------------------------------------------------- 基础

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
    const url = new URL(this.baseUrl + path);
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
    const url = new URL(this.baseUrl + path);
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

  async listLineages(params?: {
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/lineages", { query: { ...params } });
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

  async listBranches(params?: {
    lineage_id?: string;
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/branches", { query: { ...params } });
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
  }): Promise<PageEnvelope> {
    return this.request("GET", "/persons", { query: { ...params } });
  }

  async createPerson(body: PersonCreate): Promise<Person> {
    return this.request("POST", "/persons", { body });
  }

  async getPerson(
    id: string,
    expand?: Array<"events" | "unions" | "citations" | "names" | "migrations">,
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

  async search(params: {
    q: string;
    scope?: "person" | "all";
    limit?: number;
    cursor?: string;
  }): Promise<PageEnvelope> {
    return this.request("GET", "/search", { query: { ...params } });
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

    const url = new URL(this.baseUrl + "/imports/gedcom");
    const headers: Record<string, string> = {};
    if (this.token) headers["Authorization"] = `Bearer ${this.token}`;
    // Content-Type 不设：fetch 会按 FormData 自动带 boundary
    const response = await fetch(url, { method: "POST", headers, body: form });
    if (!response.ok) {
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
