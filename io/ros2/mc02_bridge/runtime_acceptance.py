#!/usr/bin/env python3
"""Real file algorithm + original fake_vel_transform -> Bridge -> binary file. No devices."""
import argparse
import json
from pathlib import Path
import sys
import time
from diagnostic_msgs.msg import DiagnosticArray
from geometry_msgs.msg import Twist
from robot_interfaces.msg import GimbalCommand, VisionTarget2D
from rclpy.qos import QoSProfile, ReliabilityPolicy
from test_bridge_runtime import Runtime, decode


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--restar-root', type=Path, required=True)
    parser.add_argument('--replay', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    assert args.replay.is_file()
    args.output.mkdir(parents=True, exist_ok=False)
    r = Runtime(args.output, create_inputs=False)
    commands, velocities, targets, probe_states = [], [], [], []
    r.node.create_subscription(GimbalCommand, 'vision/gimbal_cmd', commands.append, 100)
    r.node.create_subscription(Twist, 'cmd_vel', velocities.append, 100)
    r.node.create_subscription(VisionTarget2D, 'vision/target', targets.append,
        QoSProfile(depth=100, reliability=ReliabilityPolicy.BEST_EFFORT))
    r.node.create_subscription(DiagnosticArray, 'probe/status', probe_states.append, 10)
    result = {'passed': False, 'hardware': False, 'nav2': False,
              'source': 'real Restar file algorithm and original fake_vel_transform with synthetic navigation input'}
    try:
        transform = Path(os.environ['NAV_TRANSFORM_BIN'])
        probe = Path(os.environ['ROS_PROBE_BIN'])
        mock = Path(os.environ['ROS_MOCK_NAV_SCRIPT'])
        r.start('transform', [str(transform)] + r.ros_args + [
            '-p', 'robot_base_frame:=gimbal_yaw', '-p', 'fake_robot_base_frame:=gimbal_yaw_fake',
            '-p', 'odom_topic:=odometry', '-p', 'input_cmd_vel_topic:=cmd_vel_nav2_result',
            '-p', 'output_cmd_vel_topic:=cmd_vel', '-p', 'init_spin_speed:=0.0'])
        r.start('probe', [str(probe)] + r.ros_args + ['-p', 'summary_period_ms:=50.0'])
        nav_args = [sys.executable, str(mock)] + r.ros_args
        nav = r.start('nav_input', nav_args)
        r.wait(lambda: r.last and r.last[5:] == (1,0,0), 'navigation only')
        replay_args = [str(args.replay)] + r.ros_args + [
            '-p', f'config_path:={args.restar_root}/configs/demo.yaml',
            '-p', f'input_prefix:={args.restar_root}/assets/demo/demo',
            '-p', 'start_frame:=1', '-p', 'max_frames:=600', '-p', 'replay_rate_hz:=20.0']
        replay = r.start('algorithm', replay_args, args.restar_root)
        r.wait(lambda: r.last and r.last[5:] == (1,1,1) and targets and probe_states,
               'real sources simultaneously decoded', timeout=25)
        r.spin(1.5)
        topic = r.ns + '/cmd_vel'
        pubs = r.node.get_publishers_info_by_topic(topic)
        assert len(pubs) == 1 and pubs[0].node_name == 'fake_vel_transform', pubs
        graph = {'nodes': r.node.get_node_names_and_namespaces(), 'topics': r.node.get_topic_names_and_types(),
                 'endpoints': {}}
        for name in ['cmd_vel_nav2_result','cmd_vel','vision/gimbal_cmd','vision/target','bridge/frame']:
            path = r.ns + '/' + name
            graph['endpoints'][path] = {
                'publishers': [str(x) for x in r.node.get_publishers_info_by_topic(path)],
                'subscribers': [str(x) for x in r.node.get_subscriptions_info_by_topic(path)]}
        (args.output / 'graph.json').write_text(json.dumps(graph, indent=2))
        active = [decode(f)[1] for f in r.frames if decode(f)[1][5:] == (1,1,1)]
        assert len(active) >= 25
        for p in active:
            assert any(abs(p[0]-v.linear.x) < 1e-6 and abs(p[1]-v.linear.y) < 1e-6 and
                       abs(p[2]-v.angular.z) < 1e-6 for v in velocities)
            assert any(m.valid and m.control_enabled and abs(p[3]-m.yaw) < 1e-5 and
                       abs(p[4]-m.pitch) < 1e-5 for m in commands)
        r.stop(nav)
        r.wait(lambda: r.last[5:] == (0,1,1), 'nav stopped while actual algorithm stays live')
        r.spin(.25)
        assert r.last[:3] == (0.,0.,0.)
        nav = r.start('nav_restart', nav_args)
        r.wait(lambda: r.last[5:] == (1,1,1), 'nav restart')
        r.stop(replay)
        r.wait(lambda: r.last[5:] == (1,0,0), 'algorithm stopped while nav stays live')
        r.spin(.3)
        assert r.last[3:5] == (0.,0.)
        count = len(commands)
        r.wait(lambda: r.diagnostics and r.diagnostics[-1].get('gimbal', {}).get('reason') == 'timeout',
               'gimbal diagnostic timeout after source stop')
        sample_count = r.diagnostics[-1]['gimbal']['samples']
        r.spin(1.1)
        assert len(commands) == count
        assert r.diagnostics[-1]['gimbal']['samples'] == sample_count
        replay = r.start('algorithm_restart', replay_args, args.restar_root)
        r.wait(lambda: r.last[5:] == (1,1,1), 'algorithm restart', timeout=25)
        r.stop(replay); r.stop(nav)
        r.wait(lambda: r.last[5:] == (0,0,0), 'all stopped expire')
        result.update(passed=True, usable_frames=len(active), ros_commands=len(commands),
                      nav_messages=len(velocities), target_messages=len(targets))
    except Exception as error:
        result['error'] = repr(error)
        raise
    finally:
        try:
            r.close()
        finally:
            (args.output / 'acceptance.json').write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
