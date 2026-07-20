# gimbal_response_test 串口通信迁移记录

## 问题背景

用户需要测试 `gimbal_response_test`，但遇到以下问题：
- 原测试程序使用 CAN 总线通信（`CBoard` 类）
- 实际硬件使用 USB 串口通信（`/dev/ttyACM0`）
- 系统中没有 CAN 接口（`can0`）

## 问题分析

1. **配置文件缺失参数**
   - 初次运行时 `sentry.yaml` 缺少 `quaternion_canid`、`bullet_speed_canid`、`send_canid`、`can_interface` 参数
   - 通过参考 `example.yaml` 补充了这些参数

2. **通信方式不匹配**
   - `gimbal_response_test.cpp` 使用 `io::CBoard` 类（CAN 总线）
   - 实际硬件通过串口通信
   - 代码库中已有 `io::Gimbal` 类支持串口通信

3. **发现现有解决方案**
   - 项目中已有 `gimbal_test.cpp` 使用串口通信
   - `io::Gimbal` 类实现了完整的串口协议

## 解决方案

### 1. 配置文件修改

在 `configs/sentry.yaml` 中添加：
```yaml
can_interface: "can0"
quaternion_canid: 0x01
bullet_speed_canid: 0x101
send_canid: 0xff
```

### 2. 代码迁移

将 `tests/gimbal_response_test.cpp` 从 CAN 通信迁移到串口通信：

**头文件修改：**
```cpp
// 原代码
#include "io/cboard.hpp"
#include "io/command.hpp"

// 修改后
#include "io/gimbal/gimbal.hpp"
```

**初始化修改：**
```cpp
// 原代码
io::CBoard cboard(config_path);
io::Command command{0};

// 修改后
io::Gimbal gimbal(config_path);
float cmd_yaw = 0, cmd_pitch = 0;
```

**发送命令修改：**
```cpp
// 原代码
command.yaw = cmd_angle / 57.3;
command.pitch = cmd_angle / 57.3;
cboard.send(command);

// 修改后
cmd_yaw = cmd_angle / 57.3;
cmd_pitch = cmd_angle / 57.3;
gimbal.send(true, false, cmd_yaw, 0, 0, cmd_pitch, 0, 0);
```

**接收数据修改：**
```cpp
// 原代码
Eigen::Quaterniond q = cboard.imu_at(timestamp);

// 修改后
Eigen::Quaterniond q = gimbal.q(timestamp);
```

### 3. 重新编译

```bash
cd /home/rm/rmproject/sp_vision_25-main/build
cmake --build . --target gimbal_response_test -j$(nproc)
```

## 测试结果

✅ 程序成功启动并连接串口
✅ 接收到云台四元数数据
✅ 可以发送控制命令测试云台响应

## 通信协议对比

### CAN 总线协议（原方案）
- 使用 SocketCAN 接口
- 数据通过 CAN 帧传输
- 需要 `can0` 网络接口

### 串口协议（新方案）
- 使用 `/dev/ttyACM0` USB 串口
- 数据结构：`GimbalToVision` 和 `VisionToGimbal`
- 包含帧头 `{'S', 'P'}` 和 CRC16 校验

## 关键文件

- 测试程序：`tests/gimbal_response_test.cpp`
- 串口通信类：`io/gimbal/gimbal.hpp` 和 `io/gimbal/gimbal.cpp`
- 配置文件：`configs/sentry.yaml`
- 下位机代码：`/home/rm/rmproject/electric_framework-main`

## 经验总结

1. 优先检查项目中是否已有类似功能的实现
2. 串口通信比 CAN 总线更适合 USB 连接的开发板
3. 配置文件参数要与代码中的读取逻辑匹配
4. 测试前确认硬件设备存在（`ls /dev/ttyACM*`）
