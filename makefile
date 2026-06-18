#Need the host 
include keys.env

#For a windows machine, make sure to use the Sysnative path for ssh.exe, otherwise it will not work
SSH = C:/Windows/Sysnative/OpenSSH/ssh.exe
DIR = ~/Desktop/HouseTempNotification

TARGET = houseNotif
SRC_DIR = src
INC_DIR = include
SRC = $(SRC_DIR)/houseNotif.c $(SRC_DIR)/config.c $(SRC_DIR)/house_api.c $(SRC_DIR)/http_client.c $(SRC_DIR)/instance_lock.c $(SRC_DIR)/json_utils.c $(SRC_DIR)/logger.c $(SRC_DIR)/portable.c $(SRC_DIR)/recommendation.c
TEST_TARGET = test_core
TEST_SRC = tests/test_core.c $(SRC_DIR)/json_utils.c $(SRC_DIR)/recommendation.c
NOTIFY_TEST_TARGET = test_notification
NOTIFY_TEST_SRC = tests/test_notification.c $(SRC_DIR)/config.c $(SRC_DIR)/house_api.c $(SRC_DIR)/http_client.c $(SRC_DIR)/json_utils.c $(SRC_DIR)/portable.c
CC = gcc
CFLAGS = -g -std=c11 -Wall -Wextra -Wpedantic -I$(INC_DIR)
LIBS = -lcurl

SERVICE = house-notification.service

.PHONY: build run kill clean debug test notify-test-build notify-test start stop restart status disable enable

#For running the program 
debug:
	@$(SSH) -V
	@$(SSH) $(HOST) 'echo remote ssh works'

build:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(SRC) -o $(TARGET) $(LIBS)'

test:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(TEST_SRC) -o $(TEST_TARGET) && ./$(TEST_TARGET)'

notify-test-build:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(NOTIFY_TEST_SRC) -o $(NOTIFY_TEST_TARGET) $(LIBS)'

notify-test:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(NOTIFY_TEST_SRC) -o $(NOTIFY_TEST_TARGET) $(LIBS) && ./$(NOTIFY_TEST_TARGET)'

run: build
	$(SSH) -tt $(HOST) 'cd $(DIR) && ./$(TARGET)'

kill:
	$(SSH) $(HOST) 'pkill -INT -x $(TARGET) || echo "$(TARGET) not running"'

clean:
	$(SSH) $(HOST) 'cd $(DIR) && rm -f $(TARGET) $(TEST_TARGET) $(NOTIFY_TEST_TARGET)'


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
