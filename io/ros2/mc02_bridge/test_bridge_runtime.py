"""Independent ROS processes; no device access. Run with isolated domain83/localhost1."""
import json
import math
import os
from pathlib import Path
import signal
import struct
import subprocess
import time

from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import Twist
import pytest
import rclpy
from rclpy.executors import SingleThreadedExecutor
from rclpy.qos import QoSProfile, ReliabilityPolicy
from robot_interfaces.msg import GimbalCommand, VisionTarget2D
from std_msgs.msg import UInt8MultiArray


def crc16(data):
    crc = 0xffff
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = (crc >> 1) ^ (0x8408 if crc & 1 else 0)
    return crc


def decode(data):
    assert len(data) == 34 and data[:4] == b'SP\x01\x01'
    assert struct.unpack_from('<H', data, 6)[0] == 24
    assert struct.unpack_from('<H', data, 32)[0] == crc16(data[:32])
    values = struct.unpack_from('<5f4B', data, 8)
    vx, vy, wz, yaw, pitch, nav_valid, gimbal_valid, control, reserved = values
    assert all(math.isfinite(x) for x in values[:5])
    assert all(x in (0, 1) for x in values[5:8]) and reserved == 0
    assert nav_valid or vx == vy == wz == 0.0
    assert gimbal_valid or yaw == pitch == 0.0
    assert not gimbal_valid or control
    return struct.unpack_from('<H', data, 4)[0], values[:8]


def command():
    msg = GimbalCommand()
    msg.header.stamp = rclpy.clock.Clock().now().to_msg()
    msg.valid = msg.control_enabled = True
    msg.yaw, msg.pitch = .25, -.1
    return msg


def velocity():
    msg = Twist()
    msg.linear.x, msg.linear.y, msg.angular.z = .4, -.2, .3
    return msg


