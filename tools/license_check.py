#!/usr/bin/env python3
"""依赖许可证白名单 CI 卡点（设计文档 2.1 / 6.2）。

规则：
1. vcpkg.json 的全部依赖（含 features）必须在 shared/licenses/allowlist.txt；
2. 全仓文本文件不得出现 Qt 引用（Qt 的 CMake 包查询、Qt5/Qt6 命名空间目标、
   Qt 头文件包含）。匹配模式定义见本文件 QT_PATTERN。

退出码：0 通过；1 存在违规。
"""
from __future__ import annotations

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = ROOT / "shared" / "licenses" / "allowlist.txt"
MANIFEST = ROOT / "vcpkg.json"

SKIP_DIR_PARTS = {".git", "vcpkg_installed", "out", "__pycache__", "node_modules"}
TEXT_SUFFIXES = {
    ".json", ".cmake", ".txt", ".cpp", ".hpp", ".cxx", ".cc", ".h",
    ".py", ".yml", ".yaml", ".md", ".sql",
}
QT_PATTERN = re.compile(r"Qt[56]?::|find_package\s*\(\s*Qt|#include\s*<Q[tT]")


def load_allowlist() -> set[str]:
    names: set[str] = set()
    for line in ALLOWLIST.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#"):
            names.add(line)
    return names


def manifest_deps() -> set[str]:
    manifest = json.loads(MANIFEST.read_text(encoding="utf-8"))
    deps: set[str] = set()
    for dep in manifest.get("dependencies", []):
        deps.add(dep if isinstance(dep, str) else dep["name"])
    for feature in manifest.get("features", {}).values():
        for dep in feature.get("dependencies", []):
            deps.add(dep if isinstance(dep, str) else dep["name"])
    return deps


def main() -> int:
    violations: list[str] = []

    allowed = load_allowlist()
    for dep in sorted(manifest_deps()):
        if dep not in allowed:
            violations.append(f"依赖不在白名单: {dep}")

    for path in sorted(ROOT.rglob("*")):
        if not path.is_file() or path.suffix not in TEXT_SUFFIXES:
            continue
        if path.resolve() == Path(__file__).resolve():
            continue  # 豁免自身（QT_PATTERN 的定义文本）
        rel = path.relative_to(ROOT)
        if any(part in SKIP_DIR_PARTS or part.startswith("build") for part in rel.parts):
            continue
        try:
            text = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue
        for lineno, line in enumerate(text.splitlines(), 1):
            if QT_PATTERN.search(line):
                violations.append(
                    f"{rel}:{lineno} 出现 Qt 引用: {line.strip()[:80]}"
                )

    if violations:
        print("许可证白名单检查失败：")
        for item in violations:
            print("  -", item)
        return 1

    print(f"许可证白名单检查通过（{len(allowed)} 项登记依赖，全仓无 Qt 引用）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
