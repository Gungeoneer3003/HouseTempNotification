#Need the host 
include keys.env

#For a windows machine, make sure to use the Sysnative path for ssh.exe, otherwise it will not work
SSH = C:/Windows/Sysnative/OpenSSH/ssh.exe
DIR = ~/Desktop/HouseTempNotification

TARGET = houseNotif
SRC_DIR = src
CONFIG_DIR = $(SRC_DIR)/config
HOUSE_DIR = $(SRC_DIR)/house
LOCK_DIR = $(SRC_DIR)/lock
JSON_DIR = $(SRC_DIR)/json
LOGGER_DIR = $(SRC_DIR)/logger
HTTP_DIR = $(SRC_DIR)/http
PORTABLE_DIR = $(SRC_DIR)/portable
RECOMMENDATION_DIR = $(SRC_DIR)/recommendation
SETTINGS_DIR = $(SRC_DIR)/settings
MODULE_DIRS = $(CONFIG_DIR) $(HOUSE_DIR) $(LOCK_DIR) $(JSON_DIR) $(LOGGER_DIR) $(HTTP_DIR) $(PORTABLE_DIR) $(RECOMMENDATION_DIR) $(SETTINGS_DIR)
SRC = $(SRC_DIR)/houseNotif.c $(CONFIG_DIR)/config.c $(HOUSE_DIR)/houseApi.c $(HTTP_DIR)/httpClient.c $(LOCK_DIR)/instanceLock.c $(JSON_DIR)/jsonUtils.c $(LOGGER_DIR)/logger.c $(LOGGER_DIR)/loggerWeb.c $(PORTABLE_DIR)/portable.c $(RECOMMENDATION_DIR)/rec.c
TEST_TARGET = test_core
TEST_SRC = tests/test_core.c $(JSON_DIR)/jsonUtils.c $(RECOMMENDATION_DIR)/rec.c
NOTIFY_TEST_TARGET = test_notification
NOTIFY_TEST_SRC = tests/test_notification.c $(CONFIG_DIR)/config.c $(HOUSE_DIR)/houseApi.c $(HTTP_DIR)/httpClient.c $(JSON_DIR)/jsonUtils.c $(PORTABLE_DIR)/portable.c
LOGGER_WEB_TEST_TARGET = test_logger_web
LOGGER_WEB_TEST_SRC = tests/test_logger_web.c $(LOGGER_DIR)/loggerWeb.c
CC = gcc
CFLAGS = -g -std=c11 -Wall -Wextra -Wpedantic $(addprefix -I,$(MODULE_DIRS))
LIBS = -lcurl -pthread

SERVICE = house-notification.service

.PHONY: build run kill clean debug test notify-test-build notify-test logger-web-test-build start stop restart status disable enable

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

logger-web-test-build:
	$(SSH) $(HOST) 'cd $(DIR) && $(CC) $(CFLAGS) $(LOGGER_WEB_TEST_SRC) -o $(LOGGER_WEB_TEST_TARGET) -pthread'

run: build
	$(SSH) -tt $(HOST) 'cd $(DIR) && ./$(TARGET)'

kill:
	$(SSH) $(HOST) 'pkill -INT -x $(TARGET) || echo "$(TARGET) not running"'

clean:
	$(SSH) $(HOST) 'cd $(DIR) && rm -f $(TARGET) $(TEST_TARGET) $(NOTIFY_TEST_TARGET) $(LOGGER_WEB_TEST_TARGET)'


#For managing the system service, which is actively running
start: build
	$(SSH) $(HOST) 'sudo systemctl start $(SERVICE)'

stop:
	$(SSH) $(HOST) 'sudo systemctl stop $(SERVICE)'

restart: build
	$(SSH) $(HOST) 'sudo systemctl restart $(SERVICE)'

status:
	$(SSH) $(HOST) 'sudo systemctl status $(SERVICE) --no-pager'

disable:
	$(SSH) $(HOST) 'sudo systemctl disable $(SERVICE)'

enable:
	$(SSH) $(HOST) 'sudo systemctl enable $(SERVICE)'
