# 实验台账

## E01 基线与裁剪回归（2026-11-17）

负责人：Codex。基线为 HEAD 加 artifacts/baseline-diff.patch 与源码快照；候选为本任务工作区修改。
假设：删除打符、基地、前哨站专用路径不改变普通机器人的解算/跟踪/预测/瞄准数值行为。
数据：仓库内生成的确定性几何观测，无外部数据，无随机数；不训练、不调参。探索/回归用例，不是独立比赛测试集。
配置：configs/sentry.yaml、configs/demo.yaml（以实际运行命令为准）。
环境：本机软件环境待记录；README 目标平台 Ubuntu22.04 / i7-1260P / 海康 MV-CS016-10UC，禁止硬件操作。
指标/门槛：普通机器人状态和命令数值一致（容差 1e-10），不支持类别不得建轨；构建失败需区分基线缺陷/依赖缺失。无预热/重复性能测量，无延迟收益结论。
命令/结果/原始输出：待执行后追加。

### 命令与结果

环境为 Ubuntu 22.04 / Linux 6.8 / x86_64、GCC11.4、CMake3.22.1、OpenCV4.5.4、OpenVINO2024.6.0。实际 CPU/平台命令输出在 artifacts/cpu.txt、environment.txt；不将 README 的 i7 目标平台当作本机性能证据。输入文件 SHA-256 见 input-sha256.json；候选源码与受保护文件列表见 changed-files.txt 和 preserved-sha256.json。

基线源码解压到 /tmp/restar-trim-frozen（不含二进制资产，检测权重仍从未变化的仓库 assets 加载），先运行基线再修改源码。确定性合成数据，无随机种子，推理设备采用冻结 demo 配置的 CPU；每版一次回归，无性能计时、预热或性能重复实验。

```bash
# 基线默认命令，配置即失败（robot_interfaces overlay 未加载）
cmake -B build && cmake --build build -j"$(nproc)"
# 基线可用层级，全构建通过
cmake -B /tmp/restar-trim-baseline -DRESTAR_WITH_ROS2=OFF
cmake --build /tmp/restar-trim-baseline -j2
# 基线无硬件测试
cmake -S tests/rmul_trim -B /tmp/restar-trim-probe-before -DRESTAR_SOURCE_ROOT=/tmp/restar-trim-frozen -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/restar-trim-probe-before -j2
/tmp/restar-trim-probe-before/rmul_trim_regression --normal /tmp/restar-trim-frozen/configs/sentry.yaml research/projects/trim-2027-rmul/artifacts/baseline-normal.csv
/tmp/restar-trim-probe-before/rmul_trim_regression --excluded /tmp/restar-trim-frozen/configs/sentry.yaml
# 上条预期失败，证明旧实现仍接受裁剪目标
# 候选完整编译（仅编译，不运行硬件入口）
source /opt/ros/humble/setup.bash
source /home/c/tongxin/ros2_ws/install/local_setup.bash
cmake -B build
cmake --build build -j2
# Decider 空候选边界补丁后再完成一次增量全构建
cmake --build build -j2
# 候选无硬件回归
cmake -S tests/rmul_trim -B /tmp/restar-trim-probe-after -DCMAKE_BUILD_TYPE=Release
cmake --build /tmp/restar-trim-probe-after -j2
/tmp/restar-trim-probe-after/rmul_trim_regression --normal configs/sentry.yaml research/projects/trim-2027-rmul/artifacts/candidate-normal.csv
ctest --test-dir /tmp/restar-trim-probe-after --output-on-failure
# 相同录像与姿态，帧范围 [0,200)
/tmp/restar-trim-probe-before/rmul_trim_replay /tmp/restar-trim-frozen/configs/demo.yaml assets/demo/demo.avi assets/demo/demo.txt research/projects/trim-2027-rmul/artifacts/baseline-replay.csv 200
/tmp/restar-trim-probe-after/rmul_trim_replay configs/demo.yaml assets/demo/demo.avi assets/demo/demo.txt research/projects/trim-2027-rmul/artifacts/candidate-replay.csv 200
python3 tests/rmul_trim/check_structure.py
git diff --check
```

结果：候选 Release 三项测试通过，固定合成 518 行以及 demo 200 帧 CSV 与基线逐字节相同。预声明绝对容差 1e-10，实际差 0；两个二进制使用相同编译器/Release 条件。demo 168 帧有目标，32 帧无目标，运行中包含 large dt 和目标发散样本，不能将通过测试当作跟踪质量优秀。研究者要求本轮结束时清除临时测试夹具、冻结源码包和原始日志/CSV；验证摘要与结果数字保留在 validation-results.md，原始产物不再随工作区保存。

证据为 offline-reproduced：只支持限定用例下裁剪行为与基线一致，不支持 FPS/命中率改善或总体不退化。未评估延迟中位数/P95/P99/抖动/丢帧，未运行硬件或机器人 ROS2 节点。
