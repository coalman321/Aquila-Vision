# ProcessLib

This is primarily a C++ library designed around the state of the `procfs` filesystem as of kernel 6.8.0-31. 
This library provides a C++ interface to process information running on the system. The library provides three key modules:
- ProcessMonitor: For monitoring a kernel level process
- ProcessController: For controlling the lifecycle of a kernel level process
- SystemMonitor: For getting key system information used for computing load averages, etc. 


## ProcessController output and exit status

By default a controlled process's stdout and stderr go to `<log_dir>/<process_name>.out`, which starts with the command line. Call `setOutputMode(OutputMode::PIPE)` before `launchProcess()` to read them yourself instead, through `getStdoutFd()`/`getStderrFd()`. The controller owns those descriptors and closes them on the next launch, so finish reading before relaunching. `setParentDeathSignal()` has the kernel signal the process if the parent dies first. The kernel tracks the parent *thread* that launched it, so the signal also fires if that thread exits.`getProcessStatus()` reaps a process that has exited and reports `exit_code`/`exit_signal`, unless`setIgnoreSigchild(true)` is in effect. In that case the kernel discards the status, and both stay `-1`/`0`. If the program can't be configured or exec'd, `launchProcess()` throws `std::runtime_error` instead.
