#!/usr/bin/env python3
"""可恢复的 RoboMaster 视觉研究任务调度器。仅依赖 Python 标准库。"""

from __future__ import annotations

import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess
import sys
import threading
import time

try:
    import tomllib
except ModuleNotFoundError:  # Python 3.10
    try:
        import tomli as tomllib
    except ModuleNotFoundError as exc:
        raise SystemExit(
            "Python 3.10 需要 tomli：python3 -m pip install -r requirements-agent.txt"
        ) from exc


ROOT = Path(os.environ.get("VISION_AGENT_ROOT", Path(__file__).resolve().parents[1])).resolve()
RESEARCH_ROOT = ROOT / "research"
TASKS_ROOT = RESEARCH_ROOT / "tasks"
PROJECTS_ROOT = RESEARCH_ROOT / "projects"
RUNTIME_ROOT = RESEARCH_ROOT / "runtime"
RUNNER_CONFIG = RESEARCH_ROOT / "runner.toml"
TASK_TEMPLATE = ROOT / "templates" / "vision-task"
PROJECT_TEMPLATE = ROOT / "templates" / "vision-project"
STOP_REQUEST = RUNTIME_ROOT / "stop-requested"

SLUG_RE = re.compile(r"[a-z0-9][a-z0-9._-]*")
INFORMATION_MODES = {"offline", "connected"}
IMPLEMENTATION_MODES = {"proposal", "workspace-change"}
KNOWN_STATUSES = {
    "draft",
    "queued",
    "researching",
    "needs-data",
    "needs-hardware-validation",
    "needs-human-review",
    "blocked",
    "candidate-complete",
    "accepted",
    "archived",
    "runtime-error",
}
RUNNABLE_STATUSES = {"queued", "researching"}
HUMAN_SETTABLE_STATUSES = KNOWN_STATUSES - {"researching", "runtime-error"}


def now_iso() -> str:
    return dt.datetime.now(dt.timezone.utc).astimezone().isoformat(timespec="seconds")


def validate_slug(slug: str) -> None:
    if not SLUG_RE.fullmatch(slug):
        raise ValueError("任务标识只能使用小写字母、数字、点、下划线和连字符。")


def load_toml(path: Path) -> dict:
    with path.open("rb") as handle:
        return tomllib.load(handle)


def render_text(content: str, replacements: dict[str, str]) -> str:
    for name, value in replacements.items():
        content = content.replace("{{" + name + "}}", value)
    return content


def load_runner_config() -> dict:
    if not RUNNER_CONFIG.is_file():
        raise RuntimeError(f"找不到调度配置：{RUNNER_CONFIG}")
    defaults = {
        "session_name": "restar_vision_agent",
        "model": "",
        "reasoning_effort": "high",
        "attempt_timeout_minutes": 45,
        "max_wall_hours": 8,
        "idle_seconds": 30,
        "max_idle_cycles": 1,
        "information_mode": "offline",
        "max_consecutive_runtime_failures": 3,
        "codex_path": "",
    }
    defaults.update(load_toml(RUNNER_CONFIG))
    mode = str(defaults["information_mode"]).strip()
    if mode not in INFORMATION_MODES:
        raise ValueError(f"未知 information_mode：{mode}")
    nonnegative = ("attempt_timeout_minutes", "max_wall_hours", "max_idle_cycles")
    for key in nonnegative:
        if float(defaults[key]) < 0:
            raise ValueError(f"{key} 不能为负数")
    if int(defaults["idle_seconds"]) < 1:
        raise ValueError("idle_seconds 必须至少为 1")
    if int(defaults["max_consecutive_runtime_failures"]) < 1:
        raise ValueError("max_consecutive_runtime_failures 必须至少为 1")
    return defaults


def task_dir(slug: str) -> Path:
    return TASKS_ROOT / slug


