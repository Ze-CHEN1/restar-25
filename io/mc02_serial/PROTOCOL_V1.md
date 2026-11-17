# MC02 上位机控制帧协议 v1

状态：用于上位机离线软件联调和电控接收解析适配；不代表固件已经实现、物理语义已确认或帧可直接驱动机构。Phase 2默认Mock通过ROS字节数组验证；Phase 3新增可选Linux串口发送，参数与PTY验收见 [SERIAL_TRANSPORT.md](SERIAL_TRANSPORT.md)。协议字节定义不变。

## 1. 边界与输入

ROS 输入沿用既有接口，不改消息、算法、fake_vel_transform 或坐标参考。

| 逻辑通道 | 类型/缺省相对话题 | Payload来源 | 起始超时 |
|---|---|---|---|
| 导航速度 | `geometry_msgs/msg/Twist` / `cmd_vel` | `linear.x/y` m/s、`angular.z` rad/s | 200 ms |
| 云台命令 | `robot_interfaces/msg/GimbalCommand` / `vision/gimbal_cmd` | 最终原始 yaw/pitch，rad；不改符号、偏置、参考系 | 200 ms |
| 视觉目标（可选，只诊断） | `robot_interfaces/msg/VisionTarget2D` / `vision/target` | 检查 valid、ID1..8、有限坐标、独立新鲜度；不进入 v1 Payload | 500 ms |

三通道各自保存最近接收样本和本机 `steady_clock` 接收时刻。组帧频率默认为50Hz。帧序号每周期递增，**不表示输入样本新到**。同一有效ROS样本可在超时前出现在多个帧中；MC02必须把它们理解为同一控制值的新传输周期，而非新增ROS观测。

`Twist` 无Header：只能以本机接收age判超时，不能检测源消息在网络中延迟很久、重复或乱序。视觉消息Header为ROS发布时间，不是相机曝光时间。视觉stamp必须规范（sec非负、nanosec<1e9）、非零、相对本机ROS时钟不超过未来容差50ms且年龄不大于本通道TTL。相同、倒退时间戳不刷新有效数据；倒退样本不会降低已接受的时间高水位。观察到ROS本地时钟回拨时清高水位并隔离首条消息，再接受新时基递增消息。时钟不同步仍是部署风险。

任一控制样本无效、数值非有限/超出float32可表达范围或超时，下一周期立即写入该通道零值并清valid，不能延用上次非零值。导航与云台独立判定。`control_enabled` 仅随新鲜、stamp可用的云台样本复制；`gimbal_valid` 还要求ROS valid为真且角度合法。禁用、invalid和timeout都清角度。零速度与零角可为有效命令，必须看valid标志。视觉目标无效不会关闭独立云台控制；第一版不发送视觉坐标，也不以target有效性门控控制。

## 2. 帧布局

固定长度34字节。多字节整数、CRC和IEEE-754 binary32均低字节先传（Little Endian）。没有C结构体padding；解析器逐字段读取。

| 偏移 | 长度 | 字段 | v1值/定义 |
|---:|---:|---|---|
| 0 | 1 | 帧头 | ASCII `S` = `0x53` |
| 1 | 1 | 帧头 | ASCII `P` = `0x50` |
| 2 | 1 | `version` | `0x01` |
| 3 | 1 | `message_id` | `0x01`，组合控制帧 |
| 4 | 2 | `sequence` | uint16 LE；每周期模65536递增 |
| 6 | 2 | `payload_length` | uint16 LE，v1固定24 (`0x0018`) |
| 8 | 4 | `vx` | IEEE-754 binary32，m/s |
| 12 | 4 | `vy` | IEEE-754 binary32，m/s |
| 16 | 4 | `wz` | IEEE-754 binary32，rad/s |
| 20 | 4 | `yaw` | IEEE-754 binary32，原ROS角度，rad |
| 24 | 4 | `pitch` | IEEE-754 binary32，原ROS角度，rad |
| 28 | 1 | `nav_valid` | 0/1；0要求vx/vy/wz全为正零 |
| 29 | 1 | `gimbal_valid` | 0/1；0要求yaw/pitch全为正零 |
| 30 | 1 | `control_enabled` | 0/1；gimbal_valid=1时必须为1 |
| 31 | 1 | `reserved` | 必须为0，接收端忽略扩展能力前拒绝非0 |
| 32 | 2 | `crc16` | 覆盖byte[0..31]，uint16 LE |

