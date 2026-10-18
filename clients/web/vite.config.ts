import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// dev 模式把 /api 代理到本地 server，免 CORS 与手拼绝对地址；
// 生产构建（npm run build）产出纯静态文件，由任意静态服务器或
// genealogy-server 的静态托管（M6）承载。
export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    proxy: {
      "/api": {
        target: "http://127.0.0.1:8080",
        changeOrigin: true,
      },
    },
  },
  build: {
    outDir: "dist",
    sourcemap: true,
  },
});
