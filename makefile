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

.PHONY: build run kill clean debug

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