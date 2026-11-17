import json
import os
import pty
import select
import signal
import struct
import subprocess
import tty
import threading
import time
from pathlib import Path

from geometry_msgs.msg import Twist
import rclpy
from rclpy.executors import SingleThreadedExecutor
from robot_interfaces.msg import GimbalCommand
from std_msgs.msg import UInt8MultiArray
from test_bridge_runtime import crc16


def get_frames(fd, duration, result):
    data = bytearray()
    end = time.monotonic() + duration
    while time.monotonic() < end:
        ready, _, _ = select.select([fd], [], [], .02)
        if ready:
            try:
                data.extend(os.read(fd, 4096))
            except OSError:
                break
        while len(data) >= 34:
            pos = data.find(b'SP')
            if pos < 0:
                data.clear(); break
            if pos:
                del data[:pos]
            if len(data) < 34:
                break
            frame = bytes(data[:34])
            if struct.unpack_from('<H', frame, 32)[0] == crc16(frame[:32]):
                result.append(frame); del data[:34]
            else:
                del data[0]


def valid_command():
    msg = GimbalCommand()
    msg.header.stamp = rclpy.clock.Clock().now().to_msg()
    msg.valid = msg.control_enabled = True
    msg.yaw, msg.pitch = .25, -.1
    return msg