def _resolve_project_path(configured: str, projects_root: Path) -> Path:
    relative = Path(configured)
    if relative.is_absolute():
        raise ValueError("project_path 必须是仓库内的相对路径。")
    candidate = (ROOT / relative).resolve()
    try:
        candidate.relative_to(projects_root.resolve())
    except ValueError as exc:
        raise ValueError("project_path 必须位于 research/projects/。") from exc
    return candidate


def project_dir(item: dict) -> Path:
    configured = str(item.get("project_path", "")).strip()
    if configured:
        return _resolve_project_path(configured, PROJECTS_ROOT)
    return PROJECTS_ROOT / item["slug"]


def discover_tasks() -> list[dict]:
    found: list[dict] = []
    if not TASKS_ROOT.is_dir():
        return found
    for directory in sorted(TASKS_ROOT.iterdir()):
        if not directory.is_dir():
            continue
        config_path = directory / "config.toml"
        statement_path = directory / "task.md"
        if not config_path.is_file() or not statement_path.is_file():
            continue
        config = load_toml(config_path)
        slug = directory.name
        validate_slug(slug)
        item = {
            "slug": slug,
            "dir": directory,
            "title": str(config.get("title", slug)),
            "ready": bool(config.get("ready", False)),
            "enabled": bool(config.get("enabled", True)),
            "implementation_mode": str(config.get("implementation_mode", "proposal")).strip(),
            "information_mode": str(config.get("information_mode", "")).strip(),
            "priority": int(config.get("priority", 100)),
            "max_rounds": int(config.get("max_rounds", 0)),
            "project_path": str(config.get("project_path", "")).strip(),
        }
        if item["implementation_mode"] not in IMPLEMENTATION_MODES:
            raise ValueError(f"{slug} 的 implementation_mode 无效")
        if item["information_mode"] and item["information_mode"] not in INFORMATION_MODES:
            raise ValueError(f"{slug} 的 information_mode 无效")
        if item["max_rounds"] < 0:
            raise ValueError(f"{slug} 的 max_rounds 不能为负数")
        project_dir(item)
        found.append(item)
    return found


def status_path(item: dict) -> Path:
    return project_dir(item) / ".agent-status"


def read_status(item: dict) -> str:
    path = status_path(item)
    if path.is_file():
        return path.read_text(encoding="utf-8").strip()
    return "queued" if item["ready"] else "draft"


def write_status(item: dict, status: str) -> None:
    if status not in KNOWN_STATUSES:
        raise ValueError(f"未知任务状态：{status}")
    path = status_path(item)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(status + "\n", encoding="utf-8")


def runtime_state_path(item: dict) -> Path:
    return project_dir(item) / ".agent-runtime.json"


def read_runtime_state(item: dict) -> dict:
    path = runtime_state_path(item)
    if not path.is_file():
        return {"rounds": 0, "consecutive_runtime_failures": 0}
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise RuntimeError(f"运行状态损坏：{path}") from exc


