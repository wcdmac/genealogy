#!/usr/bin/env python3
"""备份恢复演练（M6-3 验收：「备份恢复演练通过」）。

做什么：灌一份合成谱 → 备份 → 把库整个删掉（模拟误删/磁盘事故）
→ 恢复 → 逐项核对（人物数、宗族、事件一致），再验 --force 保护
（目标已存在时拒盖；--force 把旧库移开为 .bak 而非删除）。

为什么用「删整个库文件」模拟事故：半损坏的库（拷到一半的 WAL）
VACUUM INTO 备份恰好治不了——快照只对「备份之前的一致状态」负责，
之后的事故靠「还有一份完整备份」兜底。演练要证明的正是后者。

用法：
  python3 tools/drill_backup_restore.py [--genea build/bin/genea.exe]

退出码：0 演练通过；1 有核对项失败；2 环境问题。
"""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DEFAULT_GENEA = ROOT / "build" / "bin" / ("genea.exe" if sys.platform == "win32" else "genea")

failures = 0


def check(label: str, condition: bool, detail: object = None) -> None:
    global failures
    if condition:
        print(f"  ok  {label}")
    else:
        failures += 1
        suffix = "" if detail is None else f" — {detail}"
        print(f"  FAIL {label}{suffix}")


def run(genea: Path, *args: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(genea), *args],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
    )


def person_count(genea: Path, db: Path) -> int | None:
    # --limit 拉全：默认 50 只是第一页，拿它比对会漏
    result = run(genea, "person", "list", "--db", str(db), "--limit", "5000",
                 "--format", "json")
    if result.returncode != 0:
        return None
    try:
        payload = json.loads(result.stdout)
    except json.JSONDecodeError:
        return None
    # 列表信封 {"data":[…],"page":{…}}；老版本是裸数组，两者都容
    if isinstance(payload, dict):
        return len(payload.get("data", []))
    if isinstance(payload, list):
        return len(payload)
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description="备份恢复演练")
    parser.add_argument("--genea", default=str(DEFAULT_GENEA), help="genea 可执行文件路径")
    parser.add_argument("--persons", type=int, default=500, help="合成谱人数")
    args = parser.parse_args()

    genea = Path(args.genea)
    if not genea.is_file():
        print(f"[drill] 找不到 {genea}，先构建 CLI")
        return 2

    workdir = Path(tempfile.mkdtemp(prefix="genealogy-drill-"))
    try:
        db = workdir / "family.db"
        backup_dir = workdir / "backup"

        print(f"[drill] 工作目录 {workdir}")

        # 1. 灌库
        result = run(genea, "db", "init", "--db", str(db))
        check("db init", result.returncode == 0, result.stderr.strip())
        result = run(genea, "db", "seed", "--db", str(db), "--persons", str(args.persons))
        check(f"db seed（{args.persons} 人）", result.returncode == 0, result.stderr.strip())

        baseline = person_count(genea, db)
        check("基线人物数可读", baseline is not None and baseline > 0, baseline)
        if baseline is None:
            return 1

        # 2. 备份
        result = run(genea, "db", "backup", "--db", str(db), "--out", str(backup_dir))
        check("db backup", result.returncode == 0, result.stderr.strip())
        snapshots = list(backup_dir.glob("*.db"))
        check("备份目录有快照", len(snapshots) == 1, [p.name for p in snapshots])
        if not snapshots:
            return 1

        # 3. 事故：整个库删掉（连同 WAL/SHM）
        # 注意不跑 db init 探活——open_db 对不存在的路径是「建新库」
        # 语义，跑了反而把目标占住，恢复时被 --force 保护正确拒盖。
        for suffix in ("", "-wal", "-shm"):
            target = Path(str(db) + suffix)
            if target.exists():
                target.unlink()
        check("事故后库不存在", not db.exists())

        # 4. 恢复（目标不存在，无需 --force）
        result = run(genea, "db", "restore", "--db", str(db), "--from", str(backup_dir))
        check("db restore", result.returncode == 0, result.stderr.strip())

        # 5. 核对
        restored = person_count(genea, db)
        check("恢复后人物数与基线一致", restored == baseline, f"{restored} vs {baseline}")
        schema = run(genea, "db", "init", "--db", str(db))
        check("恢复后 schema 就绪", schema.returncode == 0, schema.stderr.strip())

        # 6. --force 保护：不带 force 恢复已存在的库 → 拒盖（退出码 4）
        result = run(genea, "db", "restore", "--db", str(db), "--from", str(backup_dir))
        check("目标已存且无 --force 被拒（4）", result.returncode == 4, result.returncode)
        result = run(genea, "db", "restore", "--db", str(db), "--from", str(backup_dir), "--force")
        check("--force 恢复成功", result.returncode == 0, result.stderr.strip())
        check("--force 后旧库留 .bak", any(workdir.glob("family.db.bak-*")))
        after_force = person_count(genea, db)
        check("--force 恢复后数据仍一致", after_force == baseline, f"{after_force} vs {baseline}")

        print("\n[drill] 全部通过" if failures == 0 else f"\n[drill] {failures} 项失败")
        return 0 if failures == 0 else 1
    finally:
        shutil.rmtree(workdir, ignore_errors=True)


if __name__ == "__main__":
    sys.exit(main())