def test_ros_to_pty_continuous_disconnect_reconnect_stays_disabled(tmp_path):
    assert os.environ.get('ROS_DOMAIN_ID') == '83' and os.environ.get('ROS_LOCALHOST_ONLY') == '1'
    master, slave = pty.openpty()
    tty.setraw(slave)
    path = tmp_path / 'cdc'
    path.symlink_to(os.ttyname(slave))
    context = rclpy.context.Context(); rclpy.init(context=context)
    node = rclpy.create_node('pty_test_driver', namespace='/mc02_serial_pty', context=context)
    executor = SingleThreadedExecutor(context=context); executor.add_node(node)
    nav = node.create_publisher(Twist, 'cmd_vel', 10)
    gimbal = node.create_publisher(GimbalCommand, 'vision/gimbal_cmd', 10)
    frame_count = []
    node.create_subscription(UInt8MultiArray, 'bridge/frame', lambda m: frame_count.append(m), 10)
    prefix = Path(os.environ['MC02_BRIDGE_BIN_DIR'])
    bridge_log = (tmp_path / 'bridge.log').open('w')
    bridge = subprocess.Popen([str(prefix / 'mc02_serial_bridge_node'), '--ros-args',
        '-r', '__ns:=/mc02_serial_pty', '-p', 'transport:=serial',
        '-p', f'serial_device:={path}', '-p', 'serial_motion_enabled:=true',
        '-p', 'serial_reconnect_ms:=20', '-p', 'serial_write_timeout_ms:=100'],
        stdout=bridge_log, stderr=subprocess.STDOUT)
    received = []
    reader = threading.Thread(target=get_frames, args=(master, .8, received))
    try:
        deadline = time.monotonic()+4
        while time.monotonic()<deadline and not (gimbal.get_subscription_count() and nav.get_subscription_count()):
            executor.spin_once(timeout_sec=.01)
        assert gimbal.get_subscription_count() and nav.get_subscription_count()
        # DDS discovery can precede actual delivery; start the measurement after a usable frame.
        speed=Twist(); speed.linear.x=.4; speed.linear.y=-.2; speed.angular.z=.3
        warmup=[]
        deadline=time.monotonic()+6
        while time.monotonic()<deadline:
            nav.publish(speed); gimbal.publish(valid_command())
            executor.spin_once(timeout_sec=.01)
            get_frames(master,.02,warmup)
            if any(f[28:31]==b'\x01\x01\x01' for f in warmup):
                break
        assert any(f[28:31]==b'\x01\x01\x01' for f in warmup), (tmp_path/'bridge.log').read_text()
        reader.start()
        end = time.monotonic()+.75
        next_gimbal = next_nav = 0
        while time.monotonic()<end:
            now = time.monotonic()
            if now>=next_nav:
                speed=Twist(); speed.linear.x=.4; speed.linear.y=-.2; speed.angular.z=.3
                nav.publish(speed); next_nav=now+.05
            if now>=next_gimbal:
                gimbal.publish(valid_command()); next_gimbal=now+.05
            executor.spin_once(timeout_sec=.003)
        reader.join()
        assert len(received)>=20, len(received)
        seq=[]
        active_count=0
        for frame in received:
            assert len(frame)==34 and frame[:4]==b'SP\x01\x01'
            assert struct.unpack_from('<H',frame,6)[0]==24
            sequence=struct.unpack_from('<H',frame,4)[0]
            flags=frame[28:32]
            assert flags[3]==0
            assert flags[:3] in (b'\x00\x00\x00', b'\x01\x00\x00', b'\x00\x01\x01', b'\x01\x01\x01')
            nav_ok=flags[0]==1; gimbal_ok=flags[1]==1
            expected=struct.pack('<2sBBHH5f4B', b'SP',1,1,sequence,24,
                .4 if nav_ok else 0.,-.2 if nav_ok else 0.,.3 if nav_ok else 0.,
                .25 if gimbal_ok else 0.,-.1 if gimbal_ok else 0.,*flags)
            expected+=struct.pack('<H',crc16(expected))
            assert frame==expected  # Exact endian/payload/flags/CRC, with existing protocol.
            active_count+=nav_ok and gimbal_ok
            seq.append(sequence)
        assert active_count>=20
        assert all((b-a)%65536>0 for a,b in zip(seq,seq[1:]))
        assert not frame_count, 'serial mode must not also publish Mock frames'
        # Break the PTY. Recreate a new endpoint at the same configured path.
        os.close(master); master=-1; os.close(slave); slave=-1
        until=time.monotonic()+2
        log=''
        while time.monotonic()<until:
            executor.spin_once(timeout_sec=.01)
            log=(tmp_path/'bridge.log').read_text()
            if 'motion_enabled=0' in log:
                break
        assert 'motion_enabled=0' in log
        master,slave=pty.openpty(); tty.setraw(slave)
        path.unlink(); path.symlink_to(os.ttyname(slave))
        post=[]
        reader=threading.Thread(target=get_frames,args=(master,.6,post)); reader.start()
        end=time.monotonic()+.55
        while time.monotonic()<end:
            now=time.monotonic()
            if now>=next_nav: nav.publish(speed); next_nav=now+.05
            if now>=next_gimbal: gimbal.publish(valid_command()); next_gimbal=now+.05
            executor.spin_once(timeout_sec=.003)
        reader.join()
        assert len(post)>=10, len(post)
        for frame in post:
            assert struct.unpack_from('<H',frame,32)[0]==crc16(frame[:32])
            assert frame[28:32]==b'\x00\x00\x00\x00'
            assert struct.unpack_from('<5f',frame,8)==(0.,0.,0.,0.,0.)
        log=(tmp_path/'bridge.log').read_text()
        until=time.monotonic()+2
        while time.monotonic()<until and 'connected=1 motion_enabled=0' not in log:
            executor.spin_once(timeout_sec=.02)
            log=(tmp_path/'bridge.log').read_text()
        assert 'connected=1 motion_enabled=0' in log
        (tmp_path/'pty_result.json').write_text(json.dumps({'initial_frames':len(received),'active_frames':active_count,
          'reconnected_disabled_frames':len(post),'frame_size':34,'crc':'all passed',
          'motion_enabled_after_reconnect':False},indent=2))
    finally:
        if reader.is_alive(): reader.join(timeout=1)
        if bridge.poll() is None:
            bridge.send_signal(signal.SIGINT); bridge.wait(timeout=5)
        assert bridge.returncode==0, (tmp_path/'bridge.log').read_text()
        bridge_log.close(); executor.shutdown(); node.destroy_node(); context.shutdown()
        for fd in (master,slave):
            if fd>=0:
                try: os.close(fd)
                except OSError: pass
