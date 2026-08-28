#!/usr/bin/env bash
set -euo pipefail

WORKSPACE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
RUNNER=(python3 "$WORKSPACE_ROOT/tools/vision_agent.py")

usage() {
  printf '%s\n' \
    'RoboMaster 视觉研究 Agent' \
    '' \
    '  ./agent.sh add <slug> "标题"       创建并填写研究任务' \
    '  ./agent.sh check                    检查框架并预览下一回合' \
    '  ./agent.sh once [slug]              前台运行一个回合' \
    '  ./agent.sh run [slug]               前台连续运行' \
    '  ./agent.sh start                    用 tmux 后台运行' \
    '  ./agent.sh status                   查看后台和任务状态' \
    '  ./agent.sh watch                    进入后台实时终端' \
    '  ./agent.sh stop                     当前回合结束后停止' \
    '  ./agent.sh prompt <slug>            显示完整回合提示词' \
    '  ./agent.sh set-status <slug> <状态> 人工改变状态'
}

command_name="${1:-help}"
case "$command_name" in
  add|start|status|watch|stop|prompt|set-status)
    shift
    exec "${RUNNER[@]}" "$command_name" "$@"
    ;;
  list)
    exec "${RUNNER[@]}" list
    ;;
  check)
    "${RUNNER[@]}" doctor
    "${RUNNER[@]}" list
    exec "${RUNNER[@]}" run --once --dry-run
    ;;
  once)
    shift
    if [[ $# -gt 1 ]]; then usage >&2; exit 2; fi
    if [[ $# -eq 1 ]]; then exec "${RUNNER[@]}" run --once --slug "$1"; fi
    exec "${RUNNER[@]}" run --once
    ;;
  run)
    shift
    if [[ $# -gt 1 ]]; then usage >&2; exit 2; fi
    if [[ $# -eq 1 ]]; then exec "${RUNNER[@]}" run --slug "$1"; fi
    exec "${RUNNER[@]}" run
    ;;
  help|-h|--help)
    usage
    ;;
  *)
    printf '错误：未知命令 %s\n' "$command_name" >&2
    usage >&2
    exit 2
    ;;
esac
