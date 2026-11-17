# 实验记录

## ROS-P2-002：重读后重新审计与完成（2026-11-15）

沿用P2-001范围/基线/实验门槛，本轮先保存草稿至 /tmp/mc02_bridge_before_restart，基线/受保护源码SHA256见 ros2_ws/test_results/phase2_restart/baseline.json。此前草稿不能视为已验收。候选A：独立控制通道、严格时间戳与原CRC对照、Mock异步文件记录；候选B：统一有效性门控并用发送序号刷新输入TTL，拒绝。视觉stamp零/格式异常/过旧/过未来/重复/倒退均撤销最新输入；本地ROS时钟回拨隔离首条，后续递增新时间恢复。导航无Header，仅接收TTL。控制输出默认50Hz，源10/20Hz；测试独立停发时另一路持续发送。结果/失败原样保留 phase2_restart；无设备、无系统安装、无Git提交。


## ROS-P2-001：统一组合控制协议与无硬件Bridge（2026-11-15）

授权：用户要求上位机自行定义新协议，无需兼容现固件；workspace-change；不改算法/ROS消息、不改变HEAD/分支、不提交。基线Restar ROI e67c4d28ced2387d9db215ea675f19cf4fec67bd、导航main 5b3241f980d5cee2a51d86b030bb9a08bf0b123a；候选为新增mc02_serial_bridge包。
假设：三个独立ROS缓存和steady_clock新鲜度检查，可在50Hz组帧时保持ROS接收计数不变，且任一控制通道失效后输出零数值/清零标志；独立Mock接收器按字节还原。
候选A（采用）：SP前缀+显式LE header/payload+原CRC16按位等价实现，固定v1组合帧；候选B（拒绝）：直接memcpy旧packed结构或照搬当前CBoard CAN帧，无法明确版本/长度/端序且混淆旧接口。
实验：先用原tools/crc.cpp和独立实现对照空串/123456789/SP字节及确定性4096字节所有前缀，再测试golden帧、每字节损坏、截断/粘包/非法长度/未知版本ID/CRC和非法数值；纯缓存注入时钟覆盖停发/冻结stamp/倒退/重启/不同更新率。
构建colcon新包；独立进程ROS发布→Bridge→原始字节订阅+Mock文件解码，覆盖初始失效、两路不同频率、分别停发/重启、无效目标/命令、NaN/Inf/float32溢出、QoS、SIGINT。另用原速度变换+Restar真实文件算法回放+Probe+Bridge验证实际源到二进制帧，不冒充完整Nav2。domain83/localhost1；Humble/C++17/x86_64；无硬件/随机数/预热；频率仅软件观察不做算法比较。
门槛：所有协议/缓存/进程测试通过，raw帧由独立Python解码通过；有效值float32舍入内一致；失效帧数值全零；重复组帧不延长ROS输入TTL；原CRC对照一致。停止条件：需打开设备、修改算法/消息或安装系统包。原始结果ros2_ws/test_results/phase2_*，结果另补；不声明物理角度/执行闭环已确认。

### ROS-P2-001 实际结果