总长 `2 + 1 + 1 + 2 + 2 + 24 + 2 = 34`。线上五个float允许有限binary32值（绝对值不超过约3.402823466e38，允许0）；float64输入转换到float32存在正常舍入，小于binary32表示能力的值可能下溢为0，不做饱和截断。v1不定义硬件速度/角度上限；电控适配时仍须在固件中设置机构安全限幅，非法NaN/Inf和超出固件安全范围值必须拒绝。上位机只执行有限数值和binary32表示能力检查，不擅自夹紧或改写源控制值。

## 3. CRC16

沿用 Restar `tools/crc.cpp` 实际CRC16实现，而非凭SP字样选择CRC变种。参数为：反射多项式 `0x8408`（normal notation `0x1021`），初值`0xffff`，输入/输出反射算法，最终异或`0x0000`。覆盖范围包括帧头、版本、ID、序号、长度和24字节Payload，不包括CRC本身。线上CRC为低字节先传。Restar原`check_crc16`读取线上末尾为`data[len-1]<<8 | data[len-2]`，即wire `85 43` 表示数值`0x4385`。

独立校验向量：

| 输入hex | CRC数值 | wire |
|---|---:|---|
| 空串 | `0xffff` | `ff ff` |
| ASCII `123456789` (`31..39`) | `0x6f91` | `91 6f` |
| `53 50` (`SP`) | `0x5ba2` | `a2 5b` |
| `00 01 ... ff` | `0xcfc3` | `c3 cf` |
| v1完整示例byte[0..31] | `0x4385` | `85 43` |

C++自动化测试将v1逐位实现与原始Restar表驱动实现对4096字节固定LCG序列的全部4097个前缀逐一比较。黄金帧及CRC向量也保存在`docs/test_vectors.json`。

## 4. 字节级完整帧示例

输入：sequence=`0x1234`，vx=0.1，vy=-0.2，wz=0.3，yaw=0.25，pitch=-0.1；三标志均1，reserved=0。float采用binary32舍入。CRC只覆盖前32字节。

```text
偏移 00: 53 50 01 01 34 12 18 00
偏移 08: cd cc cc 3d       # vx = 0.1
偏移 0c: cd cc 4c be       # vy = -0.2
偏移 10: 9a 99 99 3e       # wz = 0.3
偏移 14: 00 00 80 3e       # yaw = 0.25
偏移 18: cd cc cc bd       # pitch = -0.1
偏移 1c: 01 01 01 00       # nav_valid, gimbal_valid, control_enabled, reserved
偏移 20: 85 43             # CRC16 wire little endian; value 0x4385
完整帧: 53 50 01 01 34 12 18 00 cd cc cc 3d cd cc 4c be 9a 99 99 3e 00 00 80 3e cd cc cc bd 01 01 01 00 85 43
```

## 5. 接收端解析与拒绝规则

1. 在字节流中寻找相邻 `53 50`；等待固定34字节，不按未验证的长度分配大缓冲。
2. 只接受 `version=1`、`message_id=1`、`payload_length=24`。版本或ID未知时拒绝；v1不存在可忽略的未知字段。
3. CRC错误、截断帧、非法长度、flag不在0/1、reserved非0、NaN/Inf、无效通道携带非零值、gimbal_valid=1但control_enabled=0，整帧拒绝。CRC失败后从候选帧头后一字节继续同步。
4. 检查sequence新鲜度和丢帧率。相邻序号按uint16回绕；duplicate、乱序、长时间无帧应记录并按MC02 watchdog策略处理。v1未定义MC02状态应答/握手。
5. 控制器自身还须对速度、机械软硬限位、急停、供电/通信故障设计独立保护；串口/USB故障时不能保持无限期旧命令。其时限与失效状态待固件/系统安全设计评审冻结。

## 6. ROS2软件运行和复现

Restar 的 ROS Bridge 从ROS输入生成固定字节并在 Mock 模式发布`std_msgs/msg/UInt8MultiArray`到`bridge/frame`。Mock接收进程独立解码、打印摘要；设置`output_file:=/absolute/path/capture.bin`可将有效34字节帧异步写为原始二进制。队列上限256帧，磁盘慢时丢弃记录并计数；不会阻塞ROS callback或控制组帧。进程退出会排空队列、flush并将写失败/丢帧置为非零退出码/报告。`mc02_frame_dump capture.bin`独立离线解码为CSV并检查每个字节都属于合法完整帧。