class Runtime:
    def __init__(self, path, parameters=None, best_effort=False, create_inputs=True):
        assert os.environ.get('ROS_DOMAIN_ID') == '83'
        assert os.environ.get('ROS_LOCALHOST_ONLY') == '1'
        assert os.environ.get('ROS_LOG_DIR')
        self.path = path
        self.context = rclpy.context.Context()
        rclpy.init(context=self.context)
        self.ns = '/mc02_bridge_runtime_test'
        self.node = rclpy.create_node('driver', namespace=self.ns, context=self.context)
        self.executor = SingleThreadedExecutor(context=self.context)
        self.executor.add_node(self.node)
        self.frames, self.arrivals, self.diagnostics, self.processes, self.logs = [], [], [], [], []
        self.commands, self.events = [], []
        self.node.create_subscription(UInt8MultiArray, 'bridge/frame', self.on_frame, 100)
        self.node.create_subscription(DiagnosticArray, 'bridge/status', self.on_diagnostic, 10)
        if create_inputs:
            self.nav = self.node.create_publisher(Twist, 'cmd_vel', 10)
            self.gimbal = self.node.create_publisher(GimbalCommand, 'vision/gimbal_cmd',
                QoSProfile(depth=1, reliability=ReliabilityPolicy.BEST_EFFORT if best_effort
                           else ReliabilityPolicy.RELIABLE))
            self.target = self.node.create_publisher(VisionTarget2D, 'vision/target',
                QoSProfile(depth=5, reliability=ReliabilityPolicy.BEST_EFFORT))
        self.prefix = Path(os.environ['MC02_BRIDGE_BIN_DIR'])
        self.ros_args = ['--ros-args', '-r', '__ns:=' + self.ns]
        args = []
        for key, value in (parameters or {}).items():
            args += ['-p', f'{key}:={str(value).lower() if isinstance(value, bool) else value}']
        self.bridge = self.start('bridge', [str(self.prefix / 'mc02_serial_bridge_node')] + self.ros_args + args)
        self.receiver = self.start('receiver', [str(self.prefix / 'mc02_mock_receiver')] + self.ros_args +
            ['-p', f'output_file:={path}/capture.bin'])

    def on_frame(self, msg):
        self.frames.append(bytes(msg.data)); self.arrivals.append(time.monotonic())

    def on_diagnostic(self, msg):
        self.diagnostics.append({s.name: {'reason': s.message, **{v.key: v.value for v in s.values}}
                                 for s in msg.status})

    def start(self, name, argv, cwd=None):
        log = (self.path / f'{name}.log').open('w')
        self.logs.append(log); self.commands.append({'name': name, 'argv': argv, 'cwd': str(cwd) if cwd else None})
        process = subprocess.Popen(argv, cwd=cwd, stdout=log, stderr=subprocess.STDOUT)
        self.processes.append((name, process))
        return process

    def spin(self, seconds):
        end = time.monotonic() + seconds
        while time.monotonic() < end:
            self.executor.spin_once(timeout_sec=.005)

    def wait(self, predicate, label, timeout=6):
        end = time.monotonic() + timeout
        while not predicate():
            assert time.monotonic() < end, f'{label}: {self.path}'
            self.spin(.01)
        self.events.append({'check': label, 'at': time.monotonic(), 'frames': len(self.frames)})

    @property
    def last(self):
        return decode(self.frames[-1])[1] if self.frames else None

    def ready(self, require_gimbal=True):
        self.wait(lambda: self.nav.get_subscription_count() and
                  (not require_gimbal or self.gimbal.get_subscription_count()) and self.frames,
                  'DDS discovery and initial frames')
        self.wait(lambda: self.node.count_subscribers(self.ns + '/bridge/frame') >= 2, 'receiver discovered')

    def drive(self, seconds, nav=True, gimbal=True, target=False):
        end = time.monotonic() + seconds
        next_nav = next_gimbal = 0
        while time.monotonic() < end:
            now = time.monotonic()
            if nav and now >= next_nav:
                self.nav.publish(velocity()); next_nav = now + .1
            if gimbal and now >= next_gimbal:
                m = command(); self.gimbal.publish(m); next_gimbal = now + .05
                if target:
                    t = VisionTarget2D(); t.header.stamp = m.header.stamp
                    t.valid = True; t.target_id = 6; t.x = 1.5; t.y = -.7
                    self.target.publish(t)
            self.spin(.004)

    def inject(self, publisher, msg, check, label):
        # Assert a newly observed frame, so an earlier invalid startup frame cannot pass a test.
        begin = len(self.frames)
        publisher.publish(msg)
        self.wait(lambda: len(self.frames) > begin and check(self.last), label)

    def stop(self, process):
        if process.poll() is None:
            process.send_signal(signal.SIGINT)
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill(); process.wait(); raise
        assert process.returncode == 0

    def close(self):
        errors = []
        for name, process in reversed(self.processes):
            try:
                self.stop(process)
            except Exception as error:
                errors.append(f'{name}: {error!r}')
        for log in self.logs:
            log.close()
        summary = {'commands': self.commands, 'events': self.events,
                   'frames': len(self.frames), 'process_exits': {n: p.returncode for n, p in self.processes},
                   'errors': errors, 'hardware': False}
        if len(self.arrivals) > 2:
            intervals = sorted((b-a)*1000 for a, b in zip(self.arrivals, self.arrivals[1:]))
            summary['arrival_interval_ms'] = {
                'p50': intervals[len(intervals)//2], 'p95': intervals[int(len(intervals)*.95)],
                'p99': intervals[int(len(intervals)*.99)]}
            summary['observed_hz'] = (len(self.arrivals)-1)/(self.arrivals[-1]-self.arrivals[0])
        (self.path / 'result.json').write_text(json.dumps(summary, indent=2))
        (self.path / 'diagnostics.json').write_text(json.dumps(self.diagnostics, indent=2))
        (self.path / 'observed_frames.bin').write_bytes(b''.join(self.frames))
        self.executor.shutdown(); self.node.destroy_node(); self.context.shutdown()
        assert not errors, errors
        raw = (self.path / 'capture.bin').read_bytes()
        assert raw and len(raw) % 34 == 0
        for i in range(0, len(raw), 34):
            decode(raw[i:i+34])  # Independent Python check of the C++ receiver's actual file.
        output = subprocess.run([str(self.prefix / 'mc02_frame_dump'), str(self.path / 'capture.bin')],
                                capture_output=True, text=True, timeout=5)
        (self.path / 'decoded.csv').write_text(output.stdout)
        assert output.returncode == 0, output.stderr
        assert 'dropped=0 io_error=0' in (self.path / 'receiver.log').read_text()


@pytest.fixture
def runtime(tmp_path, request):
    options = getattr(request, 'param', {})
    r = Runtime(tmp_path, **options)
    try:
        yield r
    finally:
        r.close()


def test_periodic_and_independent_channels(runtime):
    r = runtime; r.ready()
    assert r.last[5:] == (0, 0, 0)
    r.drive(.9, target=True)
    active = [decode(f)[1] for f in r.frames if decode(f)[1][5:] == (1, 1, 1)]
    assert len(active) >= 25
    for p in active:
        assert p[:5] == pytest.approx((.4, -.2, .3, .25, -.1), abs=1e-6)
    seq = [decode(f)[0] for f in r.frames]
    assert all((b-a) % 65536 > 0 for a, b in zip(seq, seq[1:]))
    r.drive(.4, nav=False)
    assert r.last[:3] == (0., 0., 0.) and r.last[5:] == (0, 1, 1)
    r.drive(.2)
    assert r.last[5:] == (1, 1, 1)
    r.drive(.4, gimbal=False)
    assert r.last[3:] == (0., 0., 1, 0, 0)
    r.drive(.2)
    assert r.last[5:] == (1, 1, 1)
    # Target invalidity is diagnostic only; no matching timestamps are required.
    t = VisionTarget2D(); t.header.stamp = command().header.stamp
    r.target.publish(t); r.drive(.15)
    assert r.last[5:] == (1, 1, 1)
    r.drive(.8, target=False)
    assert r.last[5:] == (1, 1, 1)
    assert any(d.get('target', {}).get('reason') == 'timeout' and
               d['navigation']['reason'] == 'ok' and d['gimbal']['reason'] == 'ok'
               for d in r.diagnostics)
    r.spin(.4)
    assert r.last == (0., 0., 0., 0., 0., 0, 0, 0)


def test_numeric_and_explicit_invalid_replaces_latest(runtime):
    r = runtime; r.ready()
    for kind in ['invalid', 'disabled', 'nan', 'inf', 'overflow']:
        r.drive(.15)
        assert r.last[6] == 1
        m = command()
        if kind == 'invalid': m.valid = False
        elif kind == 'disabled': m.control_enabled = False
        else: m.yaw = {'nan': float('nan'), 'inf': float('inf'), 'overflow': 1e100}[kind]
        r.inject(r.gimbal, m, lambda p: p[6] == 0, kind)
        assert r.last[3:5] == (0., 0.)
    for field, value in [('linear.x', float('nan')), ('angular.x', float('inf')), ('linear.y', 1e100)]:
        r.drive(.15)
        m = velocity(); vector, axis = field.split('.'); setattr(getattr(m, vector), axis, value)
        r.inject(r.nav, m, lambda p: p[5] == 0, field)
        assert r.last[:3] == (0., 0., 0.)
    # Zero speed/angle is a perfectly valid command.
    r.inject(r.nav, Twist(), lambda p: p[5] == 1 and p[:3] == (0., 0., 0.), 'zero speed valid')
    m = command(); m.yaw = m.pitch = 0.
    r.inject(r.gimbal, m, lambda p: p[6] == 1 and p[3:5] == (0., 0.), 'zero angle valid')


def test_timestamp_faults_do_not_renew_old_commands(runtime):
    r = runtime; r.ready(); r.drive(.15)
    frozen = command()
    r.inject(r.gimbal, frozen, lambda p: p[6] == 1, 'fresh stamped command')
    for _ in range(14):
        r.gimbal.publish(frozen); r.nav.publish(velocity()); r.spin(.025)
    assert r.last[5:] == (1, 0, 0)
    for kind in ['zero', 'malformed', 'old', 'future', 'regressed']:
        r.drive(.15)
        m = command()
        if kind == 'zero': m.header.stamp.sec = m.header.stamp.nanosec = 0
        elif kind == 'malformed': m.header.stamp.nanosec = 1000000000
        elif kind == 'old': m.header.stamp.sec -= 1
        elif kind == 'future': m.header.stamp.sec += 1
        else:
            ns = m.header.stamp.sec*10**9 + m.header.stamp.nanosec - 100000000
            m.header.stamp.sec, m.header.stamp.nanosec = divmod(ns, 10**9)
        r.inject(r.gimbal, m, lambda p: p[6] == 0, kind)
        assert r.last[3:5] == (0., 0.)
    r.drive(.2)
    assert r.last[5:] == (1, 1, 1)
    r.wait(lambda: len(r.diagnostics) >= 2, 'diagnostics available')


@pytest.mark.parametrize('runtime', [{'best_effort': True}], indirect=True)
def test_incompatible_gimbal_qos_expires_safely(runtime):
    r = runtime; r.ready(require_gimbal=False); r.drive(.5)
    assert r.gimbal.get_subscription_count() == 0
    assert r.last[5:] == (1, 0, 0)
    assert 'incompatible' in (r.path / 'bridge.log').read_text().lower()


@pytest.mark.parametrize('runtime', [{'parameters': {'frame_rate_hz': 25.0, 'subscribe_target': False}}], indirect=True)
def test_configured_rate_optional_target_and_no_sample_reuse(runtime):
    r = runtime; r.ready(); r.drive(.3)
    r.wait(lambda: r.diagnostics and r.diagnostics[-1].get('gimbal', {}).get('samples') != '0',
           'sample diagnostics')
    begin = len(r.frames); started = time.monotonic()
    r.spin(1.1)
    elapsed = time.monotonic() - started
    measured = (len(r.frames) - begin) / elapsed
    assert 20 < measured < 30
    assert r.target.get_subscription_count() == 0
    assert r.last[5:] == (0, 0, 0)
    r.wait(lambda: len(r.diagnostics) >= 3, 'post-stop diagnostics')
    a, b = r.diagnostics[-2:]
    assert a['gimbal']['samples'] == b['gimbal']['samples']
    assert int(b['gimbal']['frames']) > int(a['gimbal']['frames'])
    (r.path / 'rate.json').write_text(json.dumps({'configured_hz': 25, 'observed_hz': measured}))


def test_mock_receiver_rejects_corruption(runtime):
    r = runtime; r.ready()
    r.wait(lambda: 'decoded=' in (r.path / 'receiver.log').read_text(), 'receiver accepted initial frame')
    publisher = r.node.create_publisher(UInt8MultiArray, 'bridge/frame', 10)
    r.wait(lambda: publisher.get_subscription_count() >= 2, 'frame fault publisher discovery')
    bad = bytearray(r.frames[-1]); bad[10] ^= 1
    publisher.publish(UInt8MultiArray(data=list(bad)))
    r.wait(lambda: 'invalid frame count=1' in (r.path / 'receiver.log').read_text(), 'bad CRC rejected')
    # Remove corrupt injected observation: the receiver file must contain only accepted frames.
    r.frames = [f for f in r.frames if f != bytes(bad)]


def test_published_crc_vectors():
    vectors = json.loads((Path(__file__).parents[2] / 'mc02_serial/docs/test_vectors.json').read_text())
    for vector in vectors['crc']['vectors']:
        assert crc16(bytes.fromhex(vector['hex'])) == int(vector['crc_value'], 16)
    frame = vectors['example_frame']
    seq, values = decode(bytes.fromhex(frame['hex']))
    assert seq == frame['sequence']
    assert values[:5] == pytest.approx(tuple(frame['values'][k] for k in ['vx','vy','wz','yaw','pitch']), abs=1e-6)
    assert values[5:] == tuple(frame['flags'][k] for k in
        ['nav_valid','gimbal_valid','control_enabled'])