- Humble/C++17/x86_64；构建`mc02_serial_bridge`及其上游ROS interface，关闭硬件访问；原始Restar `tools/crc.cpp`被测试目标直接链接。
- 原CRC表算法与新bitwise实现对空串、ASCII `123456789`、`SP`、`00..ff`向量吻合；4096字节确定性LCG序列的4097个前缀逐个一致。完整黄金帧CRC=`0x4385`，wire=`85 43`。
- C++协议/状态测试通过：完整黄金帧、所有长度/全部272个位翻转、CRC正确但版本/ID/长度/flag错误、非有限值、分包所有切点、噪声/粘包/CRC恢复、空/截断/空指针、sequence回绕；状态用注入steady时刻覆盖独立TTL、同值新ROS样本、无组帧续期、invalid/control disabled/NaN/Inf/float溢出、零/格式错/过旧/过未来/重复/倒退时间戳、时钟回拨和target诊断有效性。
- 隔离`ROS_DOMAIN_ID=83 ROS_LOCALHOST_ONLY=1`直接pytest：7 passed/0 failed；同环境`colcon test --packages-select mc02_serial_bridge`及CTest：3/3 passed（协议单测、CRC对照、包含7个跨进程运行用例的pytest）。覆盖50Hz周期/可配置25Hz、nav 10Hz与视觉20Hz独立更新/停发恢复、无效与溢出、错误QoS、可选target、接收端坏CRC拒绝、source stamp冻结/倒退、二进制捕获和独立Python/C++离线解码。
- 真实上游软件组合验收脚本现位于`io/ros2/mc02_bridge/runtime_acceptance.py`：Restar实际YOLO/Solver/Tracker/Aimer文件回放与合成导航输入经原`fake_vel_transform`，再由Bridge编码，Probe/Mock接收。71帧同时有nav与gimbal有效；消息发送计数Restar53、导航46、target53。graph证明final `cmd_vel`唯一发布者是`fake_vel_transform`；分别停止/恢复源后另一通道仍有效，失效字段为0。Mock捕获10846字节=319帧；独立Python逐帧CRC/值解析及`mc02_frame_dump`成功，0丢记录/0 I/O错误。
- 原始文件：`ros2_ws/test_results/phase2_restart/{colcon_build.log,colcon_test_final.log,ctest_final.log,runtime_final.log,real_sources_rerun/}`；`baseline.json`记录HEAD及保护文件SHA256，结果为无受保护算法/接口改动、分支HEAD未动。首次错误schema测试、过早读诊断快照和未给colcon设置隔离ROS环境的失败日志保留用于审计；修正后的对应运行通过。
- 约束：FastDDS在沙箱打印UDP socket权限错误；本机隔离进程的DDS测试仍全部通过，不推断跨机。上游速度不是Nav2而是synthetic mock；没有连接CDC/串口/CAN/执行器。最高证据offline-reproduced，只支持软件帧格式和模拟接收；MC02角度物理含义、执行限幅、故障输出/watchdog未确认。

## ROS-P2-PREP-001：通信契约与复现快照收尾（2026-11-14）

范围：workspace-change，仅文档/版本快照和只读环境盘点；导航算法、消息schema、Publisher、MC02协议不变。基线仍为Restar ROI e67c4d28ced2387d9db215ea675f19cf4fec67bd，导航main 5b3241f980d5cee2a51d86b030bb9a08bf0b123a。
候选A：既有代码+补丁/新增文件/逐文件哈希冻结复现版本，独立确认MC02映射缺口；候选B：为补运行环境接管导航算法或推测映射，拒绝。用户暂不提交Git。
本轮只读检查：source /opt/ros/humble、导航install和ros2_ws/install，使用ament index盘点launch/配置依赖和ros2 pkg executables；未运行硬件launch。核心Nav2包/可执行程序可见；PolarBear专用依赖/传感器驱动不齐，工作区无导航bag。当前盘点覆盖旧报告的依赖结论，不推断环境变化原因。
计划验证：两个仓库git diff --check；对归档中的基线文件应用binary diff，复制新增/共享包后逐字节/权限/哈希对比，检查报告本地链接；不重复跑无改动的算法/通信回归，不做性能结论。无随机数、无预热、无数据/硬件操作。停止条件为需要硬件、外部凭据或算法修改。
原始环境、源码清单、快照、还原验证结果保存ros2_ws/test_results/phase15_closeout_*与phase15_phase2_*。既有运行证据仍引用Phase1.5三轮跨进程和20项回归，完整导航通信出口与MC02映射等待对应负责人提供。

## ROS-P1.5-001（2026-11-14）：多进程 ROS2 运行时验收