def write_runtime_state(item: dict, state: dict) -> None:
    path = runtime_state_path(item)
    path.write_text(json.dumps(state, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def ensure_project(item: dict) -> Path:
    project = project_dir(item)
    marker = project / ".vision-agent-project.json"
    explicit = bool(item["project_path"])
    adopted_existing = project.exists() and not marker.is_file()
    if project.exists() and not project.is_dir():
        raise RuntimeError(f"研究项目路径不是目录：{project}")
    if project.exists() and not marker.is_file() and not explicit:
        raise RuntimeError(f"拒绝覆盖非 Agent 创建的目录：{project}")
    project.mkdir(parents=True, exist_ok=True)
    replacements = {
        "TITLE": item["title"],
        "TASK_PATH": str((item["dir"] / "task.md").relative_to(ROOT)),
    }
    for source in sorted(PROJECT_TEMPLATE.rglob("*")):
        relative = source.relative_to(PROJECT_TEMPLATE)
        destination = project / relative
        if source.is_dir():
            destination.mkdir(parents=True, exist_ok=True)
        elif not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_text(
                render_text(source.read_text(encoding="utf-8"), replacements),
                encoding="utf-8",
            )
    if not marker.exists():
        marker.write_text(
            json.dumps(
                {
                    "slug": item["slug"],
                    "source": str(item["dir"].relative_to(ROOT)),
                    "created_at": now_iso(),
                    "adopted_existing": adopted_existing and explicit,
                },
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    if not status_path(item).exists():
        write_status(item, "queued")
    if not runtime_state_path(item).exists():
        write_runtime_state(item, {"rounds": 0, "consecutive_runtime_failures": 0})
    return project


def create_input_snapshot(item: dict, project: Path) -> Path:
    sources = [item["dir"] / "task.md", item["dir"] / "config.toml"]
    digest = hashlib.sha256()
    for source in sources:
        digest.update(source.name.encode("utf-8"))
        digest.update(b"\0")
        digest.update(source.read_bytes())
        digest.update(b"\0")
    short_hash = digest.hexdigest()[:16]
    destination = project / "input-snapshots" / short_hash
    if not destination.exists():
        destination.mkdir(parents=True)
        for source in sources:
            shutil.copy2(source, destination / source.name)
        (destination / "SNAPSHOT.json").write_text(
            json.dumps(
                {"source": str(item["dir"].relative_to(ROOT)), "sha256": short_hash, "created_at": now_iso()},
                ensure_ascii=False,
                indent=2,
            )
            + "\n",
            encoding="utf-8",
        )
    (project / "CURRENT_INPUT.md").write_text(
        "# Current Input\n\n"
        f"- 任务源：`{item['dir'].relative_to(ROOT)}`\n"
        f"- 冻结快照：`input-snapshots/{short_hash}/`\n"
        f"- 更新时间：{now_iso()}\n",
        encoding="utf-8",
    )
    return destination


def effective_information_mode(item: dict, config: dict) -> str:
    return item["information_mode"] or str(config["information_mode"])


def build_prompt(item: dict, project: Path, snapshot: Path, information_mode: str) -> str:
    if item["implementation_mode"] == "proposal":
        scope = (
            f"本任务是 `proposal`：只能修改 {project} 内的研究文件。"
            "不得修改仓库源码、配置、模型、构建文件或 Git 状态。"
        )
    else:
        scope = (
            "本任务是 `workspace-change`：可按冻结 task.md 的“实现范围”修改仓库文件，"
            "超出范围必须停止并请求研究者确认。不得提交或推送 Git。"
        )
    if information_mode == "offline":
        information = (
            "本回合为 `offline`：不得访问公共互联网或新增外部资料。"
            "无法核验的外部事实只登记为待办，不得伪造引用或新颖性结论。"
        )
    else:
        information = (
            "本回合为 `connected`：可联网核查，优先原论文、官方文档和上游实现；"
            "记录稳定链接、版本、适用条件和许可证。"
        )
    return f"""你正在执行 restar-25 的一个独立 RoboMaster 视觉研究回合。

任务：{item['slug']} — {item['title']}
任务快照：{snapshot}
研究项目：{project}

开始任何实质工作前，完整读取：
1. {ROOT / 'AGENTS.md'}
2. {ROOT / 'agents/instructions/vision-research.md'}
3. {ROOT / 'agents/instructions/experiments-and-validation.md'}
4. 若修改代码，再读 {ROOT / 'agents/instructions/implementation.md'}
5. {snapshot / 'task.md'} 与 {snapshot / 'config.toml'}
6. {project} 中的 CURRENT_INPUT.md、README.md、progress.md、hypothesis-ledger.md、experiment-ledger.md、decisions.md 和 benchmark-report.md

{information}
{scope}

本回合要求：
- 从当前最小未闭合问题继续，不依赖旧聊天，也不从头重复已有失败路线。
- 先核对目标、基线、数据切分、指标定义、单位、护栏和验收门槛；有实质歧义时将状态设为 needs-human-review。
- 先复现基线。缺少数据时设为 needs-data；不得猜测或编造指标。
- 在 hypothesis-ledger.md 保留至少两个机制不同的候选方案，并选择信息增益最高的一条做最小可证伪实验。
- 同时审计检测、几何解算、跟踪/预测、规划/火控和端到端延迟中相关的误差来源。
- 所有实验记录代码状态、数据/录像 ID、配置、命令、随机种子、环境、指标、失败样本和产物路径。
- 不连接或控制真实相机、串口、CAN、云台、摩擦轮、拨弹机构或 ROS2 机器人节点，不运行 autostart.sh，不安装系统包。
- 如果下一步必须台架或实车验证，准备安全检查和精确步骤，将状态设为 needs-hardware-validation，等待研究者。
- 性能结论标注证据等级。你最多可升级到 benchmark-supported，不能自行写成 hardware-validated 或 competition-validated。
- 修改源码时保持最小差异，运行当前环境支持的最高层级检查；依赖或硬件缺失必须明确记录。

结束前必须：
1. 追加 progress.md；
2. 同步 hypothesis-ledger.md、experiment-ledger.md、decisions.md、benchmark-report.md 和 README.md；
3. 把较长分析放入 notes/，把小型可提交产物放入 artifacts/；
4. 在 {project / '.agent-status'} 写入合法状态；有明确下一步时保持 researching，达到全部离线门槛后才可写 candidate-complete；
5. 最终简要报告产物、检查、证据等级、风险和下一步。
"""


def locate_codex(config: dict) -> str:
    explicit = str(config.get("codex_path", "")).strip()
    if explicit:
        path = Path(explicit).expanduser()
        if not path.is_file() or not os.access(path, os.X_OK):
            raise RuntimeError(f"codex_path 不可执行：{path}")
        return str(path)
    found = shutil.which("codex")
    if not found:
        raise RuntimeError("PATH 中找不到 codex；请安装 Codex CLI 或设置 research/runner.toml 的 codex_path。")
    return found


def codex_command(config: dict, prompt: str, last_message: Path, information_mode: str) -> list[str]:
    command = [locate_codex(config), "-C", str(ROOT), "-s", "workspace-write", "-a", "never"]
    if information_mode == "connected":
        command.append("--search")
    model = str(config.get("model", "")).strip()
    if model:
        command.extend(["-m", model])
    effort = str(config.get("reasoning_effort", "")).strip()
    if effort:
        command.extend(["-c", f'model_reasoning_effort="{effort}"'])
    command.extend(
        [
            "exec",
            "--skip-git-repo-check",
            "--ephemeral",
            "--json",
            "--output-last-message",
            str(last_message),
            prompt,
        ]
    )
    return command


def run_process(command: list[str], log_path: Path, timeout_seconds: int) -> tuple[int, bool]:
    timed_out = False
    with log_path.open("w", encoding="utf-8") as log:
        process = subprocess.Popen(
            command,
            cwd=ROOT,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            bufsize=1,
        )

        def stream() -> None:
            assert process.stdout is not None
            for line in process.stdout:
                print(line, end="", flush=True)
                log.write(line)
                log.flush()

        thread = threading.Thread(target=stream, daemon=True)
        thread.start()
        try:
            process.wait(timeout=timeout_seconds if timeout_seconds > 0 else None)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        thread.join(timeout=5)
        if process.stdout is not None:
            process.stdout.close()
    return process.returncode, timed_out


def execute_round(item: dict, config: dict, dry_run: bool = False) -> int:
    information_mode = effective_information_mode(item, config)
    state = read_runtime_state(item)
    round_number = int(state.get("rounds", 0)) + 1
    project = project_dir(item)
    logs = RUNTIME_ROOT / "logs" / item["slug"]
    timestamp = dt.datetime.now().strftime("%Y%m%d-%H%M%S")
    event_log = logs / f"round-{round_number:04d}-{timestamp}.jsonl"
    last_message = logs / f"round-{round_number:04d}-{timestamp}-last.md"
    if dry_run:
        print(f"[dry-run] 任务：{item['slug']} — {item['title']}")
        print(f"[dry-run] 项目：{project}")
        print(f"[dry-run] 信息模式：{information_mode}")
        print(f"[dry-run] 实现模式：{item['implementation_mode']}")
        try:
            command = codex_command(config, "<研究回合提示词>", last_message, information_mode)
            print(f"[dry-run] 命令：{shlex.join(command[:-1])} <研究回合提示词>")
        except RuntimeError as exc:
            print(f"[dry-run] 命令暂不可用：{exc}")
        return 0

    project = ensure_project(item)
    snapshot = create_input_snapshot(item, project)
    logs.mkdir(parents=True, exist_ok=True)
    prompt = build_prompt(item, project, snapshot, information_mode)
    command = codex_command(config, prompt, last_message, information_mode)
    write_status(item, "researching")
    started_at = now_iso()
    print(f"[{started_at}] 开始 {item['slug']} 第 {round_number} 回合", flush=True)
    timeout_seconds = int(config["attempt_timeout_minutes"]) * 60
    return_code, timed_out = run_process(command, event_log, timeout_seconds)
    state["rounds"] = round_number
    state["last_started_at"] = started_at
    state["last_finished_at"] = now_iso()
    state["last_return_code"] = return_code
    state["last_timed_out"] = timed_out
    state["last_information_mode"] = information_mode
    state["last_event_log"] = str(event_log.relative_to(ROOT))
    state["last_message"] = str(last_message.relative_to(ROOT)) if last_message.is_file() else None
    if return_code == 0 and not timed_out:
        state["consecutive_runtime_failures"] = 0
    else:
        state["consecutive_runtime_failures"] = int(state.get("consecutive_runtime_failures", 0)) + 1
        if state["consecutive_runtime_failures"] >= int(config["max_consecutive_runtime_failures"]):
            write_status(item, "runtime-error")
    status = read_status(item)
    if status not in KNOWN_STATUSES:
        state["invalid_agent_status"] = status
        write_status(item, "needs-human-review")
        status = "needs-human-review"
    elif status == "accepted":
        state["invalid_agent_status"] = "Agent 不能自行设置 accepted"
        write_status(item, "needs-human-review")
        status = "needs-human-review"
    write_runtime_state(item, state)
    print(f"[{state['last_finished_at']}] 结束：return={return_code} status={status}", flush=True)
    return return_code


def eligible_tasks(items: list[dict]) -> list[dict]:
    eligible: list[dict] = []
    for item in items:
        if not item["ready"] or not item["enabled"]:
            continue
        if read_status(item) not in RUNNABLE_STATUSES:
            continue
        rounds = int(read_runtime_state(item).get("rounds", 0))
        if item["max_rounds"] and rounds >= item["max_rounds"]:
            continue
        eligible.append(item)
    return sorted(
        eligible,
        key=lambda item: (-item["priority"], int(read_runtime_state(item).get("rounds", 0)), item["slug"]),
    )


def add_task(args: argparse.Namespace) -> int:
    validate_slug(args.slug)
    destination = task_dir(args.slug)
    if destination.exists():
        raise RuntimeError(f"任务已存在：{destination}")
    destination.mkdir(parents=True)
    replacements = {"TITLE": args.title or args.slug}
    for source in TASK_TEMPLATE.iterdir():
        if source.is_file():
            (destination / source.name).write_text(
                render_text(source.read_text(encoding="utf-8"), replacements), encoding="utf-8"
            )
    print(f"已创建：{destination.relative_to(ROOT)}")
    print("填写 task.md 并把 config.toml 的 ready 改为 true。")
    return 0


def list_tasks() -> int:
    items = discover_tasks()
    if not items:
        print("当前没有研究任务。使用 ./agent.sh add <slug> \"标题\" 创建。")
        return 0
    print(f"{'SLUG':24} {'STATUS':28} {'ROUNDS':>6}  MODE")
    for item in items:
        rounds = int(read_runtime_state(item).get("rounds", 0))
        mode = item["implementation_mode"]
        print(f"{item['slug'][:24]:24} {read_status(item)[:28]:28} {rounds:6d}  {mode}")
    return 0


def doctor() -> int:
    required = [
        ROOT / "AGENTS.md",
        ROOT / "agents/instructions/vision-research.md",
        ROOT / "agents/instructions/implementation.md",
        ROOT / "agents/instructions/experiments-and-validation.md",
        ROOT / "requirements-agent.txt",
        RUNNER_CONFIG,
        TASK_TEMPLATE / "task.md",
        TASK_TEMPLATE / "config.toml",
        PROJECT_TEMPLATE / "README.md",
    ]
    failed = False
    for path in required:
        if not path.is_file():
            print(f"缺少：{path.relative_to(ROOT)}", file=sys.stderr)
            failed = True
    if sys.version_info < (3, 10):
        print("需要 Python 3.10+。", file=sys.stderr)
        failed = True
    load_runner_config()
    discover_tasks()
    if shutil.which("codex") is None and not str(load_runner_config().get("codex_path", "")).strip():
        print("提示：当前未找到 Codex CLI；框架检查可通过，但运行回合前需要安装或配置路径。")
    if shutil.which("tmux") is None:
        print("提示：当前未找到 tmux；前台 once/run 可用，后台 start/watch 不可用。")
    if failed:
        print("Agent 框架检查失败。", file=sys.stderr)
        return 1
    print("Agent 框架检查通过。")
    return 0


def consume_stop_request() -> bool:
    if not STOP_REQUEST.exists():
        return False
    STOP_REQUEST.unlink()
    return True


def run_loop(args: argparse.Namespace) -> int:
    config = load_runner_config()
    started = time.monotonic()
    idle_cycles = 0
    while True:
        if consume_stop_request():
            print("收到停止请求，安全退出。")
            return 0
        items = discover_tasks()
        if args.slug:
            items = [item for item in items if item["slug"] == args.slug]
            if not items:
                raise RuntimeError(f"找不到任务：{args.slug}")
        candidates = eligible_tasks(items)
        if not candidates:
            if args.once or args.dry_run:
                print("没有可运行任务。")
                return 0
            idle_cycles += 1
            max_idle = int(config["max_idle_cycles"])
            if max_idle and idle_cycles >= max_idle:
                print("没有可运行任务，结束扫描。")
                return 0
            time.sleep(max(1, int(config["idle_seconds"])))
            continue
        idle_cycles = 0
        result = execute_round(candidates[0], config, dry_run=args.dry_run)
        if args.once or args.dry_run:
            return result
        max_hours = float(config["max_wall_hours"])
        if max_hours and time.monotonic() - started >= max_hours * 3600:
            print("达到本次运行总时限。")
            return 0


def session_name(config: dict) -> str:
    name = str(config["session_name"]).strip()
    if not name or not re.fullmatch(r"[A-Za-z0-9_.-]+", name):
        raise ValueError("session_name 只能使用字母、数字、点、下划线和连字符。")
    return name


def tmux_session_exists(name: str) -> bool:
    tmux = shutil.which("tmux")
    return bool(tmux and subprocess.run([tmux, "has-session", "-t", name], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0)


def start_runner() -> int:
    config = load_runner_config()
    locate_codex(config)
    tmux = shutil.which("tmux")
    if not tmux:
        raise RuntimeError("PATH 中找不到 tmux；可使用 ./agent.sh run 前台运行。")
    name = session_name(config)
    if tmux_session_exists(name):
        raise RuntimeError(f"tmux 会话已存在：{name}")
    STOP_REQUEST.unlink(missing_ok=True)
    subprocess.run([tmux, "new-session", "-d", "-s", name, sys.executable, str(Path(__file__).resolve()), "run"], check=True)
    print(f"已启动后台会话：{name}；使用 ./agent.sh watch 查看。")
    return 0


def stop_runner() -> int:
    RUNTIME_ROOT.mkdir(parents=True, exist_ok=True)
    STOP_REQUEST.write_text(now_iso() + "\n", encoding="utf-8")
    print("已请求停止；当前回合完成后退出。")
    return 0


def show_status() -> int:
    config = load_runner_config()
    name = session_name(config)
    print(f"后台会话：{'running' if tmux_session_exists(name) else 'stopped'} ({name})")
    return list_tasks()


def watch_runner() -> int:
    config = load_runner_config()
    tmux = shutil.which("tmux")
    if not tmux:
        raise RuntimeError("PATH 中找不到 tmux。")
    name = session_name(config)
    if not tmux_session_exists(name):
        raise RuntimeError(f"后台会话未运行：{name}")
    os.execvp(tmux, [tmux, "attach-session", "-t", name])
    return 0


def set_status(args: argparse.Namespace) -> int:
    items = {item["slug"]: item for item in discover_tasks()}
    if args.slug not in items:
        raise RuntimeError(f"找不到任务：{args.slug}")
    item = items[args.slug]
    ensure_project(item)
    write_status(item, args.status)
    print(f"{args.slug} -> {args.status}")
    return 0


def show_prompt(args: argparse.Namespace) -> int:
    items = {item["slug"]: item for item in discover_tasks()}
    if args.slug not in items:
        raise RuntimeError(f"找不到任务：{args.slug}")
    item = items[args.slug]
    project = ensure_project(item)
    snapshot = create_input_snapshot(item, project)
    print(build_prompt(item, project, snapshot, effective_information_mode(item, load_runner_config())))
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="restar-25 RoboMaster 视觉研究 Agent")
    sub = parser.add_subparsers(dest="command", required=True)
    add = sub.add_parser("add", help="创建研究任务")
    add.add_argument("slug")
    add.add_argument("title", nargs="?")
    sub.add_parser("list", help="列出任务")
    sub.add_parser("doctor", help="只读框架检查")
    run = sub.add_parser("run", help="前台运行队列")
    run.add_argument("--once", action="store_true", help="只运行一个回合")
    run.add_argument("--dry-run", action="store_true", help="只展示下一回合")
    run.add_argument("--slug", help="只调度指定任务")
    sub.add_parser("start", help="用 tmux 后台运行")
    sub.add_parser("stop", help="当前回合结束后停止")
    sub.add_parser("status", help="显示队列状态")
    sub.add_parser("watch", help="进入 tmux 实时终端")
    status = sub.add_parser("set-status", help="人工设置任务状态")
    status.add_argument("slug")
    status.add_argument("status", choices=sorted(HUMAN_SETTABLE_STATUSES))
    prompt = sub.add_parser("prompt", help="生成并显示某任务的完整回合提示词")
    prompt.add_argument("slug")
    return parser


def main() -> int:
    args = build_parser().parse_args()
    try:
        if args.command == "add":
            return add_task(args)
        if args.command == "list":
            return list_tasks()
        if args.command == "doctor":
            return doctor()
        if args.command == "run":
            return run_loop(args)
        if args.command == "start":
            return start_runner()
        if args.command == "stop":
            return stop_runner()
        if args.command == "status":
            return show_status()
        if args.command == "watch":
            return watch_runner()
        if args.command == "set-status":
            return set_status(args)
        if args.command == "prompt":
            return show_prompt(args)
    except (OSError, RuntimeError, ValueError, tomllib.TOMLDecodeError) as exc:
        print(f"错误：{exc}", file=sys.stderr)
        return 1
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
