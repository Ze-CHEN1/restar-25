# 候选方案

Phase 2组合控制帧：A（采用）固定SP前缀、显式version/message_id/sequence/length、LE IEEE binary32与经Restar源码对照的CRC16；通道独立TTL、无效值零填并清标志；ROS-only Mock端解码。B（拒绝）直接memcpy旧packed SP结构或复用旧CBoard CAN字段，不能明确v1解析契约且混淆不同硬件路径。offline-reproduced支持帧字节兼容和模拟通路；不证明MC02物理角度映射或固件安全策略。

Phase2准备：A（采用）冻结已有ROS契约、保存HEAD+补丁+新增文件快照、把部署导航出口/MC02映射列为待确认，后续订阅层独立推进；B（拒绝）为通信任务重建/替换导航算法、猜测执行角度或重写已验收消息。符合用户只负责信息收发的边界。

Phase1.5：A（采用）在真实独立进程运行原文件算法回放、原速度变换和Probe，上游速度Mock明确标注；B（不采用）因Nav2依赖缺失而停止所有软件层运行验证。
A已三轮offline-reproduced，证明部分真实组件通信，不能提升为完整Nav2/sentry联合验收。剩余假设需导航完整运行环境与离线传感器数据验证。

输入接口迁移：A（采用）新建 restar_msgs，仅承载 Restar 已使用的敌人状态/目标编号输入，导航 Twist 保持标准类型；
B（拒绝）将导航速度或云台/目标输出塞进同一个新包，造成语义重复并破坏现有 robot_interfaces/geometry_msgs 契约。
发布方需统一迁移；不生成名为 sp_msgs 的兼容假包。

A（采用）：保留独立 CMake 工程；同一 ROS 发布源码可单独构建 Mock，完整工程订阅项目自有 restar_msgs。
B（不采用）：将整个视觉工程改为 ament 包；引入无关构建迁移，超出本阶段。
硬件隔离采用独立 Mock，不通过只跳过 CBoard::send() 宣称无硬件。

本轮自瞄通信：A（采用）独立录像入口运行原算法并复用 Publisher；B（拒绝）伪造 sp_msgs 或静默关闭 sentry 的旧订阅。
新增入口只验证主相机离线检测/跟踪/瞄准输出，不代替完整 sentry。
