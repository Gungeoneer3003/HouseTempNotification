cmake_minimum_required(VERSION 3.20)

project(HouseTempNotification C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

option(BUILD_HOUSE_APP "Build the full notifier executable" ON)

set(PROJECT_INCLUDE_DIRS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app
    ${CMAKE_CURRENT_SOURCE_DIR}/src/config
    ${CMAKE_CURRENT_SOURCE_DIR}/src/house
    ${CMAKE_CURRENT_SOURCE_DIR}/src/http
    ${CMAKE_CURRENT_SOURCE_DIR}/src/json
    ${CMAKE_CURRENT_SOURCE_DIR}/src/lock
    ${CMAKE_CURRENT_SOURCE_DIR}/src/logger
    ${CMAKE_CURRENT_SOURCE_DIR}/src/notification
    ${CMAKE_CURRENT_SOURCE_DIR}/src/poller
    ${CMAKE_CURRENT_SOURCE_DIR}/src/portable
    ${CMAKE_CURRENT_SOURCE_DIR}/src/recommendation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/settings
    ${CMAKE_CURRENT_SOURCE_DIR}/src/web_controls
)

set(LOGGER_WEB_SOURCES
    src/portable/portable_socket.c
    src/logger/logger.c
    src/logger/web/loggerWeb.c
    src/logger/web/loggerWebServer.c
    src/logger/web/loggerWebRoutes.c
    src/logger/web/loggerWebResponse.c
    src/logger/web/loggerWebTemplate.c
    src/logger/web/loggerWebPages.c
    src/logger/web/loggerWebAssets.c
    src/logger/web/loggerWebLogRows.c
    src/logger/web/loggerWebTime.c
    src/logger/web/graph/loggerWebGraphConfig.c
    src/logger/web/graph/loggerWebGraphRange.c
    src/logger/web/graph/loggerWebGraphJson.c
    src/logger/web/graph/loggerWebToday.c
)

find_package(Threads REQUIRED)

add_library(logger_web STATIC ${LOGGER_WEB_SOURCES})
target_include_directories(logger_web PUBLIC ${PROJECT_INCLUDE_DIRS})
target_link_libraries(logger_web PUBLIC Threads::Threads)
if(WIN32)
    target_link_libraries(logger_web PUBLIC ws2_32)
endif()

add_executable(test_core
    tests/test_core.c
    src/json/jsonUtils.c
    src/recommendation/rec.c
)
target_include_directories(test_core PRIVATE ${PROJECT_INCLUDE_DIRS})

add_executable(test_logger
    tests/test_logger.c
    src/logger/logger.c
)
target_include_directories(test_logger PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(test_logger PRIVATE Threads::Threads)

add_executable(test_logger_web tests/test_logger_web.c)
target_include_directories(test_logger_web PRIVATE ${PROJECT_INCLUDE_DIRS})
target_link_libraries(test_logger_web PRIVATE logger_web)

if(BUILD_HOUSE_APP)
    find_package(CURL)
    if(CURL_FOUND)
        add_executable(houseNotif
            src/houseNotif.c
            src/app/app_startup.c
            src/poller/poller.c
            src/notification/notification_worker.c
            src/web_controls/web_controls.c
            src/config/config.c
            src/house/houseApi.c
            src/http/httpClient.c
            src/lock/instanceLock.c
            src/json/jsonUtils.c
            src/portable/portable.c
            src/recommendation/rec.c
        )
        target_include_directories(houseNotif PRIVATE ${PROJECT_INCLUDE_DIRS})
        target_link_libraries(houseNotif PRIVATE logger_web CURL::libcurl Threads::Threads)
    else()
        message(STATUS "CURL was not found; skipping full houseNotif target")
    endif()
endif()

enable_testing()
add_test(NAME core COMMAND test_core)
add_test(NAME logger COMMAND test_logger)