假设：现有 Robot messages、Restar 离线算法 replay、Nav fake_vel_transform 与独立 Probe 可在隔离 ROS domain 中并行交换三路数据，节点停止后 Probe 超时，重启后恢复。
基线：Restar ROI e67c4d28ced2387d9db215ea675f19cf4fec67bd；导航 main 5b3241f980d5cee2a51d86b030bb9a08bf0b123a；候选为当前工作区差异及本地 ros2_ws。
数据：Restar 原 demo.avi + demo.txt，从frame1开始、单次上限600帧，测试按生命周期流程主动中断，非每轮跑满600；导航上游为独立 Mock cmd_vel_nav2_result 和 odometry，经仓库原 fake_vel_transform；不将 Mock 标记为真实 Nav2。哈希沿用根Phase1报告。
配置：Humble、C++17、rmw_fastrtps_cpp；ROS_DOMAIN_ID=83、ROS_LOCALHOST_ONLY=1；最终隔离 namespace phase15_acceptance；回放最大发布节奏20Hz，导航10Hz；Probe timeout 200/200/500ms，诊断周期50ms。固定输入、无随机种子、无预热，不做算法性能比较。
候选A（采用）：用真实算法文件回放与既有导航速度变换、独立Probe进行跨进程验证；候选B：因当前无 nav2_controller / bag 而停止全部验证，不获本地运行证据。
阈值：ROS graph 中被测进程/话题和类型可见；final cmd_vel 有单一 fake_vel_transform 发布者；三路 Probe ok 且两路视觉回放非Mock；目标无效不被标有效，独立进程退出/重启后恢复；记录topic hz实际到达频率。原卡“停发超时<各配置值”描述有误：配置值是age判据，外部诊断要加摘要周期/调度，不能用该不等式声称通过。
另分别执行 Restar 完整与非ROS构建、Nav fake_vel_transform 构建；不运行 sentry 硬件入口，不连接执行机构。性能比较不适用，无随机数。
停止条件：任何真实设备访问、非隔离ROS图混入、目标显示硬件控制。本地 ROS workspace 非Git仓库，提交前记录独立版本化方式。
实际结果：runtime_2/3/5三轮通过；runtime_1因脚本误判hz CLI SIGINT退出2失败，修正仅适用于CLI；runtime_4遗漏可写ROS_LOG_DIR，在观察节点初始化时失败。无硬件。
实测导航9.879–10.035Hz、云台18.233–20.125Hz、目标18.707–19.993Hz；不宣称严格20Hz或端到端延迟。首次timeout诊断age：导航211.487/235.408/229.280ms，云台252.168/251.337/218.294ms，目标501.243/538.285/518.616ms。
三路同时通信、导航/视觉独立停发与重启、无效目标、旧数据不继续增长均通过；全部生产进程SIGINT退出0，直接管理时0.112–0.203秒。生命周期30轮通过。
完整Restar ROS目标、独立replay、纯非ROS standard均编译成功；导航fake_vel_transform和bringup包编译成功。Nav2 Controller/Planner/Smoother运行包仍缺失，未验收整个导航仓库或完整Nav2。
最后colcon：20个pytest用例全过，23条汇总记录，0 errors/failures/skipped，CTest23.90秒。既有NaN/Inf/QoS/冻结stamp/输入消息与回放数值回归通过。
原始命令/graph/QoS/hz/JSON/日志：`/home/c/tongxin/ros2_ws/test_results/phase15_*`；最终报告`/home/c/tongxin/ROS_INTEGRATION_PHASE15.md`。
证据offline-reproduced；完整sentry因CAN/相机构造仍未无硬件运行；跨机UDP、全段姿态缺失录像、历史launch进程组信号升级项未通过。用户要求暂不提交Git，无新分支或提交。

## ROS-P1-004（2026-11-14，Codex）：项目自有 restar_msgs

用户明确要求用自有 restar_msgs 替换 sp_msgs，不再等待原包；不是伪造同名兼容包。
假设：保留两个已确认列表字段及发布时间，迁移为新类型，可构建完整包装器并保持已使用的输入语义。
候选 A（采用）：新包独立定义两个输入消息，迁移所有 C++/CMake 使用，既有 robot_interfaces 输出不变。
候选 B（不采用）：把导航 Twist 和已有输出一并改成新类型，扩大无关接口迁移并破坏标准速度接口。
基线分支/提交见 README；候选为本轮 workspace-change。Ubuntu22.04/Humble/C++17，CPU；无设备、录像或随机输入。
配置：隔离 ROS_DOMAIN_ID=83、ROS_LOCALHOST_ONLY=1；固定输入 [1,6]、[3,4]、空列表、[8]。
计划：colcon build restar_msgs robot_interfaces robot_ros_probe；interface show；独立 ROS 工具构建；
restar_ros_inputs_test 检查真实包装器的初始空值、字段映射、清空、1500ms 独立超时和两次重建；
完整 Restar ROS sentry/publish_test/topic_loop_test 仅编译不运行；既有 pytest 通信/录像回归；纯非ROS配置。
门槛：源码/构建无 sp_msgs 依赖，新消息生成，输入测试退出0，原回归0失败；完整编译失败如实记录具体依赖。
停止条件：需启动硬件或安装系统包。无预热、性能比较或硬件结论；原始输出 ros2_ws/test_results/restar_msgs_*。
结果待追加。

### ROS-P1-004 实际结果

