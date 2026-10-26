# Launch

This system acts a process launch system for a production environment. The system loads a python based description similar to the ROS2 launch API with synchronous evaluation. Once evaluated, the node will fork into the requested processes and begin monitoring each process. The system also exposes a ROS based control interface allowing for a user to interact with the system remotely. The control interface allows the user to get real-time status for each process as well as controlling the state of the system via requests. By default on start up, this node will attempt to load a launch configuration and start the processes when the node is started. Additionally if the launch node is shut down, all child processes will be closed before exiting gracefully.

## Python Launch File API

The python launch files are derived from the original ROS 2 launch API. The `launch_api` package contains the library infrastructure for creating custom launch configurations. The example launch configurations are located in the [launch directory](./api/launch/). Right now the system only supports ROS nodes with explicit variables or param files.

## Child Process Handling

Child processes are launched via a fork of the launch process. Once forked, the child will create a log file output, and replace itself with the requested process. This forked child will run as a normal process until it exits gracefully, or shuts down by its own accord. 

If the process is requested to exit via the launch node, the child process will be subject to escalating signals. First the child is sent a SIGINT, to attempt a graceful exit. If the shutdown is not observed in an allowable window of time, the signal will escalate to a SIGTERM. If sigterm is ignored again for the same period, the child will be sent an un-ignorable SIGKILL, and the child will be closed. 

## Control & Status Interface

The interface definitions are contained within the [process_msgs](../../interfaces/process_msgs/) package.

### Status Interface

The status of the system is published to `/launch/process_status` using the `process_msgs/msg/ProcessList` [interface](../../interfaces/process_msgs/msg/ProcessList.msg). This status interface includes information such as:
- The name of the launch node
- The active configuration file
- The time that the status was observed
- A list of process information determined by the configuration

The status of an individual process in `/launch/process_status` is derived from the `process_msgs/msg/ProcessInfo` [interface](../../interfaces/process_msgs/msg/ProcessInfo.msg) and includes:
- The ID of the process according to the launch file
- The name of the process (full node name for ROS nodes)
- The status of the process
    1. Gracefully Stopped
    2. Starting
    3. Running
    4. Watchdog Timed Out
    5. Stopping
    6. Restarting
    7. Dead (completed max # of restarts)
- The PID on the launch system
- The current percent cpu usage
- The current memory used in bytes
    

### Control Interface

Sending requests to the launch system is done via a service call to the launch node. the service is located at `/launch/request` and uses the `process_msgs/srv/RemoteRequest` [interface](../../interfaces/process_msgs/srv/RemoteRequest.srv).

The request is composed of 1 required argument and two optional arguments:
- The `request_id` is a selection based on the enum field contained within the message.
    1. None -  This option allows the user to test if the service is working via a ping - pong communication.
    2. Start - This option allows the user to start a process via the `id_argument` argument.
    3. Stop - This option allows the user to stop a process via the `id_argument` argument.
    4. Restart - This option allows the user to restart a process via the `id_argument` argument.
    5. Load Config - This option allows the user to change the loaded launch configuration a process via the `scenario_argument` argument. All processes must be shut down to change configurations. 
    6. Launch - This option allows the user to start all processes in the current launch configuration.
    7. Shutdown - This option allows the user to stop all processes in the current launch configuration. 

The response to the request contains both a "machine readable" error code field called `resp_code` and a corresponding human readable string contained in `response_msg`. Defined with `resp_code` is an enum of possible response codes that give more detailed information about the failure.