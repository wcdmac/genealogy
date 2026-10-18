import { StrictMode } from "react";
import { createRoot } from "react-dom/client";

import { App } from "./App";
import { ClientProvider } from "./context";
import "./styles.css";

const root = document.getElementById("root");
if (!root) throw new Error("#root 不存在");

createRoot(root).render(
  <StrictMode>
    <ClientProvider>
      <App />
    </ClientProvider>
  </StrictMode>,
);