- `colcon build --base-paths src --packages-select restar_msgs robot_interfaces robot_ros_probe --symlink-install`：3 包成功。
- `ros2 interface show` 两个 restar_msgs 消息均生成；字段为 timestamp 与 int8[] ID 列表。
- `RESTAR_ROS2_TOOLS_ONLY=ON` 编译通过，新增真实包装器无硬件测试程序成功构建。
- 完整 `RESTAR_WITH_ROS2=ON` CMake 配置与构建通过：sentry、sentry_bp、sentry_debug、
  sentry_multithread、publish_test、subscribe_test、topic_loop_test 全部构建成功。只编译，不运行硬件入口。
- `restar_ros_inputs_test`：2 轮字段映射、初始空值、更新清空、目标持续发布期间敌人列表独立超时、
  wrapper 销毁重建均 PASS，退出 0。同进程不同 namespace/context，不替代跨机器或真实导航发布方。
- 原 `publish_test` 的 String 与 VisionTarget2D 订阅 pytest 通过，退出 0。
- 18 项既有 Probe/Mock/算法录像回归在包迁移后为 18 passed、0 skipped、14.59秒；加入原 publisher 测试后为19 passed、18.35秒。
- 新增两项跨进程测试最终为2 passed、7.32秒：外部进程收到新 restar_msgs 的两轮列表/时间戳，
  并验证真实 wrapper 自检通过；原 publisher 输出兼容 String/VisionTarget2D。
- 新输入观察测试首次失败，实际原因是 C++ 测试假定订阅数恰好 1，而 Python 观察者新增了第二个；
  改为至少 1 后通过。此前将问题归因于 namespace 是错误判断，已纠正，未改变消息或运行时订阅实现。
- 首轮 colcon 集成时又暴露测试发现时序：重建 wrapper 后观察者先被发现，测试只发送一次，新的 wrapper 尚未匹配，ID mapping 失败。
  Mock 改为有界周期发布直到真实 wrapper 收到列表，仍要求值、独立超时、两轮重建全部满足；未更改生产订阅或放宽超时门槛。
- 纯非 ROS CMake configure 通过；Restar ROS 源码/构建不再引用 sp_msgs。
- 最终 `colcon test --packages-select robot_ros_probe`：3个pytest套件、20个用例全通过，CTest21.45秒；
  `colcon test-result --verbose`：23 tests、0 errors、0 failures、0 skipped，含3个ament包装记录。
  新输入套件2项耗时6.44秒；首次失败输出保存restar_msgs_colcon_initial_failure.log。
- 全程无相机、CAN、串口或 sentry 执行；既有 Nav2 runtime graph 仍未启动核查。

## ROS-P1-001（2026-11-13，Codex）
假设：共享消息及共用发布实现可在不加载 CAN/相机/串口时独立完成 ROS 数据通路验证。
基线见 README；候选为工作区差异。无录像、随机输入或硬件；固定 Mock 数据；无需随机种子。
平台：Ubuntu 22.04.5，ROS2 Humble，C++17，CPU 本地；性能不作算法比较。
计划：colcon build robot_interfaces robot_ros_probe；独立 cmake 构建 io/ros2；pytest 集成与故障测试。
阈值：字段/QoS/无效状态映射正确；三路独立更新，停止后分别超时；NaN/Inf 无效；正常终止/重启。
停止条件：任何测试需要打开设备；依赖缺失则记录阻塞。
原始输出：/home/c/tongxin/ros2_ws/test_results/（仅保留软件测试报告）。
尚未执行，结果后续追加。

### 实际结果（2026-11-13）
- colcon build：robot_interfaces、robot_ros_probe、fake_vel_transform 成功；两个 ros2 interface show 成功。
- cmake -S restar-25 -B restar-25/build-ros2 -DRESTAR_ROS2_TOOLS_ONLY=ON；cmake --build：成功。
- /usr/bin/python3 -m pytest -q src/robot_ros_probe/test/test_ros_topics.py --junitxml=test_results/dds_integration.xml：11 passed，0 skipped，6.80 秒。
- colcon test --packages-select robot_ros_probe；colcon test-result --verbose：12 tests，0 errors/failures/skipped（含 ament 包装记录）。
- cmake -S test -B build/inprocess；build/inprocess/inprocess_test：15 个 PASS 检查点，退出 0。
- restar_ros_lifecycle_test：30 轮不同 namespace 私有 context，立即 stop/join、销毁一个后另一个继续发布，退出 0。
- software_integration.launch.py：nav/gimbal/target 全 ok；发送 SIGINT，4 个子进程 cleanly，launch 退出 0。
- RESTAR_WITH_ROS2=OFF：CMake 配置与 standard 目标（及依赖）编译通过，不运行硬件程序。
- RESTAR_WITH_ROS2=ON：缺真实 sp_msgs，明确 FATAL_ERROR；完整包装器和真实算法调用点尚未编译/运行。
- Python py_compile、两个仓库 git diff --check：通过。
原始产物在 ros2_ws/test_results：dds_integration.xml/.log、inprocess.log、lifecycle.log、launch_smoke.log、nonros_build.log。
未记录算法性能提升；没有录像、真实 IMU/相机输入、硬件或跨机网络证据。

