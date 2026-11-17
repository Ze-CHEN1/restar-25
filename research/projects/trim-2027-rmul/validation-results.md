# 验证结果（2026-11-17）

最高证据：offline-reproduced。没有性能改善结论或硬件结论。

| 检查 | 结果 | 原始产物（artifacts/，Git 忽略） |
|---|---|---|
| 原始默认构建 | 配置失败：未加载 robot_interfaces | baseline-build.log |
| 基线 ROS2 关闭完整构建 | 通过 | baseline-no-ros-configure.log / baseline-no-ros-build.log |
| 候选加载 ROS overlay 完整构建 | 通过（含所有现存哨兵入口） | candidate-configure.log / candidate-build.log / candidate-final-build.log |
| Release 回归 | 3/3 通过 | candidate-ctest.log |
| 固定观测对照 | 518 行逐字节一致，最大差 0 | baseline-normal.csv / candidate-normal.csv / normal-comparison.json |
| 固定示例回放 | 200 帧逐字节一致，168 帧有跟踪目标，最大差 0 | baseline-replay.csv / candidate-replay.csv / replay-comparison.json |
| 旧类别基线暴露 | 原基线失败：excluded target accepted；候选通过 | baseline-excluded.log / candidate-ctest.log |
| 静态引用与原文件哈希 | 112 文件未变，无删除功能活动引用 | structure-check.log / preserved-sha256.json |
| Git 差异检查 | 通过 | final-diff-check.log |

双装甲已知越界使本次对照只覆盖初始化/预测/规划，不覆盖该更新分支；录像不代表总体精度和实时性，未执行真实设备与 ROS2 机器人节点。研究者要求结束本轮后清理临时回归源码、构建日志和原始 CSV；以上结果是保留的摘要，清理后不能在本工作区直接重跑原命令。