```bash
cd /home/c/tongxin/restar-25
source /opt/ros/humble/setup.bash
source pb2025_sentry_nav/install/setup.bash
source ros2_ws/install/setup.bash
export ROS_DOMAIN_ID=83 ROS_LOCALHOST_ONLY=1 PYTHONNOUSERSITE=1
export ROS_LOG_DIR=/tmp/mc02_phase2_ros_logs

cmake -S . -B build-mc02 -DRESTAR_ROS2_TOOLS_ONLY=ON -DBUILD_TESTING=ON
cmake --build build-mc02 -j2
ctest --test-dir build-mc02 --output-on-failure

# 纯软件/真实上游文件回放，导航仍使用合成输入经原 fake_vel_transform
/usr/bin/python3 io/ros2/mc02_bridge/runtime_acceptance.py \
  --restar-root /home/c/tongxin/restar-25 \
  --replay /tmp/restar-phase2-replay/restar_ros_replay \
  --output /home/c/tongxin/ros2_ws/test_results/phase2_restart/final_rerun

# 独立解析实际Mock接收端的capture.bin
build-mc02/io/ros2/mc02_frame_dump /tmp/mc02_capture.bin
```

所有运行只在domain83、localhost-only内，测试导航输入明确是Mock；不得将其称作Nav2数据。production调度是SingleThreadedExecutor + steady wall timer；ROS回调只复制小消息、校验并覆盖latest状态，没有同步文件/串口/CAN操作。默认QoS按现有契约：nav BestEffort/1、gimbal Reliable/1、target BestEffort/5；三者Volatile，可在启动参数选择Reliable/BestEffort。Mock模式发布二进制帧ROS话题；Phase 3串口模式与Mock互斥，见SERIAL_TRANSPORT.md。

### 启动参数

全部自定义参数只在启动时读取，运行中修改被拒绝；缺失使用下表默认值。

| 参数 | 类型/默认 | 单位、合法范围/含义 |
|---|---|---|
| nav_topic/gimbal_topic/target_topic | string，见第1节 | 合法ROS话题名，可相对或绝对，不同namespace允许分别映射 |
| frame_topic | string `bridge/frame` | Mock二进制输出话题 |
| frame_rate_hz | double 50.0 | Hz，[1,1000]，wall timer周期；非硬实时保证 |
| nav_timeout_ms/gimbal_timeout_ms/target_timeout_ms | double 200/200/500 | ms，[1,60000]，独立输入TTL |
| future_tolerance_ms | double 50.0 | ms，[0,1000]，仅视觉stamp相对本机ROS时钟的未来容差 |
| subscribe_target | bool true | false时不创建目标订阅；v1始终不发送坐标 |
| nav_reliability/gimbal_reliability/target_reliability | string best_effort/reliable/best_effort | 仅允许`reliable`或`best_effort`；KeepLast深度1/1/5、Volatile |
| use_sim_time | ROS标准bool false | 视觉发布者与Bridge必须使用相同时基；未收到/暂停clock会使视觉数据失效 |

Mock接收端独立参数：`frame_topic`默认`bridge/frame`；`output_file` string默认空（不写文件），非空必须为可写普通文件路径。Bridge不会打开这个文件。诊断`bridge/status`默认每秒发布一次逐通道原因码、接收计数、stamp接受/重复/倒退计数、publisher计数和age；帧安全失效在每个组帧周期独立判定，不等待诊断周期。无Header导航的accepted_stamps固定0，应查看samples。错误QoS由ROS incompatible QoS日志提示。

## 7. MC02物理语义待确认

该协议只搬运原ROS值，不赋予角度的电机意义。接收解析可以由电控组直接依帧布局实现；执行器映射在确认以下项目之前不得上线：

- yaw/pitch是绝对位置、相对角还是增量；参考坐标、机械/编码器零位与IMU/world零位关系；
- 轴正方向、连续旋转/多圈/回绕解释、可用角度范围及软硬限位；
- 当前Aimer与Decider各分支的控制结果应否共用同一角度模式；Aimer配置偏置由视觉算法给出，Bridge不得重复加入；
- 控制禁用、通道valid=0、超时、帧丢失/CRC错时的安全状态；需保持、制动或交由独立控制器策略的定义；
- 速度范围/死区/加速度限制、设备端float支持/端序、上线握手和状态回传/watchdog时限。

`restar-25/io/gimbal`内的`VisionToGimbal` SP串口结构属于Restar另一程序路径；当前 sentry 实际 CBoard走SocketCAN。两者均不是MC02既有硬件协议的证据，本协议v1不声称兼容它们。
