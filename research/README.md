# 视觉研究任务与项目

`tasks/` 保存研究者维护的正式输入，`projects/` 保存 Agent 可持续更新的研究状态，`runtime/` 保存本机运行日志。任务输入和研究输出分离，避免 Agent 在研究过程中悄悄改写验收目标。

Ubuntu 22.04 默认的 Python 3.10 需要先安装轻量兼容依赖：

```bash
python3 -m pip install -r requirements-agent.txt
```

快速开始：

```bash
./agent.sh add tracker-outlier "降低高速旋转时的跟踪发散"
```

填写 `research/tasks/tracker-outlier/task.md`，然后把同目录 `config.toml` 中的 `ready` 改为 `true`。先检查而不调用 Codex：

```bash
./agent.sh check
```

执行一个有边界回合：

```bash
./agent.sh once tracker-outlier
```

详细说明见 [`docs/vision-research-agent.md`](../docs/vision-research-agent.md)。
