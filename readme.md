
# Restar 2025 自瞄系统

**Restar Auto Aim | RoboMaster Vision System**

基于 C++17 的 RoboMaster 机器人视觉自瞄系统，涵盖目标检测、位姿解算、运动状态估计、轨迹规划和上下位机通信。项目面向 Restar 哨兵机器人平台，持续开展算法适配、性能优化与工程重构。

![Ubuntu](https://img.shields.io/badge/Ubuntu-22.04-E95420?logo=ubuntu&logoColor=white)
![C++](https://img.shields.io/badge/C++-17-00599C?logo=cplusplus&logoColor=white)
![OpenCV](https://img.shields.io/badge/OpenCV-4.5.4-5C3EE8?logo=opencv&logoColor=white)
![OpenVINO](https://img.shields.io/badge/OpenVINO-2024.6-0071C5)
![License](https://img.shields.io/badge/License-MIT-green)

> **版本说明**
>
> `main` 为 Restar 自瞄系统的 V1 基础可运行版本，保留完整的自瞄功能与独立部署能力。
>
> 后续算法优化、工程重构与比赛专项版本采用独立分支进行开发。`main` 不代表所有最新功能均已集成。
>
> [查看开发分支](https://github.com/Ze-CHEN1/restar-25/branches)

## ✨ 项目亮点

- **完整视觉自瞄链路**：集成 YOLO 装甲板检测、PnP 位姿解算、EKF 目标跟踪、弹道补偿与 MPC 轨迹规划，形成从图像输入到云台控制指令输出的完整流程。
- **面向比赛场景的工程重构**：针对高校联盟赛 3V3 场景，裁剪非必要功能、解耦可选模块、整理 CMake 依赖，降低工程复杂度和维护成本。
- **多线程与状态处理优化**：修复双装甲目标候选遍历边界问题，使用线程安全队列传递规划调试数据，完善目标丢失状态下的处理逻辑。
- **硬件通信适配**：针对实际机器人平台，完成云台响应测试由 CAN 向 USB 串口通信的迁移，验证姿态接收与控制指令发送。
- **模块化开发与测试**：按照感知、估计、规划、通信和工具模块组织代码，提供独立测试程序，并在后续版本中引入自动化源码检查与 C++ 回归测试。

## 📖 核心功能

| 模块 | 技术方案 | 功能说明 |
| --- | --- | --- |
| 图像采集 | HikRobot SDK / OpenCV | 工业相机接入、图像获取与参数配置 |
| 目标检测 | YOLOv5 / YOLOv8 / YOLO11 | 装甲板检测、关键点提取及目标筛选 |
| 位姿解算 | PnP / Eigen | 基于装甲板几何信息估计目标三维位姿 |
| 目标跟踪 | EKF | 估计目标位置、速度、旋转角及角速度 |
| 运动预测 | 目标运动模型 | 预测目标未来位置与装甲板运动状态 |
| 轨迹规划 | MPC / TinyMPC | 根据目标轨迹和云台约束计算瞄准轨迹 |
| 弹道补偿 | 弹道模型 | 结合弹速与目标距离进行弹道解算 |
| 通信控制 | Serial / SocketCAN | 接收机器人姿态信息并发送控制指令 |
| 工程工具 | CMake / YAML / spdlog | 编译管理、参数配置、日志与独立测试 |

项目保留传统视觉识别与神经网络识别两类方案，支持根据模型与运行环境选择对应处理流程。

基础 EKF、MPC、检测器等算法实现包含对原开源框架的继承与复用；Restar 的主要改进集中在工程适配、模块重构、边界修复及比赛专项优化。

## 🏗️ 系统架构

系统采用模块化设计，将图像采集、视觉感知、状态估计、轨迹规划和硬件通信解耦。

```text
             Industrial Camera
                     |
                     v
              Image Acquisition
                     |
                     v
              Armor Detection
                YOLO / OpenCV
                     |
                     v
             Pose Estimation
                 PnP
                     |
                     v
              Target Tracking
                  EKF
                     |
                     v
             Motion Prediction
                     |
                     v
             Trajectory Planning
               MPC / Ballistics
                     |
                     v
               Communication
               Serial / CAN
                     |
                     v
              Gimbal Controller
```

上位机负责视觉计算、目标状态估计和瞄准规划，下位机负责执行对应的云台与发射机构控制。

基础自瞄系统不强制依赖 ROS2，可独立部署与调试。

## 🛠️ 核心工程改进

### 1. 联盟赛专项轻量化重构

针对 2027 赛季 RoboMaster 高校联盟赛 3V3 场景，开展独立的单相机自瞄工程裁剪。

主要工作：

- 移除打符、基地、前哨站专项处理及副相机全向感知等非目标场景模块。
- 精简旧运行入口、相机后端及相关测试程序。
- 保留普通机器人自瞄所需的检测、PnP、EKF、弹道解算与 MPC 规划链路。
- 将 CAN 诊断、达妙 IMU 及 ROS2 适配拆分为可选构建模块。
- 优化 CMake 依赖管理，取消 OpenVINO 固定安装路径，并整理第三方库的链接关系。

**设计目标**：减少非必要模块耦合，使视觉系统更贴合实际比赛任务，并提高后续部署和维护的可控性。

### 2. 跟踪边界与多线程数据安全

在专项版本的代码自审过程中，对目标状态处理与线程间数据交互进行了调整。

**双装甲目标边界处理**

针对双装甲目标实际候选数量与固定遍历数量不一致的问题，调整候选遍历上限，避免发生越界读取，同时保留四装甲目标原有的候选处理逻辑。

**规划调试数据线程安全**

原有调试流程中，显示线程直接读取规划线程更新的调试数据，存在并发访问风险。

调整后：

- 规划线程生成有效调试点。
- 通过线程安全队列传递数据快照。
- 显示线程读取快照，而非直接访问共享可变数据。
- 无有效规划结果时跳过对应调试点显示。

**目标丢失状态处理**

针对目标进入 `lost` 状态后的执行路径，减少不必要的后续状态检查，使处理逻辑更加明确。

上述修改侧重软件健壮性与可维护性，不改变原有 MPC 数学求解实现。

### 3. 通信接口迁移与设备适配

针对实际机器人通过 USB 虚拟串口连接下位机的情况，调整原有云台响应测试程序。

主要工作：

- 将测试程序的通信接口由 `io::CBoard` 调整为 `io::Gimbal`。
- 将 CAN 数据收发方式适配为已有串口通信协议。
- 对接云台姿态四元数读取与控制指令发送接口。
- 完成串口连接、姿态接收及基础控制指令发送测试。

相关技术涉及 Linux 串口设备、二进制通信协议、CRC16 校验与机器人控制数据交互。

详细记录：

[gimbal_response_test 串口通信迁移](gimbal_response_test_migration.md)

### 4. 自动化检查与回归验证

针对联盟赛裁剪版，建立代码完整性与功能回归检查流程。

主要覆盖：

- 删除模块的残留引用检查。
- 配置文件与模型资源路径检查。
- 保留源码的 SHA-256 一致性校验。
- 空帧、目标过滤、跟踪丢失与重新获取测试。
- 双装甲更新及旋转预测相关测试。
- Release 模式下的有效测试条件检查。

该专项版本的自审记录包含 131 个未修改文件的一致性校验。

目前相关验证主要覆盖源码与 C++ 测试层面，完整实车性能仍需结合实际硬件测试。

## 🌿 版本与开发分支

| 版本 / 方向 | 主要内容 | 状态 |
| --- | --- | --- |
| `main` · V1 | 基础自瞄系统、完整算法模块及部署工具 | 基础可运行 |
| 联盟赛专项版本 | 单相机工程裁剪、依赖解耦、状态与线程安全调整 | 代码验证与迭代 |
| 动态 ROI | 根据目标状态动态调整图像处理区域 | 持续优化 |
| 调试 Agent | 辅助日志分析、异常定位与参数调试 | 开发中 |
| ROS2 导航集成 | 导航部署及视觉系统协同 | 开发中 |
| 一体化通信 | 统一视觉、导航与下位机数据交互 | 开发中 |

后续成熟功能将通过对应分支维护，避免将实验性代码直接合入 V1 基础版本。

实际分支名称与最新代码请查看：

[GitHub Branches](https://github.com/Ze-CHEN1/restar-25/branches)

## 💻 开发与运行环境

当前主要部署于 Restar 哨兵机器人上位机。

| 项目 | 配置 |
| --- | --- |
| CPU | Intel Core i7-13620H |
| GPU | Intel UHD Graphics |
| 内存 | 16 GB |
| 操作系统 | Ubuntu 22.04.5 LTS |
| 语言 | C++17 |
| 编译工具 | GCC 11.4 / CMake 3.22 |
| OpenCV | 4.5.4 |
| OpenVINO | 2024.6.0 |
| 主要相机 | HikRobot 工业相机 |

## 🚀 快速开始

以下步骤适用于 `main` 分支。其他专项版本的构建依赖和程序入口可能有所不同。

### 1. 克隆仓库

```bash
git clone -b main https://github.com/Ze-CHEN1/restar-25.git
cd restar-25
```

### 2. 安装基础依赖

```bash
sudo apt update

sudo apt install -y \
    git \
    g++ \
    cmake \
    make \
    can-utils \
    libopencv-dev \
    libfmt-dev \
    libeigen3-dev \
    libspdlog-dev \
    libyaml-cpp-dev \
    libusb-1.0-0-dev \
    nlohmann-json3-dev \
    openssh-server \
    screen
```

### 3. 安装第三方 SDK

根据实际使用的相机和功能安装以下依赖：

| 依赖 | 用途 | 下载 / 文档 |
| --- | --- | --- |
| HikRobot MVS | 海康工业相机驱动 | [官方 SDK](https://www.hikrobotics.com/) |
| MindVision SDK | 迈德威视相机支持 | [官方 SDK](https://mindvision.com.cn/category/software/sdk-installation-package/) |
| OpenVINO | 神经网络推理 | [安装文档](https://docs.openvino.ai/2024/get-started/install-openvino/install-openvino-archive-linux.html) |
| Ceres Solver | 原版部分求解与打符模块 | [安装文档](http://ceres-solver.org/installation.html) |

`main` 的 OpenVINO CMake 路径默认配置为：

```cmake
set(OpenVINO_DIR "/opt/intel/openvino_2024.6.0/runtime/cmake/")
```

如安装路径不同，请修改 `CMakeLists.txt` 中对应配置。

### 4. 编译项目

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

cmake --build build -j"$(nproc)"
```

### 5. 运行程序

基础自瞄测试：

```bash
./build/auto_aim_test
```

自瞄调试程序：

```bash
./build/auto_aim_debug_mpc
```

具体运行前需检查模型文件、相机标定数据和硬件通信配置。部分测试程序需要实际相机或下位机设备。

## ⚙️ 运行配置

主要参数位于：

```text
configs/sentry.yaml
```

包括：

- 神经网络模型及 OpenVINO 推理设备。
- 相机曝光、增益和 ROI。
- 装甲板检测与目标跟踪参数。
- 云台 yaw/pitch 补偿。
- MPC 规划参数。
- 串口与 CAN 通信配置。

### 串口配置

当前哨兵配置包含串口通信参数，例如：

```yaml
comm_backend: "serial"
com_port: "/dev/ttyACM0"
baudrate: 921600
```

配置串口访问权限：

```bash
sudo usermod -aG dialout "$USER"
```

重新登录后检查设备：

```bash
ls -l /dev/ttyACM*
```

如设备名不稳定，建议通过 udev 规则创建固定符号链接。

### CAN 配置（可选）

对于使用 SocketCAN 的旧设备和部分诊断工具，可按实际接口配置：

```bash
sudo ip link set can0 up type can bitrate 1000000
```

具体接口与波特率应匹配实际硬件。

### GPU 推理（可选）

使用 Intel 核显进行 OpenVINO 推理时，需要安装匹配的 GPU 驱动和运行时。

参考：

[Intel Compute Runtime](https://github.com/intel/compute-runtime)

设备检查：

```bash
sudo apt install clinfo
clinfo
```

实际推理设备应通过 OpenVINO 查询结果和运行日志确认，仅在 YAML 中设置 `GPU` 不代表推理已经成功运行在 GPU 上。

### 自启动

项目保留 `autostart.sh`，但当前脚本仍含旧工程路径及 `watchdog.sh` 依赖。

部署时需将工作目录调整为实际项目路径，并确认被调用的程序与脚本存在。

完成检查后，可根据 Ubuntu 桌面自启动或 `systemd` 机制配置开机运行。

## 📁 项目结构

```text
restar-25/
├── assets/             # 模型与相关资源
├── calibration/        # 相机及手眼标定
├── configs/            # 机器人与算法参数
├── io/                 # 相机及通信接口
├── src/                # 程序入口
├── tasks/
│   ├── auto_aim/       # 装甲板自瞄
│   ├── auto_buff/      # 能量机关（V1）
│   └── omniperception/ # 全向感知（V1）
├── tests/              # 独立测试程序
├── tools/              # 数学、日志及通用工具
├── CMakeLists.txt
├── autostart.sh
└── readme.md
```

后续联盟赛专项版本根据实际需要精简部分模块和入口，目录结构以对应版本为准。

## 🔭 后续方向

围绕 Restar 比赛需求，项目将继续推进：

- **视觉效率**：动态 ROI、推理流程与多线程性能分析。
- **系统可靠性**：目标超时处理、通信异常恢复和实车测试。
- **辅助开发**：引入调试 Agent，优化日志分析与问题排查效率。
- **系统集成**：推进 ROS2 导航与视觉自瞄的协同部署。
- **工程验证**：逐步完善 FPS、推理延迟、资源占用及完整链路性能测试。

## 🙏 致谢

本项目以同济大学 **SuperPower 战队** 开源视觉工程为初始技术基础，结合 Restar 自身硬件平台与比赛场景开展后续开发。

感谢 SuperPower 战队在装甲板识别、目标状态估计、轨迹规划及工程架构等方面的开源贡献。

参考项目：

- [SuperPower / sp_vision_25](https://github.com/TongjiSuperPower/sp_vision_25)
- [SuperPower / sp_vision_24](https://github.com/TongjiSuperPower/sp_vision_24)
- [SuperPower / sp_vision_23](https://github.com/TongjiSuperPower/sp_vision_23)

原项目的算法创新、理论研究及比赛成绩归属于相应作者。Restar 的实际工程改动以本仓库代码、开发记录和后续版本为准。

同时感谢 RoboMaster 开源社区的各位开发者。

## 📚 参考文献

保留原始开源项目所列参考文献：

[1] Alan Day.【RM2024赛季-识别模型】深圳大学-RobotPilots[EB/OL]. RoboMaster论坛. https://bbs.robomaster.com/article/54091, 2025.

[2] gaoxin.【RM2024-识别训练网络及推理代码开源】北京科技大学Reborn[EB/OL]. RoboMaster论坛. https://bbs.robomaster.com/article/9655, 2024.

[3] 方俊杰. Linear Modelled Top Detector[EB/OL]. GitHub. https://github.com/julyfun/rm.cv.fans, 2023.

[4] 陈君. rm_vision[EB/OL]. GitHub. https://github.com/chenjunnn/rm_vision, 2023.

[5] 桂凯. 系统辨识心得分享[EB/OL]. 知乎. https://www.zhihu.com/question/57405191/answer/153098166, 2017.

[6] Lynch K M, Park F C. Modern Robotics: Mechanics, Planning, and Control[M]. Cambridge: Cambridge University Press, 2017.

[7] Nguyen K, Schoedel S, Alavilli A, et al. TinyMPC: Model-predictive control on resource-constrained microcontrollers[C]//2024 IEEE International Conference on Robotics and Automation (ICRA). IEEE, 2024: 1-7.

[8] 王洪玺, 计泽贤, 张兰勇. 基于卡尔曼滤波的目标识别跟踪与射击系统设计[J]. Journal of Ordnance Equipment Engineering, 2022, 43(11).

## 📄 License

本项目遵循 [MIT License](LICENSE)。

保留原始项目的版权声明，并遵守相关第三方组件的开源协议。

---

**Restar Vision — From Open Source to Real-World Engineering.**
