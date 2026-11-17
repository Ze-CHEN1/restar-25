from launch import LaunchDescription
import os
from pathlib import Path
from launch.actions import ExecuteProcess
from launch.substitutions import EnvironmentVariable, PathJoinSubstitution


def generate_launch_description():
    root = Path(__file__).resolve().parents[1]
    config = str(root / 'bridge.yaml')
    binary_dir = EnvironmentVariable('RESTAR_MC02_BIN_DIR')
    return LaunchDescription([
        ExecuteProcess(cmd=[PathJoinSubstitution([binary_dir, 'mc02_serial_bridge_node']), '--ros-args',
                            '--params-file', config], output='screen'),
        ExecuteProcess(cmd=[PathJoinSubstitution([binary_dir, 'mc02_mock_receiver']), '--ros-args',
                            '--params-file', config], output='screen'),
    ])
