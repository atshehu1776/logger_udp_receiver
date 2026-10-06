from launch import LaunchDescription
from launch_ros.actions import Node


def generate_launch_description():

    return LaunchDescription([

        Node(
            package='logger_udp_receiver',
            executable='logger_udp_receiver',
            name='logger_udp_receiver',
            output='screen',

            parameters=[
                {
                    'udp_port': 9000,
                    'csv_file': 'logger_received.csv'
                }
            ]
        )
    ])
