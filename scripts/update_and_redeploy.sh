#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(git -C "$SCRIPT_DIR/.." rev-parse --show-toplevel 2>/dev/null || true)"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() { printf '%b\n' "${BLUE}[INFO]${NC} $1"; }
print_success() { printf '%b\n' "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { printf '%b\n' "${YELLOW}[WARNING]${NC} $1"; }
print_error() { printf '%b\n' "${RED}[ERROR]${NC} $1"; }

confirm() {
    local prompt="$1"
    local answer
    read -r -p "$prompt [y/N] " answer
    case "$answer" in
        y|Y|yes|YES) return 0 ;;
        *) return 1 ;;
    esac
}

require_repo_root() {
    if [[ -z "$REPO_ROOT" ]]; then
        print_error "无法定位 Git 仓库根目录，请确认脚本位于 /home/pi/dolydev/scripts 下。"
        exit 1
    fi

    cd "$REPO_ROOT"
    if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        print_error "当前目录不是 Git 仓库：$REPO_ROOT"
        exit 1
    fi
}

reset_to_remote() {
    local remote_ref="$1"

    print_warning "将本地仓库重置为 $remote_ref，并清理未跟踪文件。"
    git reset --hard "$remote_ref"
    git clean -fd
    print_success "仓库已重置到远端最新状态。"
}

sync_repository() {
    local current_branch upstream_ref remote_ref ahead behind dirty

    current_branch="$(git symbolic-ref --short -q HEAD || true)"
    if [[ -z "$current_branch" ]]; then
        print_error "当前处于 detached HEAD，无法自动更新。"
        exit 1
    fi

    upstream_ref="$(git rev-parse --abbrev-ref --symbolic-full-name '@{u}' 2>/dev/null || true)"
    remote_ref="${upstream_ref:-origin/${current_branch}}"

    if ! git rev-parse --verify "$remote_ref" >/dev/null 2>&1; then
        print_error "找不到远端分支 $remote_ref，请先确认本地分支已经跟踪 origin。"
        exit 1
    fi

    print_status "抓取远端最新代码..."
    git fetch --all --prune

    read -r ahead behind < <(git rev-list --left-right --count "HEAD...${remote_ref}")
    dirty=0
    if [[ -n "$(git status --porcelain)" ]]; then
        dirty=1
    fi

    print_status "当前分支: ${current_branch}"
    print_status "与 ${remote_ref} 的差异: ahead=${ahead}, behind=${behind}, dirty=${dirty}"

    if [[ "$dirty" -eq 0 && "$ahead" -eq 0 && "$behind" -gt 0 ]]; then
        print_status "工作区干净，执行 fast-forward 更新。"
        git merge --ff-only "$remote_ref"
        print_success "仓库已更新到最新代码。"
        return 0
    fi

    print_warning "检测到本地修改或分支已分叉。"
    git status --short

    while true; do
        printf '%s\n' "请选择更新方式:"
        printf '%s\n' "  [m] merge  - 保留本地修改并尝试合并远端更新"
        printf '%s\n' "  [r] reset  - 放弃本地修改，直接重置到远端最新代码"
        printf '%s\n' "  [a] abort  - 退出"
        read -r -p "> " choice

        case "${choice:-a}" in
            m|M|merge|MERGE)
                print_status "执行合并更新..."
                if git pull --no-rebase --autostash origin "$current_branch"; then
                    print_success "仓库已合并到最新代码。"
                    return 0
                fi
                print_error "合并失败，可能存在冲突。"
                git status --short || true
                if confirm "是否改为重置到远端最新代码？"; then
                    reset_to_remote "$remote_ref"
                    return 0
                fi
                print_error "更新未完成。"
                exit 1
                ;;
            r|R|reset|RESET)
                if confirm "确认放弃本地修改并重置到远端最新代码？"; then
                    reset_to_remote "$remote_ref"
                    return 0
                fi
                ;;
            a|A|abort|ABORT|q|Q|quit|QUIT)
                print_error "用户取消更新。"
                exit 1
                ;;
            *)
                print_warning "无效输入，请重新选择。"
                ;;
        esac
    done
}

redeploy_module() {
    local module_name="$1"
    local script_rel="$2"
    local script_path="$REPO_ROOT/$script_rel"
    local script_dir script_name

    if [[ ! -f "$script_path" ]]; then
        print_error "未找到模块脚本: $script_path"
        exit 1
    fi

    script_dir="$(dirname "$script_path")"
    script_name="$(basename "$script_path")"

    print_status "开始重新部署: $module_name"
    if [[ -x "$script_path" ]]; then
        (cd "$script_dir" && "./$script_name" redeploy)
    else
        (cd "$script_dir" && bash "$script_name" redeploy)
    fi
    print_success "$module_name 重新部署完成"
}

main() {
    require_repo_root

    print_status "仓库根目录: $REPO_ROOT"
    sync_repository

    local modules=(
        "audio_player|libs/audio_player/scripts/manage_audio_player_service.sh"
        "drive|libs/drive/scripts/manage_service.sh"
        "EyeEngine|modules/eyeEngine/scripts/manage_service.sh"
        "daemon|modules/doly/scripts/manage_service.sh"
        "fan|libs/fan/scripts/manage_service.sh"
        "nlu/doly-nlpjs|libs/nlu/doly-nlpjs/scripts/manage_service.sh"
        "serial|libs/serial/scripts/manage_serial_service.sh"
        "tts/edge-tts|libs/tts/edge-tts/scripts/manage_service.sh"
        "widgets|libs/widgets/scripts/manage_service.sh"
    )

    print_status "开始重新编译并部署服务..."
    for entry in "${modules[@]}"; do
        local module_name script_rel
        module_name="${entry%%|*}"
        script_rel="${entry#*|}"
        redeploy_module "$module_name" "$script_rel"
    done

    print_success "所有模块已完成更新和重新部署。"
}

main "$@"
