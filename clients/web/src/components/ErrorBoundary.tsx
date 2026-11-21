// 渲染层兜底（第三轮新-W5）：任何视图抛错都不再把整个 React 树掀翻
// 成白屏——错误显示在原地，用户可以切到别的视图或重试。
// React 的错误边界只支持类组件（官方口径，函数组件无等价物）。

import { Component, type ReactNode } from "react";

interface Props {
  children: ReactNode;
}

interface State {
  error: Error | null;
}

export class ErrorBoundary extends Component<Props, State> {
  state: State = { error: null };

  static getDerivedStateFromError(error: Error): State {
    return { error };
  }

  componentDidUpdate(prevProps: Props) {
    // 子树切换（视图导航）时自动复位——旧视图的错不该粘在新视图上
    if (prevProps.children !== this.props.children && this.state.error) {
      this.setState({ error: null });
    }
  }

  render() {
    if (this.state.error) {
      return (
        <div className="banner banner-error">
          视图渲染出错：{this.state.error.message}
        </div>
      );
    }
    return this.props.children;
  }
}
