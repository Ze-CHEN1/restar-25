# 苏州工学院 Restar 战队视觉自瞄
# 基于同济大学 SuperPower 战队 RoboMaster 25 赛季视觉代码

本仓库是以同济大学 SuperPower 战队 RoboMaster 25 赛季为基底的视觉代码，包含相机与通信、装甲板和能量机关识别、位姿解算、目标跟踪、预测、轨迹规划、火控、标定和离线测试。

Restar将仓库同时内置了一套面向 RoboMaster 视觉开发的研究 Agent，用于将算法研究整理为可复现、可恢复、可验证的工程流程。

## 项目特点

* 无 ROS 依赖，方便新成员快速上手。
* 支持传统视觉与 YOLO/OpenVINO 推理。
* 包含装甲板检测、分类、PnP 解算、整车 EKF 跟踪、弹道解算与轨迹规划。
* 支持相机标定、手眼标定、录像回放与模块化测试。
* 支持步兵、哨兵、无人机、能量机关等应用入口。
* 内置视觉研究 Agent，保存假设、实验、失败原因与下一步，避免研究过程随对话结束而丢失。

## 环境要求

推荐环境：

* Ubuntu 22.04
* C++17
* Python 3.10 或更高版本
* OpenCV、Eigen、fmt、spdlog、yaml-cpp
* OpenVINO
* Ceres
* MindVision SDK 或 HikRobot MVS SDK

安装基础依赖：

```bash
sudo apt update
sudo apt install -y \
  git \
  g++ \
  cmake \
  libopencv-dev \
  libfmt-dev \
  libeigen3-dev \
  libspdlog-dev \
  libyaml-cpp-dev \
  libusb-1.0-0-dev \
  nlohmann-json3-dev \
  python3-pip
```

安装研究 Agent 的 Python 依赖：

```bash
python3 -m pip install -r requirements-agent.txt
```

Python 3.11 及以上版本自带 `tomllib`；Ubuntu 22.04 默认的 Python 3.10 会自动使用 `tomli` 兼容包。

## 编译与运行

```bash
cmake -B build
cmake --build build -j"$(nproc)"
```

运行离线自瞄测试：

```bash
./build/auto_aim_test
```

实际运行前，请根据机器人、相机和兵种选择或修改 `configs/` 中对应的 YAML 配置。

## 视觉研究 Agent

研究 Agent 不会自动控制真实机器人。它的目标是帮助视觉组完成以下研发闭环：

```text
问题定义 → 基线复现 → 假设设计 → 最小实验 → 受控代码改动 → 离线验证 → 台架/实车人工验收
```

| 路径                                                               | 作用                     |
| ---------------------------------------------------------------- | ---------------------- |
| [`AGENTS.md`](AGENTS.md)                                         | 全局工程规则、安全边界、证据等级与完成标准  |
| [`agents/instructions/`](agents/instructions/)                   | 视觉研究、代码实现、实验验收的专项流程    |
| [`research/tasks/`](research/tasks/)                             | 研究者填写的正式任务输入           |
| [`research/projects/`](research/projects/)                       | Agent 保存的进度、假设、实验与输入快照 |
| [`agent.sh`](agent.sh)                                           | 创建任务、检查环境、运行研究回合       |
| [`docs/vision-research-agent.md`](docs/vision-research-agent.md) | 完整使用手册                 |

## 开始第一个研究任务

先检查环境：

```bash
./agent.sh check
```

创建任务：

```bash
./agent.sh add tracker-outlier "降低高速小陀螺场景的跟踪发散"
```

然后填写：

```text
research/tasks/tracker-outlier/task.md
```

任务至少应写清：

* 当前代码、模型、配置与可复现基线；
* 使用的数据或录像；
* 指标定义、单位、目标与不得退化的指标；
* 允许修改的目录；
* 实时性、平台与硬件限制；
* 哪些步骤需要台架或实车人工确认。

确认任务后，将：

```toml
ready = false
```

改为：

```toml
ready = true
```

先预览，再运行一个有边界的研究回合：

```bash
./agent.sh check
./agent.sh once tracker-outlier
```

## Agent 权限模式

任务默认使用：

```toml
implementation_mode = "proposal"
```

此模式下 Agent 只能更新 `research/projects/` 中的研究台账，不会改动视觉源码。

只有在 `task.md` 中明确写明允许修改范围后，才可改为：

```toml
implementation_mode = "workspace-change"
```

此时 Agent 可以修改指定源码，但仍不会自动提交、推送、安装系统依赖或执行真实硬件动作。

## 研究证据等级

| 等级                      | 含义             |
| ----------------------- | -------------- |
| `hypothesis`            | 机制推测，尚未验证      |
| `code-reviewed`         | 代码和接口已审查       |
| `offline-reproduced`    | 固定输入下可复现       |
| `benchmark-supported`   | 相对冻结基线达到预设指标   |
| `hardware-validated`    | 研究者确认通过台架或实车验证 |
| `competition-validated` | 研究者确认在比赛条件下验证  |

Agent 最多只能得出离线基准结论。实车与比赛结论必须由成员根据真实测试记录确认。

## 目录结构

```text
.
├── assets/                 # 模型、演示视频和测试资源
├── calibration/            # 相机、手眼标定程序
├── configs/                # 各机器人和相机的 YAML 配置
├── io/                     # 相机、串口、CAN、ROS2 等硬件接口
├── src/                    # 不同兵种和调试程序入口
├── tasks/
│   ├── auto_aim/           # 自瞄检测、解算、跟踪、规划和火控
│   ├── auto_buff/          # 能量机关检测、解算和预测
│   └── omniperception/     # 全向感知
├── tests/                  # 模块、录像回放和硬件测试
├── tools/                  # 滤波、日志、图像、弹道等通用工具
├── research/               # Agent 任务、项目状态和运行配置
├── templates/              # Agent 任务与项目模板
├── AGENTS.md               # Codex 仓库级指令
└── agent.sh                # Agent 命令入口
```

## 安全说明

* 不要将 Token、私钥、Cookie、`.env`、比赛私有数据或设备凭据提交到仓库。
* 不要让 Agent 自动执行串口、CAN、云台、摩擦轮、拨弹或自动开火相关命令。
* 台架和实车测试前，应由现场成员确认急停、限位、供电、供弹、场地隔离和护目措施。
* 修改参数时必须明确单位、坐标系和时间戳语义，避免混用角度/弧度、毫秒/秒或不同坐标系。

## 许可证

本项目使用 [MIT License](LICENSE)。
