# ROS-INTEGRATION-PHASE-1

implementation_mode: workspace-change (用户明确授权：主要先写 ROS 代码)
状态：Phase 2 软件 Bridge/protocol candidate-complete；部署导航通信出口/MC02角度物理映射与固件watchdog待确认；仅负责ROS通信，不开发导航算法，禁止打开硬件设备。
基线：Restar ROI e67c4d28ced2387d9db215ea675f19cf4fec67bd；导航 main 5b3241f980d5cee2a51d86b030bb9a08bf0b123a。
验收：共享消息构建、共用 Restar 发布实现的 Mock、Probe 独立接收/超时/非法值/QoS/退出重启。
完整算法/Nav2 验收依赖缺失时必须单独记录。

ROS 软件实现、Mock 与独立录像算法输出已验证；更新套件20项pytest全通过，0 skipped；colcon test-result为23 tests、0 errors/failures/skipped（含3个包装记录）。
导航最终速度接口和双路通信在范围内；导航规划/执行任务由导航侧负责。
`restar_msgs` 已取代 Restar 源码中的旧 `sp_msgs` 依赖；消息不是旧包的 wire-compatible 复刻。
完整 sentry 与 ROS 测试目标已编译，未运行会创建硬件的 sentry。
独立算法回放不等于完整在线感知入口。
最高软件证据 offline-reproduced；完整包装器/sentry 调用点 code-reviewed。
报告：/home/c/tongxin/ROS_INTEGRATION_PHASE1.md。
最新Phase1.5报告：/home/c/tongxin/ROS_INTEGRATION_PHASE15.md。用户最新要求暂不提交Git；本轮无新分支/提交/推送。
Phase 2/3：MC02协议与CDC发送源现归档在`io/mc02_serial`，ROS Bridge、Mock和PTY测试在`io/ros2/mc02_bridge`；Restar可选ROS工具构建会生成Bridge目标。完整协议文档见`io/mc02_serial/PROTOCOL_V1.md`，Transport说明见同目录`SERIAL_TRANSPORT.md`。此前独立ROS工作区的日志留在`/home/c/tongxin/ros2_ws/test_results/phase2_restart/`及`phase3/`作为历史证据。
