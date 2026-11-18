// 客户端实例的 React 注入层。
//
// 为什么抽 context：client.ts 是无依赖的纯 TS 类（浏览器/Node 都能跑），
// React 只负责持有它 + 广播连接状态。视图组件一律经 useClient() 取用，
// 不各自 new，也不把 fetch 逻辑漏进组件。

import {
  createContext,
  useCallback,
  useContext,
  useMemo,
  useRef,
  useState,
  type ReactNode,
} from "react";

import { ApiError, GenealogyClient, type Role } from "./api/client";

export interface ConnectionState {
  baseUrl: string;
  connected: boolean;
  role?: Role;
  error?: string;
}

interface ClientContextValue {
  client: GenealogyClient;
  connection: ConnectionState;
  /** 连接（登录 + /me 探活）；失败时把原因写进 connection.error。 */
  connect: (
    baseUrl: string,
    username?: string,
    password?: string,
  ) => Promise<boolean>;
  disconnect: () => void;
}

const ClientContext = createContext<ClientContextValue | null>(null);

const DEFAULT_BASE = "/api/v1"; // dev 走 vite 代理；生产同源部署时即此值

export function ClientProvider({ children }: { children: ReactNode }) {
  const clientRef = useRef<GenealogyClient>(new GenealogyClient(DEFAULT_BASE));
  const [connection, setConnection] = useState<ConnectionState>({
    baseUrl: DEFAULT_BASE,
    connected: false,
  });

  const connect = useCallback(
  async (baseUrl: string, username = "web", password = "web") => {
    const client = new GenealogyClient(baseUrl);
    // 401 → 置断连态提示重登（M13；server --require-auth 下凭据
    // 可从 UI 输入轮换，不再硬编码 web/web——2026-11 静态审查 H10）
    client.onUnauthorized = () => {
      setConnection({ baseUrl, connected: false, error: "登录已过期，请重新连接" });
    };
    try {
      const role = await client.login(username, password);
      await client.me();
      clientRef.current = client;
      setConnection({ baseUrl, connected: true, role });
      return true;
    } catch (error) {
      const message =
        error instanceof ApiError
          ? `${error.status} ${error.message}`
          : String(error);
      setConnection({ baseUrl, connected: false, error: message });
      return false;
    }
  },
  []);

  const disconnect = useCallback(() => {
    clientRef.current = new GenealogyClient(DEFAULT_BASE);
    setConnection({ baseUrl: DEFAULT_BASE, connected: false });
  }, []);

  const value = useMemo(
    () => ({ client: clientRef.current, connection, connect, disconnect }),
    [connection, connect, disconnect],
  );
  return <ClientContext.Provider value={value}>{children}</ClientContext.Provider>;
}

export function useClient(): ClientContextValue {
  const ctx = useContext(ClientContext);
  if (!ctx) throw new Error("useClient 必须在 ClientProvider 内使用");
  return ctx;
}