### 失败探索与修正
早期测试脚本的自定义 context 未配独立 executor，以及等待诊断后未等待目标订阅回调，均已修正。
DDS UDP 被沙箱禁止，提权也自动拒绝；最终同机实际跨进程通信成功，不声称 UDP 或跨机通过。
禁用全部 transport 的尝试失败，已删除该配置。重复节点名/rosout 的多 context 探索发生崩溃，
部署要求不同 namespace；默认主程序只创建一个包装器。正式生命周期结果不覆盖同名并发实例。
ASan 遇到预编译 ROS allocator 的尺寸检查，探索中关闭该项及 leak 检查后运行过，不能当作全面 sanitizer 验收。

## ROS-P1-002（2026-11-13，Codex）：自瞄录像到 ROS

用户明确本轮边界：自瞄与导航的数据通信均在范围内；不涉及导航如何规划/执行任务。
继续保留最终速度订阅、namespace/QoS/超时与双路联调；优先推进自瞄真实算法发布。implementation_mode 保持 workspace-change。
假设：复用原 YOLO/Solver/Tracker/Aimer 与 Publisher，可以用仓库 demo 录像/姿态验证真实算法输出到 ROS，
而无需猜测 sp_msgs 或创建任何硬件对象。该入口不是完整 sentry 全向感知/裁判订阅链。
候选 A（采用）：增加独立离线入口，显式构建开关，保留完整 sentry 的 sp_msgs 依赖检查。
候选 B（不采用）：为缺失 sp_msgs 创建替代消息或关闭订阅后运行 sentry，会误改旧接口/决策语义。
基线：README 中提交；候选为本轮工作区差异。
数据：assets/demo/demo.avi + demo.txt；配置 configs/demo.yaml；模型使用配置原有文件，不替换。
录像首两行相隔约 25 秒，测试从 frame=1 开始，不能把该间隔伪装成 30 Hz。
数据哈希、确切命令、帧数和统计在运行后追加。固定输入，无随机种子，无硬件/GUI。
计划：显式 non-ROS 原工程 + 单独 ROS replay 输出目标；构建并运行 60 帧，接入真实 Probe 和独立接收进程。
阈值：至少一个有效 Aimer 输出及已求解目标；收到的数值与本地算法 trace 一致；EOF 后无效/超时；
非法输入报错；SIGINT 正常退出；保留已有 Mock 通路。记录软件阶段耗时分位数，不声称实车实时性。
停止条件：输入不是普通文件、姿态非法或非递增、依赖缺失；不打开设备，不安装包，不修改算法。
原始输出：ros2_ws/test_results/replay*（忽略 Git）。运行结果待追加。

### ROS-P1-002 实际结果

- `cmake -S restar-25 -B /tmp/restar-p1-nonros -DRESTAR_WITH_ROS2=OFF -DRESTAR_BUILD_ROS2_REPLAY=ON`；
  `cmake --build /tmp/restar-p1-nonros --target restar_ros_replay -j2` 通过。
- 原有 configs/demo.yaml，帧 1..60，27 m/s 配置弹速，to_now=false，30 Hz 最大发布节奏；
  三次分别 60 帧、58 个 control、49 个目标观测、退出 0，无额外预热。
- `/usr/bin/python3 -m pytest -q src/robot_ros_probe/test --junitxml=test_results/replay_and_topics.xml`：
  17 passed、0 skipped、11.76 秒。有效算法结果与独立进程接收逐条匹配（1e-12），导航独立保持更新；
  SIGINT、非法参数、非递增姿态、图像多于姿态均覆盖。
