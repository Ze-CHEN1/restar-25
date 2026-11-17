# Phase 3 USB CDC 发送

现有34字节帧、CRC、ROS消息和缓存/超时逻辑不变。新增Linux termios串口输出，固定115200/8N1/raw、无流控；USB CDC实际速率由设备驱动决定。串口模式不发布`bridge/frame`，Mock模式不打开串口；输入诊断继续保留。

| 启动参数 | 默认值 | 类型/范围 |
|---|---|---|
| transport | mock | string，仅mock/serial |
| serial_device | /dev/ttyACM0 | string，非空串口路径，可用/dev/serial/by-id稳定路径 |
| serial_motion_enabled | false | bool，仅启动时读取 |
| serial_reconnect_ms | 250 | int，1..60000毫秒 |
| serial_write_timeout_ms | 50 | int，1..1000毫秒 |

`serial_motion_enabled=false`时，线上每周期仍发送原协议帧，但速度、角度及三个有效/使能标志全部为0。显式以true启动且首次打开成功后才允许发送原控制帧，输入本身仍须通过原有效性与超时检查。任何打开失败、写超时、断开或I/O错误均锁存禁用；设备恢复时继续发全零禁用帧，不自动恢复运动。恢复使能只能停止节点，再显式以`serial_motion_enabled:=true`重启；新进程仍需接收新的ROS数据。

工作线程独占fd，非阻塞open/write，poll最大5ms片段，ROS回调只覆盖单个待发槽，不做设备I/O。部分写入在同一帧内按偏移续写；EINTR重试；EAGAIN等待可写直到本帧截止；失败丢弃未发尾部并关闭，重连清空待发槽和设备缓冲，不补发历史队列。若发生截断，接收端应使用原协议CRC和SP重同步。串口写成功仅计为操作系统接受完整帧，不等于MC02接收、解析或执行成功。

构建与PTY验收（不打开真实串口）：

```bash
cd /home/c/tongxin/restar-25
source /opt/ros/humble/setup.bash
source /home/c/tongxin/ros2_ws/install/setup.bash
cmake -S . -B build-mc02 -DRESTAR_ROS2_TOOLS_ONLY=ON -DBUILD_TESTING=ON
cmake --build build-mc02 -j2
ctest --test-dir build-mc02 --output-on-failure
```

测试进程使用domain83/localhost-only。`test_serial_pty.py`自行创建PTY，发布合成Twist/GimbalCommand，逐字节检查接收帧的固定长度、数值、序号和CRC；关闭PTY主端并创建新的PTY后，验证持续重连、全零禁用且ROS发布仍在继续。`serial_sender_test.cpp`注入5字节短写、EINTR、EAGAIN，验证部分写、打开失败恢复和有界退出。原Mock/CRC/超时测试继续运行。

手动配置示例（将路径替换为自行创建的PTY；默认运动禁用）：

```bash
build-mc02/io/ros2/mc02_serial_bridge_node --ros-args \
  -p transport:=serial -p serial_device:=/dev/pts/REPLACE_WITH_PTY
```

本轮未连接MC02或真实执行器，也未开发固件/ACK。设备端参数、真实USB拔插、驱动行为及执行动作不属于PTY验证结论。
