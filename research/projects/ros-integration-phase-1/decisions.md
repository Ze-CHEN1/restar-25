# 接口决定

2026-11-15 Phase 2：采用新 `mc02_serial_bridge` 与组合帧v1；Mock ROS topic/file 是唯一传输输出，不创建USB/串口/CAN。Header视觉数据要求有效ROS发布时间；导航Twist只用steady接收TTL。每通道独立超时/无效清零；目标只诊断，不进首版Payload、不门控云台。帧序号只表示周期，不续期输入。协议CRC16按位算法与原Restar表实现4097前缀对照一致。电控可按固定偏移/大小端/拒绝规则解析，但固件接收范围、机械限位、角度模式、零位/符号/环绕、失联策略和watchdog需负责人确认；不把旧`io::Gimbal`的SP串口包或sentry的CBoard CAN当MC02事实。

2026-11-14收尾：导航算法归导航组，本任务仅对接ROS出口。当前核心Nav2包已可见，缺专用插件/驱动/bag；依赖盘点只说明通信出口尚未具备，不自动接管导航开发。三路到Probe不等于导航/自瞄互相消费，真实restar_msgs输入发布方仍未接入。
MC02执行前必须另行确认角度类型/符号/零位/环绕范围；当前仅冻结原io::Command rad及消息标志。Bridge默认不重复转换最终Twist坐标、不重复加Aimer偏置，不伪造速度/加速度。无Git提交，以源码快照+哈希保存复现基线。

2026-11-14 Phase1.5：按用户最新要求暂不提交Git、不建分支。保留两个仓库当前HEAD和本地独立ROS工作空间。
真实算法回放与原速度变换/Probe的独立进程验收采用单一父脚本管理，CLI图查询禁用daemon以适应沙箱；不因此声称跨机UDP通过。新脚本只记录/诊断，不改生产算法。
保留旧sentry硬件入口，独立replay作为无硬件入口；完整sentry、Nav2未联合验收。不把200/200/500ms age判据说成对外诊断必在该时间内到达，摘要周期和调度延迟单独记录。

2026-11-14：按用户决定建立项目自有 `restar_msgs`，替换 Restar 源码中旧 `sp_msgs` 依赖。
范围只采用静态调用已确认的列表字段和 timestamp；定义不是旧包 schema 的猜测性复刻，也不保证 wire 兼容。
上游发布方需迁移到新 ROS 类型。导航速度仍用标准 `geometry_msgs/Twist`，不放入 `restar_msgs`。
发布方审计未找到本地 `enemy_status` / `autoaim_target`，因此不声称当前北极熊导航发送这些输入。

消息严格采用任务书字段；不发送 shoot，不猜测角速度/加速度。
header.stamp 为发布时间；frame_id 暂空。接收超时使用 steady_clock。
原始目标有效性含局限：按 name 匹配不保证 type 已求解，真实调用点须保守检查已求解来源。
保持 gimbal_yaw 速度参考系和 fake_vel_transform；仅最小 launch/remap 适配。

最终将 ROS2 Publisher 直接集成 Publish2Nav，避免新增重复控制节点。
独立 Mock 明确 ros_only；完整 sentry 保留 legacy_hardware，不给它增加虚假的无设备模式。
发现并仅初始化导航首次里程计前未定义的角度为零，变换算法保持原样。
多个包装器需唯一 namespace，避免同名节点/rosout 冲突。

本轮独立文件入口作为第二种 ros_only 验证方式：原算法输出与固定 Mock 明确区分；
通过 RESTAR_BUILD_ROS2_REPLAY 显式构建，不静默绕过完整 sentry 的输入订阅依赖。
录像时间送原算法、ROS stamp 只代表发布；末尾无姿态时失败而不是沿用上一帧或插值补造。
导航最终速度仍需订阅/联调，规划/定位/决策如何完成由导航组负责，不新增这些算法工作。
