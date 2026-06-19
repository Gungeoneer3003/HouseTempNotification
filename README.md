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
- `src/recommendation/` and `src/json/` are side-effect-free core helpers.
- `src/config/` and `src/lock/` handle local process state.
- `src/logger/` contains the reusable log writer/trimmer module.
- `src/http/` contains the reusable libcurl HTTP client module.
- `src/house/` maps the house sensor, fan control, and Pushover APIs.
- `src/portable/` wraps platform differences for env vars, sleep, time, and PID.
- `src/settings/` contains shared compile-time settings.
- `tests/` contains small focused test programs.

Each module folder contains its public header and implementation file, so it can
be copied into another C program as a unit. Shared dependencies live under
`src/` as folders too; there is no separate root `include/` directory.

Run `make test` to compile and execute the small core test binary on the
remote host. Run `make build` to compile the full notifier.

Run `make notify-test-build` to compile the notification smoke-test program
without sending a notification. Run `make notify-test` to compile it and send a
Pushover test notification using `keys.env`. The test message can be overridden
by setting `TEST_NOTIFICATION_MESSAGE` in the remote environment before running
`./test_notification`.
