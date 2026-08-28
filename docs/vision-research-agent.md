# restar-25 视觉研究 Agent 使用手册

## 这套 Agent 是什么

它是一套放在视觉代码仓库里的“研究操作系统”，由四层组成：

1. `AGENTS.md`：Codex 进入仓库时自动读取的总规则；
2. `agents/instructions/`：研究、实现、实验三个按需加载的专业流程；
3. `research/tasks/` 与 `research/projects/`：把研究者输入和 Agent 长期状态分开保存；
4. `tools/vision_agent.py` 与 `agent.sh`：创建任务、冻结输入、生成提示词、执行有界回合和恢复队列。

它不会训练一个新模型，也不是常驻机器人控制程序。它调用本机已有的 Codex CLI，在仓库规则约束下研究和修改代码。对话结束后，下一次运行会从项目台账和冻结输入继续。

参考设计来自 [graph-geometry-research-workspace](https://github.com/coker412/graph-geometry-research-workspace)，但状态、证据等级、安全边界、指标和模板都已改写为 RoboMaster 视觉工程语义。

## 为什么分成任务与项目

`research/tasks/<slug>/` 是研究者控制的正式需求：问题、基线、数据、指标、允许修改范围和硬件验收项。Agent 每轮只读取它的不可变快照，不应在实验失败后修改目标。

`research/projects/<slug>/` 是 Agent 的工作记忆：

```text
research/projects/<slug>/
├── README.md                 # 当前状态和最小缺口
├── CURRENT_INPUT.md          # 当前冻结输入指针
├── progress.md               # 每轮进展
├── hypothesis-ledger.md      # 候选机制和证伪结果
├── experiment-ledger.md      # 可复现实验
├── decisions.md              # 采用、拒绝、回退决定
├── benchmark-report.md       # 基线与候选对比
├── input-snapshots/          # task.md/config.toml 内容快照
├── notes/                    # 长分析和失败样本说明
├── artifacts/                # 小型、可提交的实验产物
├── .agent-status             # 状态机
└── .agent-runtime.json       # 回合和运行故障计数
```

运行日志写到 `research/runtime/`，默认不进入 Git。

## 环境要求

- Python 3.10 或更高版本；Python 3.10 通过 `tomli` 兼容读取 TOML；
- Codex CLI，并已完成正常登录；
- Git；
- `tmux` 仅后台模式需要；
- 编译视觉代码还需要项目 README 中列出的 OpenCV、Eigen、OpenVINO、Ceres、相机 SDK 等依赖。

先安装 Agent 的 Python 兼容依赖。Python 3.11+ 会自动跳过 `tomli`：

```bash
python3 -m pip install -r requirements-agent.txt
```

先运行只读检查：

```bash
./agent.sh check
```

如果缺少 Codex CLI，框架检查仍会指出模板和配置是否健康，但不能执行研究回合。如果缺少 `tmux`，`once` 和前台 `run` 仍可用。

## 第一个研究任务

### 1. 创建

```bash
./agent.sh add tracker-outlier "降低高速小陀螺场景的跟踪发散"
```

这会生成：

```text
research/tasks/tracker-outlier/
├── task.md
└── config.toml
```

### 2. 填写任务卡

至少填写：

- 可复现基线和命令；
- 数据/录像 ID 与切分方式；
- 指标定义、单位、目标值和护栏；
- 允许修改的目录；
- CPU/GPU、实时性和平台限制；
- 已知失败样本；
- 台架和实车需要人工完成的项目。

一个合格的目标不是“优化跟踪”，而是类似：

> 在冻结的 `spin-v1-report` 录像集上，将 10–14 rad/s 场景的跟踪发散率从基线降低至少 30%，同时创轨时间 P95 退化不超过 5 ms，端到端延迟 P95 退化不超过 1 ms。

所有数字都应来自真实测量或明确标为待测，不能照抄示例。

### 3. 选择权限模式

`config.toml` 默认：

```toml
implementation_mode = "proposal"
```

此时 Agent 只能更新 `research/projects/<slug>/`，适合先做问题审计、方案设计和实验计划。

只有希望它实际改源码时才改成：

```toml
implementation_mode = "workspace-change"
```

同时必须在 `task.md` 的“实现范围”明确允许的目录和禁止项。Agent 仍不能提交或推送、控制硬件或安装系统依赖。

### 4. 选择信息模式

默认继承 `research/runner.toml` 的离线模式：

```toml
information_mode = "offline"
```

适合先独立分析已有代码和数据。需要查论文、官方文档或其他开源实现时，在单任务配置中改为：

```toml
information_mode = "connected"
```

联网结果仍须记录来源、版本、适用条件和许可证。

### 5. 入队并预览

完成 `task.md` 后，将：

```toml
ready = false
```

改为：

```toml
ready = true
```

然后预览，不调用 Codex：

```bash
./agent.sh check
./agent.sh prompt tracker-outlier
```

`prompt` 会初始化研究项目并冻结当前输入，方便人工审查完整指令。

### 6. 运行

先只运行一轮：

```bash
./agent.sh once tracker-outlier
```

检查 `git diff`、研究台账和实验结果后，再决定是否继续：

```bash
./agent.sh run tracker-outlier
```

后台公平轮询所有可运行任务：

```bash
./agent.sh start
./agent.sh status
./agent.sh watch
./agent.sh stop
```

`stop` 会等待当前 Codex 回合结束，不会在写台账中途强杀。按 `Ctrl-b` 再按 `d` 可以离开 tmux 查看而不停止任务。

## 状态机

| 状态 | 含义 | 谁可以设置 |
|---|---|---|
| `draft` | 输入未完成或 `ready = false` | 配置自然产生 |
| `queued` | 已准备，等待第一轮 | Runner/研究者 |
| `researching` | 仍有明确离线动作 | Agent |
| `needs-data` | 缺少决定性数据或标注 | Agent/研究者 |
| `needs-hardware-validation` | 下一证据必须来自台架/实车 | Agent/研究者 |
| `needs-human-review` | 目标歧义、许可、权衡或高风险决定需确认 | Agent/研究者 |
| `blocked` | 已知方案均有精确结构性阻塞 | Agent/研究者 |
| `candidate-complete` | 全部离线门槛通过，等待人工确认 | Agent |
| `accepted` | 研究者接受结论或改动 | 仅研究者 |
| `archived` | 不再继续调度 | 研究者 |
| `runtime-error` | 连续 CLI/超时故障达到阈值 | Runner |

人工恢复或归档：

```bash
./agent.sh set-status tracker-outlier queued
./agent.sh set-status tracker-outlier archived
./agent.sh set-status tracker-outlier accepted
```

## 证据如何升级

研究结论从 `hypothesis` 开始。完成静态审查只能到 `code-reviewed`；固定输入可复现后到 `offline-reproduced`；相对冻结基线达到预先声明门槛后到 `benchmark-supported`。

`hardware-validated` 和 `competition-validated` 必须由研究者根据真实记录确认。Agent 不能因为离线视频表现好，就宣称命中率或击杀时间在实车上提升。

## 配置参考

`research/runner.toml` 控制所有任务的默认运行方式：

| 字段 | 含义 |
|---|---|
| `model` | 留空使用 Codex CLI 默认模型 |
| `reasoning_effort` | 推理强度 |
| `attempt_timeout_minutes` | 单回合超时，0 为无限制 |
| `max_wall_hours` | 一次连续运行总时限 |
| `idle_seconds` | 无任务时重新扫描间隔 |
| `max_idle_cycles` | 连续空扫描退出阈值，0 为持续等待 |
| `information_mode` | 全局 `offline` 或 `connected` |
| `max_consecutive_runtime_failures` | 自动转入 `runtime-error` 的阈值 |
| `codex_path` | 可选 Codex CLI 绝对路径 |

单任务 `config.toml` 控制 `ready`、`enabled`、实现权限、信息模式、优先级和累计回合上限。调度时先看优先级，再优先选择累计回合更少的任务，避免一个任务永久独占。

## 直接交互式使用

不使用队列也可以在仓库根目录启动 Codex。根级 `AGENTS.md` 会提供工程边界，Agent 会按任务读取对应细则。队列的额外价值是冻结输入、维护状态、限制回合和在多次运行之间恢复。

## 安全提醒

以下操作永远不由队列自动执行：

- 打开真实相机或机器人节点；
- 发送串口、CAN、云台或发射命令；
- 运行自启动脚本、修改 udev、设备权限或系统包；
- 用私有比赛录像、未获许可数据或凭据写入仓库；
- 将分支提交、推送或公开发布。

需要硬件验证时，Agent 只准备检查表、命令、预期输出、中止条件和回退版本，由现场研究者执行。

## 常见问题

### `check` 通过，但 `once` 找不到 Codex

安装并登录 Codex CLI，或在 `research/runner.toml` 填写 `codex_path`。框架检查允许在没有 CLI 的机器上先审查模板。

### 任务没有运行

检查 `ready = true`、`enabled = true`、状态属于 `queued/researching`，且没有达到 `max_rounds`。

### CMake 构建失败

先区分代码错误与 OpenVINO、Ceres、OpenCV、相机 SDK 等环境缺失。Agent 应记录缺失依赖，并继续完成语法、Python 测试或可用模块检查，不得伪装成全量构建通过。

### 如何撤销一次实验改动

先用 `git diff` 确定该回合改动。Agent 不自动提交，所以可以只回退明确属于该回合的文件；不要用破坏性命令清理整个工作区。把拒绝原因写入 `decisions.md`，避免下一轮重复同一路线。
