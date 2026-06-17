#Need the host 
include keys.env

#For a windows machine, make sure to use the Sysnative path for ssh.exe, otherwise it will not work
SSH = C:/Windows/Sysnative/OpenSSH/ssh.exe
DIR = ~/Desktop/HouseTempNotification

TARGET = houseNotif
SRC = houseNotif.c
CC = gcc
CFLAGS = -g
LIBS = -lcurl

SERVICE = house-notification.service

.PHONY: build run kill clean debug start stop restart status disable enable

#For running the program 
debug:
	@$(SSH) -V
	@$(SSH) $(HOST) 'echo remote ssh works'

build:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)'

run: build
	$(SSH) -tt $(HOST) 'cd $(DIR) && ./$(TARGET)'

kill:
	$(SSH) $(HOST) 'pkill -INT -x $(TARGET) || echo "$(TARGET) not running"'

clean:
	$(SSH) $(HOST) 'cd $(DIR) && rm -f $(TARGET)'


#For managing the system service, which is actively running
start:
	$(SSH) $(HOST) 'sudo systemctl start $(SERVICE)'

stop:
	$(SSH) $(HOST) 'sudo systemctl stop $(SERVICE)'

restart:
	$(SSH) $(HOST) 'sudo systemctl restart $(SERVICE)'

status:
	$(SSH) $(HOST) 'sudo systemctl status $(SERVICE) --no-pager'

disable:
	$(SSH) $(HOST) 'sudo systemctl disable $(SERVICE)'

enable:
	$(SSH) $(HOST) 'sudo systemctl enable $(SERVICE)'