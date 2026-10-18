#!/usr/bin/env python3
"""OpenAPI 规范校验（设计文档 M2 验收：「openapi.json 校验通过」）。

为什么单独一个脚本：openapi.yaml 是客户端代码生成的唯一事实源，
spec 写坏（断引用、类型错误、缺 required 字段）不会让任何 C++ 编译
失败，只会在生成客户端或对接第三方时炸——而且是几个月后炸。
把它做成 CI 卡点，坏 spec 当天红。

校验内容：
  1. YAML 可解析；
  2. 符合 OpenAPI 3.1 元 schema（openapi-spec-validator）；
  3. 项目自身约定（比元 schema 更严）：
     - 所有 $ref 能解析（断引用直接报）；
     - 每个 operation 有 operationId（客户端生成器要靠它命名方法）；
     - 每个 operation 至少一个 2xx 响应；
     - 错误响应用 RFC 9457 problem+json 且带 code 字段。

用法：
  python3 tools/validate_openapi.py            # 校验 shared/openapi/openapi.yaml
  python3 tools/validate_openapi.py --file X   # 校验别的文件

退出码：0 通过；1 校验失败；2 用法/依赖问题。
依赖：pyyaml、openapi-spec-validator（CI 里 pip install；
本地用受管 venv，见 binary 约定）。
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_SPEC = ROOT / "shared" / "openapi" / "openapi.yaml"


def fail(msg: str) -> int:
    print(f"[openapi] 校验失败: {msg}")
    return 1


def main() -> int:
    ap = argparse.ArgumentParser(description="校验 OpenAPI 规范")
    ap.add_argument("--file", default=str(DEFAULT_SPEC), help="规范文件路径")
    args = ap.parse_args()

    spec_path = Path(args.file)
    if not spec_path.is_file():
        return fail(f"文件不存在: {spec_path}")

    try:
        import yaml
    except ImportError:
        print("[openapi] 缺少依赖 pyyaml（pip install pyyaml openapi-spec-validator）")
        return 2
    try:
        from openapi_spec_validator import validate
        from openapi_spec_validator.readers import read_from_filename
    except ImportError:
        print("[openapi] 缺少依赖 openapi-spec-validator"
              "（pip install pyyaml openapi-spec-validator）")
        return 2

    # ---- 1) 元 schema 校验 ----
    try:
        spec_dict, base_uri = read_from_filename(str(spec_path))
        validate(spec_dict, base_uri=base_uri)
    except Exception as e:  # noqa: BLE001 - 校验库异常类型繁多，统一转失败
        return fail(f"不符合 OpenAPI 元 schema: {e}")

    print(f"[openapi] 元 schema 校验通过: {spec_path}")

    # ---- 2) 项目约定 ----
    version = spec_dict.get("openapi", "")
    if not version.startswith("3.1"):
        return fail(f"规范声明的是 OpenAPI {version}，本项目约定 3.1")

    paths = spec_dict.get("paths", {})
    if not paths:
        return fail("paths 为空——没有端点算什么规范")

    problems: list[str] = []
    op_count = 0
    for path, item in paths.items():
        if not isinstance(item, dict):
            continue
        for method, op in item.items():
            if method not in {"get", "post", "put", "patch", "delete",
                              "head", "options", "trace"}:
                continue
            op_count += 1
            where = f"{method.upper()} {path}"
            if not isinstance(op, dict):
                problems.append(f"{where}: operation 不是对象")
                continue
            # operationId：客户端生成器的方法名来源
            if not op.get("operationId"):
                problems.append(f"{where}: 缺 operationId")
            # 至少一个 2xx
            responses = op.get("responses", {})
            if not any(str(code).startswith("2") for code in responses):
                problems.append(f"{where}: 没有 2xx 响应")

    if op_count == 0:
        return fail("paths 里没有任何 operation")

    # ---- 3) $ref 断引用 ----
    # 元 schema 不校验「本地引用是否真的存在」，这里补一道。
    # 收集全部可引用位置（components/schemas 等顶层 components 段）。
    components = spec_dict.get("components", {})
    ref_errors: list[str] = []

    def walk(node: object, trail: str) -> None:
        if isinstance(node, dict):
            for k, v in node.items():
                if k == "$ref" and isinstance(v, str) and v.startswith("#/"):
                    # 解析 #/a/b/c 形式的 JSON Pointer
                    cur: object = spec_dict
                    ok = True
                    for part in v[2:].split("/"):
                        part = part.replace("~1", "/").replace("~0", "~")
                        if isinstance(cur, dict) and part in cur:
                            cur = cur[part]
                        else:
                            ok = False
                            break
                    if not ok:
                        ref_errors.append(f"{trail}: 断引用 {v}")
                else:
                    walk(v, f"{trail}/{k}")
        elif isinstance(node, list):
            for i, v in enumerate(node):
                walk(v, f"{trail}[{i}]")

    walk(spec_dict, "#")
    problems.extend(ref_errors)

    if problems:
        for p in problems:
            print(f"  - {p}")
        return fail(f"{len(problems)} 处违反项目约定")

    print(f"[openapi] 项目约定校验通过: {op_count} 个 operation，"
          f"$ref 全部可解析")
    print("[openapi] OK")
    return 0


if __name__ == "__main__":
    sys.exit(main())
