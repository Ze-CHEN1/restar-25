# 2027 RMUL 哨兵保守裁剪

implementation_mode: workspace-change
状态：candidate-complete（仅本轮保守裁剪的离线门槛，等待研究者审查；不代表硬件或 2027 完整适配）
最高证据等级：offline-reproduced

## 范围和结果

用户范围：2027 赛季，当前哨兵、未来可能兼容步兵、单枪管；打符、基地、前哨站明确不需要。通信、主入口选择、ROS2 架构、检测方案本轮不改选。

删除 20 个独占源码/文本文件（106518 bytes）：`tasks/auto_buff/`、两个打符入口、打符测试、正弦拟合工具与打符绘图布局，清单见 [deleted-files.json](deleted-files.json)。从三个混合入口只移除打符构造/分支；CMake 不再查找 Ceres 或链接 auto_buff。未更换或删除二进制权重/SDK/示例录像。

删除基地/前哨站的三装甲初始化、专用过程噪声、定速限制、收敛/丢失门槛、重投影误差函数和瞄准特判。两个 Tracker 入口和 Decider 过滤这些旧模型类别及非装甲。模型类别/通信编号保留，普通机器人的两/四装甲数学保留；`reproject_armor` 的签名保留，前哨站专用 `oupost_reprojection_error` 已随功能移除。外部调用方若直接使用该已删除功能需要迁移。

Decider 排序前移除过滤后为空的相机候选，避免 `front()` 访问空列表。非自瞄通信模式枚举仍保留兼容值，不再执行已删除的打符算法。

保留原有哨兵入口、ROS2 未提交改动、相机后端、CAN/串口与协议字段、检测模型/解码/传统修正、动态 ROI、MPC、全向感知的其余职责、标定/诊断/回放。112 个受保护文件的哈希见 [preserved-sha256.json](preserved-sha256.json)，检查命令 `python3 tests/rmul_trim/check_structure.py`。

## 基线与验证

基线：`e67c4d28ced2387d9db215ea675f19cf4fec67bd` 加本轮前的工作区修改；源码快照/差异/原始输出位于被 Git 忽略的 `artifacts/`，未提交或推送 Git。不能把 HEAD 单独当作实验基线。

- 基线默认构建配置因未加载 robot_interfaces 而失败；关闭 ROS2 后完整构建通过。
- 候选加载现有 ROS overlay 后完整构建通过，包括 sentry/sentry_bp/sentry_debug/sentry_multithread；未运行硬件程序。
- 3 项 Release 离线测试全部通过：普通目标、排除/混合目标、Decider 空候选过滤。
- 固定观测回归 518 行 CSV 与冻结基线逐字节一致，状态/预测/Aimer/MPC 输出最大差为 0（预声明绝对容差 1e-10）。
- 仓库 demo 前 200 帧回放，168 帧有跟踪目标；基线与候选 CSV 逐字节一致，最大差为 0。包含记录内时间间隔过长和跟踪发散样本，两版行为一致。这些计数是回归覆盖，不是召回率或命中率指标。

精确命令、平台和输入哈希见 [experiment-ledger.md](experiment-ledger.md)、[input-sha256.json](input-sha256.json)、[validation-results.md](validation-results.md)。本轮临时回归夹具和原始日志已按研究者要求清理；保留的是验证摘要，不再提供本地夹具重跑入口。

## 验证缺口与下一步

本轮假设：删除明确不支持的专用路径，不改变普通机器人算法行为。证据只支持本轮固定用例；未测 P50/P95/P99 延迟、抖动、丢帧或命中率，不声称性能提升。单段 demo 不是总体性能评测。没有适配 2027 新装甲/类别白名单。

原有双装甲 `Target::update` 固定访问第三候选的缺陷仍在，本轮只验证双装甲初始化、预测和规划；原 Aimer 低速判断读取半径索引 8 的行为保留。相机/姿态阻塞后规划旧目标、调试点跨线程访问、GPU、真实时间同步/通信/供弹与实车效果均未验证。

下一项最有价值动作：独立修复双装甲更新越界并用最小双装甲序列回归；以后选定主入口/通信后，再决定哪些感知和诊断工具改为可选构建，以及开展停控/超时硬件验收。