- `autoaim_replay.launch.py` 与独立导航 Mock：三路 ok，随后 nav=ok 而视觉 timeout；进程均退出 0。
  单 SIGINT 通过 launcher 转发，显式检查没有 SIGTERM 升级。
  原始证据 replay_launch_smoke_single_signal.log 与 replay_launch_nav_single_signal.log。
- 更新 colcon test-result：19 tests、0 errors/failures/skipped（17 用例和 2 个 ament 包装记录）。
- 全文件试验未通过：FFmpeg 顺序解码 687 图像、620 姿态，程序报缺姿态并退出 1；不伪造输入。
  因此不声称完成整段录像验收，演示使用已验证片段。
- 首轮测试观察者每次只处理一个回调，60 帧中仅采集 19 个有效命令；排空就绪回调后修正，未降低样本门槛。
  首轮 smoke 对 ros2 run 外壳单发 SIGINT 未使子节点退出；另一次整个进程组加 launcher 转发双发 SIGINT，
  Probe 触发 5 秒后 SIGTERM 升级。最终直接管理 Mock、仅向 launcher 单发信号通过；双信号问题未宣称解决。
- cv2 文件只读核查先遇到用户 NumPy2 与系统 OpenCV ABI 冲突；PYTHONNOUSERSITE=1 后正常，不改依赖。
- 三次算法 P50/P95/P99 ms：11.423/17.086/25.436，11.186/17.234/22.678，10.908/14.858/16.226。
  三次消息构造及 publish 调用 P50/P95/P99 ms：0.055/0.071/0.175，0.052/0.108/0.337，0.054/0.085/0.197。
  仅软件局部耗时，无基线对比；不是接收端延迟或硬件实时性证明。
输入/模型 SHA256 固定在工作区根 ROS_INTEGRATION_PHASE1.md；原始 trace/JSON/log 位于忽略的 test_results。
结论：该录像片段的主相机算法输出到 ROS 为 offline-reproduced；完整 sentry/sp_msgs 仍 code-reviewed。

## ROS-P1-003（2026-11-13）：Probe 重复信号关闭

范围只改 Probe main，不改数据回调或算法。基线为 ROS-P1-002 当前工作区。
观察：默认 rclcpp 异步信号处理下，进程组 SIGINT 加 launcher 转发曾导致 Probe 5 秒后被升级 SIGTERM。
不据一次观察认定是 ROS 上游缺陷。候选 A：入口只用 sig_atomic_t 置位，主线程有界 spin 后统一销毁/shutdown；
候选 B：继续依赖 launcher 的 SIGTERM 升级。选 A，避免把升级当正常退出。
固定原 Mock/录像输入，ROS_DOMAIN_ID 独立、无硬件。阈值：重复 SIGINT 回归退出 0、<5 秒；
同样的进程组加转发 launch 测试无升级、三路接收和独立超时不回退；原通信套件重跑。
停止条件：需动导航算法/硬件或依赖安装。原始证据 test_results/replay_* 与 probe_signal_*。
结果待追加。

### ROS-P1-003 实际结果

- Probe 入口构建通过；信号回调不调用 ROS，主线程以 20 ms 有界 spin 检查退出标志，销毁 executor/node 后关闭 context。
- 直接重复 SIGINT 回归通过；全部 pytest 18 passed、0 skipped、12.24 秒。
  colcon test-result 为 20 tests、0 errors/failures/skipped（18 用例及 2 包装记录）。
- 整个进程组加 launcher 转发的日志仍出现 SIGTERM 升级，“无升级”验收失败；没有放宽该门槛。
- 为区分节点残留与日志检测，固定重跑并同步读取 /proc：发信号后 0.100 秒 Probe 存在，
  0.501 秒和 1.502 秒均已不存在，6.207 秒 launcher 退出 0。原始 probe_signal_observe.json/.log。
  这支持“进程已消失而 launcher 延迟报告”的解释，但未定位上游根因，不能声称组合退出完全修复。
- 原 ROS-P1-002 的早期双信号解释只能作为当时推测；不能把没有升级日志的单信号测试代替整组门槛。
证据等级：直接重复信号退出 offline-reproduced；launcher 无升级仍未通过。本轮不修改导航、ROS 上游或系统依赖。

收尾补测：修改后的入口在单次 launcher 转发 SIGINT 下，相关进程均退出 0、无升级，关闭约 0.216 秒。
证据 replay_launch_after_signal_change.log/.json；未用此结果替代整组信号的未通过项。
