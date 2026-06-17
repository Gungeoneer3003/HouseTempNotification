The following program can be compiled remotely using the makefile commands, in
particular `make kill` for removing dupe processes, then `make run` for 
running. However, this program is meant to be run continuously. As a result, 
a few commands can be found within the makefile for managing the service.
In particular, you can run `make disable`, `make enable`, and `make restart`
for starting the machine. As a final note, the makefile is meant to be
used remotely on a windows machine that has ssh. If you wish to run it on a
different environment, then you simply must change the ssh directory.