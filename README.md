The following program can be compiled remotely using the makefile commands, in
particular `make kill` for removing dupe processes, then `make run` for 
running. However, this program is meant to be run continuously. As a result, 
a few commands can be found within the makefile for managing the service.
In particular, you can run `make disable`, `make enable`, and `make restart`
for starting the machine. As a final note, the makefile is meant to be
used remotely on a windows machine that has ssh. If you wish to run it on a
different environment, then you simply must change the ssh directory.

The code is split by responsibility:

- `src/houseNotif.c` owns the polling/debounce loop.
- `src/recommendation.c` and `src/json_utils.c` are side-effect-free core helpers.
- `src/config.c`, `src/logger.c`, and `src/instance_lock.c` handle local process state.
- `src/http_client.c` owns repeated curl setup and response handling.
- `src/house_api.c` maps the house sensor, fan control, and Pushover APIs.
- `src/portable.c` wraps platform differences for env vars, sleep, time, and PID.
- `include/` contains the module headers and shared settings.
- `tests/` contains small focused test programs.

Run `make test` to compile and execute the small core test binary on the
remote host. Run `make build` to compile the full notifier.

Run `make notify-test-build` to compile the notification smoke-test program
without sending a notification. Run `make notify-test` to compile it and send a
Pushover test notification using `keys.env`. The test message can be overridden
by setting `TEST_NOTIFICATION_MESSAGE` in the remote environment before running
`./test_notification`.
