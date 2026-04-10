#!/usr/bin/env python3
"""自动生成 `config/audio_player.yaml` 中 alias_paths 的辅助脚本。

脚本会扫描 assets/sounds 下的所有音频文件，基于相对路径生成唯一 alias，
并将自动生成的映射写入配置，在需要时保留原有的非 assets 路径 alias。
"""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path
from typing import Dict, Iterable

import yaml


SUPPORTED_EXTENSIONS = {
    ".aac",
    ".flac",
    ".m4a",
    ".mp3",
    ".ogg",
    ".opus",
    ".wav",
    ".wma",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="自动构建 audio_player alias_paths"
    )
    parser.add_argument(
        "--config",
        type=Path,
        default=Path("/home/pi/dolydev/config/audio_player.yaml"),
        help="audio_player 配置文件路径，相对于项目根目录或使用绝对路径",
    )
    parser.add_argument(
        "--assets",
        type=Path,
        default=Path("/home/pi/dolydev/assets/sounds"),
        help="音频资源根目录，相对于项目根目录或使用绝对路径",
    )
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="仅打印将要写入的 alias_paths 而不修改文件",
    )
    return parser.parse_args()


def sanitize_alias(raw: str) -> str:
    """只保留小写字母/数字/下划线，替换其它字符并压缩重复下划线。"""
    alias = raw.lower().replace(os.sep, "_")
    alias = re.sub(r"[^a-z0-9_]+", "_", alias)
    alias = re.sub(r"_+", "_", alias)
    alias = alias.strip("_")
    return alias or "audio"


def collect_audio_files(base: Path) -> Iterable[Path]:
    """递归收集支持的音频文件。"""
    for path in sorted(base.rglob("*")):
        if not path.is_file():
            continue
        if path.suffix.lower() in SUPPORTED_EXTENSIONS:
            yield path


def ensure_trailing_slash(path: str) -> str:
    if not path.endswith("/"):
        return path + "/"
    return path


def build_alias_map(
    assets_root: Path, files: Iterable[Path]
) -> Dict[str, str]:
    """根据相对于 assets 的路径生成 alias -> 相对 assets 的 path 映射。"""
    result: Dict[str, str] = {}
    used_aliases = set()
    for file_path in files:
        rel = file_path.relative_to(assets_root)
        name_without_suffix = rel.with_suffix("")
        alias_base = sanitize_alias(str(name_without_suffix.as_posix()))
        alias = alias_base
        counter = 1
        while alias in used_aliases:
            alias = f"{alias_base}_{counter}"
            counter += 1
        used_aliases.add(alias)
        result[alias] = rel.as_posix()
    return result


def normalize_manual_aliases(
    alias_paths: Dict[str, str], assets_root: Path, project_root: Path
) -> Dict[str, str]:
    """保留不在 assets 目录下的手动 alias 配置，避免被自动 alias 覆盖。"""
    manual = {}
    assets_root_resolved = assets_root.resolve()
    for alias, target in alias_paths.items():
        candidate = Path(target)
        if not candidate.is_absolute():
            candidate = (assets_root / candidate).resolve()
        if assets_root_resolved == candidate or assets_root_resolved in candidate.parents:
            continue
        else:
            manual[alias] = str(target)
    return manual


def main() -> None:
    args = parse_args()
    # determine project root based on config file location
    config_path = args.config
    if not config_path.is_absolute():
        # treat as path relative to project root candidate (current working directory)
        # discover root by walking up until we find 'config' directory
        config_path = (Path.cwd() / args.config)
    project_root = config_path.resolve().parent.parent

    # resolve assets path; allow absolute paths or relative to project_root
    assets_root = args.assets
    if not assets_root.is_absolute():
        assets_root = (project_root / assets_root)
    assets_root = assets_root.resolve()

    if not assets_root.is_dir():
        raise SystemExit(f"音频资源目录不存在：{assets_root}")

    with args.config.open("r", encoding="utf-8") as fp:
        data = yaml.safe_load(fp) or {}

    existing_alias = data.get("alias_paths", {}) if data else {}
    manual_alias = normalize_manual_aliases(existing_alias, assets_root, project_root)

    auto_alias = build_alias_map(assets_root, collect_audio_files(assets_root))

    merged = dict(sorted(auto_alias.items()))
    merged.update(manual_alias)

    data["alias_paths"] = merged
    data["path_prefix"] = ensure_trailing_slash(assets_root.as_posix())

    if args.dry_run:
        print("即将写入 alias_paths:")
        for alias, path in merged.items():
            print(f"{alias}: {path}")
        return

    # 写入前备份以便回溯
    backup_path = args.config.with_suffix(args.config.suffix + ".bak")
    args.config.replace(backup_path)
    with args.config.open("w", encoding="utf-8") as fp:
        yaml.safe_dump(data, fp, sort_keys=False)

    print(
        f"已更新 {args.config}，共生成 {len(auto_alias)} 条自动 alias，保留 {len(manual_alias)} 条手动 alias。"
    )


if __name__ == "__main__":
    main()