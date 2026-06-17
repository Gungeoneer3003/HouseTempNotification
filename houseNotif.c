#define _POSIX_C_SOURCE 200809L
#include <curl/curl.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

//Poll and retry settings
#define POLL_INTERVAL_SECONDS 300
#define SENSOR_RETRY_COUNT 10
#define CURL_CONNECT_TIMEOUT_SECONDS 10L
#define CURL_TOTAL_TIMEOUT_SECONDS 20L
#define MAX_RESPONSE_BYTES ((size_t)64 * 1024)

//Alert tuning settings
#define OPEN_MARGIN 2
#define CLOSE_MARGIN 2
#define REQUIRED_STABLE_POLLS 2
#define SAME_ALERT_REMINDER_SECONDS 0

//Log settings
#define DEFAULT_LOG_FILE "house_notify.log"
#define DEFAULT_LOCK_FILE "house_notify.lock"
#define LOG_RETENTION_DAYS 30
#define LOG_TRIM_INTERVAL_SECONDS 86400

//Configuration values
static const char* envFile = "keys.env";
static const char* API = NULL;
static const char* USER_KEY = NULL;
static const char* HOUSE_LINK = NULL;
static const char* CGI_PART = NULL;
static const char* POWER_PART = NULL;
static const char* LOG_PATH = DEFAULT_LOG_FILE;
static const char* LOCK_PATH = DEFAULT_LOCK_FILE;
static int lockFd = -1;

//Built request links
static char CGILINK[256];
static char SHUTOFFLINK[256];

//Curl response memory
struct Memory {
    char* response;
    size_t size;
};

//Window recommendation states
typedef enum {
    REC_NONE = 0,
    REC_OPEN,
    REC_CLOSE
} Recommendation;

//Configuration helpers
static void loadDotenv(const char* filename);
static char* trimWhitespace(char* str);
static int initConfig(void);
static int buildLinks(void);
static int acquireInstanceLock(void);
static void releaseInstanceLock(void);

//Curl helpers
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
static int houseSense(int* house, int* oa, int* power);
static int turnOffFans(void);
static int sendPushover(const char* message);
static int curlHttpOk(CURL* curl, CURLcode res, const char* action);

//Parsing helpers
static int parseJsonInt(const char* json, const char* key, int* out);

//Notification helpers
static Recommendation getRecommendation(int house, int oa, int power);
static const char* recommendationName(Recommendation rec);
static int shouldSend(Recommendation rec, Recommendation lastSent, time_t lastSentTime);

//Log helpers
static int appendLog(const char* event, int house, int oa, int power, Recommendation rec, const char* detail);
static void writeLogField(FILE* file, const char* value);
static void trimLogFile(void);

//Loop function for sensing and messaging
int main(void) {
    //Start the program!
    printf("Starting House Temperature Notification System\n");
    fflush(stdout);

    //Get the config from the .env file
    if (!initConfig()) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    //Build the sensor and power control links
    if (!buildLinks()) {
        fprintf(stderr, "Failed to build sensor/control links\n");
        return EXIT_FAILURE;
    }

    //Prevent two copies from running and sending duplicate notifications
    if (!acquireInstanceLock()) {
        return EXIT_FAILURE;
    }

    //Initialize libcurl globally
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != 0) {
        fprintf(stderr, "Failed to initialize libcurl\n");
        return EXIT_FAILURE;
    }

    //If the porgram is restarted, trim the log file to remove old entries
    trimLogFile();
    appendLog("startup", -1, -1, -1, REC_NONE, "program started");

    //Initialize state variables for the main loop
    Recommendation lastSent = REC_NONE;
    Recommendation pending = REC_NONE;
    int pendingCount = 0;
    time_t lastSentTime = 0;
    time_t lastLogTrimTime = time(NULL);

    while (1) {
        //Readings from the sensor
        int house = 0;
        int oa = 0;
        int power = 0;
        time_t now = time(NULL);

        //Trim the file if it's been a while since the last trim
        if (difftime(now, lastLogTrimTime) >= LOG_TRIM_INTERVAL_SECONDS) {
            trimLogFile();
            lastLogTrimTime = now;
        }

        //Get the current sensor readings
        if (!houseSense(&house, &oa, &power)) {
            fprintf(stderr, "Skipping this poll because the sensor read failed\n");
            appendLog("sensor_fail", -1, -1, -1, REC_NONE, "sensor read failed");
            sleep(POLL_INTERVAL_SECONDS);
            continue;
        }

        //Get the status based on the current readings
        Recommendation rec = getRecommendation(house, oa, power);

        //If there is no recommendation, reset the pending state and continue
        if (rec == REC_NONE) {
            if (pending != REC_NONE) {
                printf("Recommendation cleared before debounce completed In:%d Out:%d Power:%d\n",
                       house, oa, power);
                appendLog("cleared", house, oa, power, rec, "recommendation cleared before debounce completed");
            } else {
                printf("No action needed In:%d Out:%d Power:%d\n", house, oa, power);
                appendLog("idle", house, oa, power, rec, "no action needed");
            }

            pending = REC_NONE;
            pendingCount = 0;
            lastSent = REC_NONE;
            lastSentTime = 0;
            sleep(POLL_INTERVAL_SECONDS);
            continue;
        }

        //Check if the cur rec is the same as the pending rec
        if (rec == pending) {
            pendingCount++;
        } else {
            pending = rec;
            pendingCount = 1;
        }

        //Log the pending recommendation
        printf("Pending recommendation: %s (%d/%d) In:%d Out:%d Power:%d\n",
               recommendationName(rec), pendingCount, REQUIRED_STABLE_POLLS, house, oa, power);
        appendLog("pending", house, oa, power, rec, "recommendation waiting for debounce");

        //If the close recommendation is stable, keep trying to shut off fans even if
        //the repeated close notification itself is suppressed
        int fanOffOk = 1;
        if (pendingCount >= REQUIRED_STABLE_POLLS && rec == REC_CLOSE) {
            fanOffOk = turnOffFans();
        }

        //If the recommendation has been stable for enough polls, send a notification
        if (pendingCount >= REQUIRED_STABLE_POLLS && shouldSend(rec, lastSent, lastSentTime)) {
            char message[160];

            //If the rec is simply to open windows, then make that the message
            //Otherwise, include the fan shutoff result in the message
            if (rec == REC_OPEN) {
                snprintf(message, sizeof(message), "Open the windows (Out:%d In:%d)", oa, house);
            } else {
                //Make the message depending on if turning off the fan was successful or not
                if (fanOffOk) {
                    snprintf(message, sizeof(message), "Close the windows (Out:%d In:%d)", oa, house);
                } else {
                    snprintf(message, sizeof(message),
                             "Close the windows (fan shutoff failed, Out:%d In:%d)", oa, house);
                }
            }

            //Send the notification and log the result
            if (sendPushover(message)) {
                lastSent = rec;
                lastSentTime = time(NULL);
                printf("Sent notification: %s\n", message);
                appendLog("notify_sent", house, oa, power, rec, message);
            } else {
                appendLog("notify_failed", house, oa, power, rec, message);
            }

        } else if (pendingCount >= REQUIRED_STABLE_POLLS) {
            //Check if the recommendation was already sent earlier
            printf("Already sent %s; suppressing repeat notification\n", recommendationName(rec));
            appendLog("suppressed", house, oa, power, rec, "repeat notification suppressed");
        }

        //Wait for the next poll
        sleep(POLL_INTERVAL_SECONDS);
    }

    //Cleanup libcurl before exiting
    //(Technically shouldn't get here)
    curl_global_cleanup();
    return EXIT_SUCCESS;
}

//Decide if windows should be opened or closed
static Recommendation getRecommendation(int house, int oa, int power) {
    int diff = oa - house;

    if (!power && diff <= -OPEN_MARGIN) {
        return REC_OPEN;
    }

    if (power && diff >= CLOSE_MARGIN) {
        return REC_CLOSE;
    }

    return REC_NONE;
}

//Convert recommendation to text
static const char* recommendationName(Recommendation rec) {
    switch (rec) {
        case REC_OPEN:
            return "open";
        case REC_CLOSE:
            return "close";
        default:
            return "none";
    }
}

//Decide whether an alert should send
static int shouldSend(Recommendation rec, Recommendation lastSent, time_t lastSentTime) {
    if (rec != lastSent) {
        return 1;
    }

//Make a compile time decision on whether to check for time since last sent
#if SAME_ALERT_REMINDER_SECONDS > 0
    time_t now = time(NULL);
    if (lastSentTime == 0 || difftime(now, lastSentTime) >= SAME_ALERT_REMINDER_SECONDS) {
        return 1;
    }
#else
    (void)lastSentTime;
#endif

    return 0;
}

//Collect curl response data
static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    if (nmemb != 0 && size > ((size_t)-1) / nmemb) {
        return 0;
    }

    size_t totalSize = size * nmemb;
    struct Memory* mem = (struct Memory*)userp;

    //Check if the new data fits
    if (mem->size > MAX_RESPONSE_BYTES || totalSize > MAX_RESPONSE_BYTES - mem->size) {
        return 0;
    }

    char* ptr = realloc(mem->response, mem->size + totalSize + 1);
    if (!ptr) {
        return 0;
    }

    //Get the relevant data and store it
    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, totalSize);
    mem->size += totalSize;
    mem->response[mem->size] = '\0';

    return totalSize;
}

//Check curl transport and HTTP status together
static int curlHttpOk(CURL* curl, CURLcode res, const char* action) {
    if (res != CURLE_OK) {
        fprintf(stderr, "%s failed: %s\n", action, curl_easy_strerror(res));
        return 0;
    }

    long responseCode = 0;
    if (curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode) == CURLE_OK &&
        responseCode != 0 &&
        (responseCode < 200 || responseCode >= 300)) {
        fprintf(stderr, "%s failed with HTTP status %ld\n", action, responseCode);
        return 0;
    }

    return 1;
}

//Send a Pushover notification
static int sendPushover(const char* message) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "Failed to initialize curl for Pushover\n");
        return 0;
    }

    //URL-encode the API token, user key, and message
    char* token = curl_easy_escape(curl, API, 0);
    char* user = curl_easy_escape(curl, USER_KEY, 0);
    char* encodedMessage = curl_easy_escape(curl, message, 0);

    //If any of the URL-encoding failed, clean up and return
    if (!token || !user || !encodedMessage) {
        fprintf(stderr, "Failed to URL-encode Pushover fields\n");
        if (token) curl_free(token);
        if (user) curl_free(user);
        if (encodedMessage) curl_free(encodedMessage);
        curl_easy_cleanup(curl);
        return 0;
    }

    //Calculate the length of the POST body and allocate memory for it
    size_t postLen = strlen("token=&user=&message=") + strlen(token) + strlen(user) + strlen(encodedMessage) + 1;
    char* postfields = malloc(postLen);
    if (!postfields) {
        fprintf(stderr, "Failed to allocate Pushover POST body\n");
        curl_free(token);
        curl_free(user);
        curl_free(encodedMessage);
        curl_easy_cleanup(curl);
        return 0;
    }

    snprintf(postfields, postLen, "token=%s&user=%s&message=%s", token, user, encodedMessage);

    struct Memory chunk;
    chunk.response = NULL;
    chunk.size = 0;

    //Set up the curl request for Pushover
    curl_easy_setopt(curl, CURLOPT_URL, "https://api.pushover.net/1/messages.json");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT_SECONDS);
    curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

    //Perform the request and check for errors
    CURLcode res = curl_easy_perform(curl);
    int ok = curlHttpOk(curl, res, "Pushover request");
    if (ok) {
        int status = 0;
        if (!chunk.response || !parseJsonInt(chunk.response, "status", &status) || status != 1) {
            fprintf(stderr, "Pushover response did not confirm success\n");
            ok = 0;
        }
    }

    if (res == CURLE_WRITE_ERROR && chunk.size >= MAX_RESPONSE_BYTES) {
        fprintf(stderr, "Pushover response exceeded %zu bytes\n", MAX_RESPONSE_BYTES);
        ok = 0;
    }

    //Clean up and return
    free(chunk.response);
    free(postfields);
    curl_free(token);
    curl_free(user);
    curl_free(encodedMessage);
    curl_easy_cleanup(curl);
    return ok;
}

//Turn off the house fans
static int turnOffFans(void) {
    //Make a certain amount of attempts before giving up
    for (int attempt = 0; attempt < SENSOR_RETRY_COUNT; attempt++) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "Failed to initialize curl for fan shutoff\n");
            appendLog("fan_fail", -1, -1, -1, REC_CLOSE, "curl initialization failed");
            return 0;
        }

        struct Memory chunk;
        chunk.response = NULL;
        chunk.size = 0;

        //Set up the curl request for fan shutoff
        curl_easy_setopt(curl, CURLOPT_URL, SHUTOFFLINK);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SECONDS);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT_SECONDS);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        //Perform the request and check for errors
        CURLcode res = curl_easy_perform(curl);
        int ok = curlHttpOk(curl, res, "Fan shutoff request");
        free(chunk.response);
        curl_easy_cleanup(curl);

        //If the request was successful, log it and return success
        if (ok) {
            printf("Successfully turned off fans\n");
            appendLog("fan_off", -1, -1, -1, REC_CLOSE, "fan shutoff request succeeded");
            return 1;
        }

        //If the request failed, log the error and retry after a short delay
        fprintf(stderr, "Fan shutoff failed attempt %d of %d\n",
                attempt + 1, SENSOR_RETRY_COUNT);
        sleep(1);
    }

    appendLog("fan_fail", -1, -1, -1, REC_CLOSE, "fan shutoff request failed after retries");
    return 0;
}

//Read house temperature data
static int houseSense(int* house, int* oa, int* power) {
    //Likewise, make a certain amount of attempts before giving up
    for (int attempt = 0; attempt < SENSOR_RETRY_COUNT; ++attempt) {
        CURL* curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "Failed to initialize curl for sensor read\n");
            return 0;
        }

        struct Memory chunk;
        chunk.response = NULL;
        chunk.size = 0;

        //Set up the curl request for sensor data
        curl_easy_setopt(curl, CURLOPT_URL, CGILINK);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
        curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, CURL_CONNECT_TIMEOUT_SECONDS);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, CURL_TOTAL_TIMEOUT_SECONDS);
        curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);

        //Perform the request and check for errors
        CURLcode res = curl_easy_perform(curl);
        int ok = curlHttpOk(curl, res, "Sensor read");
        curl_easy_cleanup(curl);

        //If the request was successful and there was a response, parse the JSON data        
        if (ok && chunk.response) {
            int newHouse = 0;
            int newOa = 0;
            int newPower = 0;

            //Parse the JSON response for the inside, outside, and power values
            if (parseJsonInt(chunk.response, "inside", &newHouse) &&
                parseJsonInt(chunk.response, "oa", &newOa) &&
                parseJsonInt(chunk.response, "power", &newPower)) {
                *house = newHouse;
                *oa = newOa;
                *power = newPower;
                free(chunk.response);
                return 1;
            }

            fprintf(stderr, "Sensor response was missing inside/oa/power fields: %s\n", chunk.response);
        } else {
            fprintf(stderr, "Sensor read failed attempt %d of %d\n",
                    attempt + 1, SENSOR_RETRY_COUNT);
        }

        free(chunk.response);
        sleep(1);
    }

    fprintf(stderr, "Failed to read valid data from house sensor after %d attempts\n", SENSOR_RETRY_COUNT);
    return 0;
}

//Parse an integer from the sensor JSON
static int parseJsonInt(const char* json, const char* key, int* out) {
    char pattern[64];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    //Find the key in the JSON string
    const char* start = strstr(json, pattern);
    if (!start) {
        return 0;
    }

    start += strlen(pattern);
    while (isspace((unsigned char)*start)) {
        start++;
    }

    //The next char should be a colon
    if (*start != ':') {
        return 0;
    }

    start++;
    while (isspace((unsigned char)*start)) {
        start++;
    }

    //Parse the integer value after the colon
    char* end = NULL;
    errno = 0;

    long value = strtol(start, &end, 10);
    if (end == start || errno == ERANGE || value < INT_MIN || value > INT_MAX) {
        fprintf(stderr, "Failed to parse integer for key %s in JSON\n", key);
        return 0;
    }

    *out = (int)value;
    return 1;
}

//Remove whitespace around dotenv values
static char* trimWhitespace(char* str) {
    if (!str) {
        return NULL;
    }

    while (isspace((unsigned char)*str)) {
        str++;
    }

    if (*str == '\0') {
        return str;
    }

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }
    *(end + 1) = '\0';

    return str;
}

//Load key and value pairs from keys.env
static void loadDotenv(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) {
        return;
    }

    //Get a single line from the file and parse it into a key and value
    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char* trimmed = trimWhitespace(line);
        if (!trimmed || *trimmed == '\0' || *trimmed == '#') {
            continue;
        }

        char* equals = strchr(trimmed, '=');
        if (!equals) {
            continue;
        }

        //Split the line into the key and value
        *equals = '\0';
        char* key = trimWhitespace(trimmed);
        char* value = trimWhitespace(equals + 1);

        size_t valueLen = strlen(value);
        if (valueLen >= 2 && *value == '"' && value[valueLen - 1] == '"') {
            value[valueLen - 1] = '\0';
            value++;
        }

        //Set the var accordingly
        if (*key && *value) {
            setenv(key, value, 1);
        }
    }

    fclose(file);
}

//Initialize environment configuration
static int initConfig(void) {
    loadDotenv(envFile);

    //Go through each private required variable
    API = getenv("API");
    USER_KEY = getenv("USER_KEY");
    HOUSE_LINK = getenv("HOUSE_LINK");
    CGI_PART = getenv("CGI_PART");
    POWER_PART = getenv("POWER_PART");

    const char* logPath = getenv("LOG_FILE");
    if (logPath && *logPath) {
        LOG_PATH = logPath;
    }

    const char* lockPath = getenv("LOCK_FILE");
    if (lockPath && *lockPath) {
        LOCK_PATH = lockPath;
    }

    //Check if anything is missing and log the result
    if (!API || !*API || !USER_KEY || !*USER_KEY || !HOUSE_LINK || !*HOUSE_LINK ||
        !CGI_PART || !*CGI_PART || !POWER_PART || !*POWER_PART) {
        fprintf(stderr,
                "Missing configuration in %s or environment: API=%s USER_KEY=%s HOUSE_LINK=%s CGI_PART=%s POWER_PART=%s\n",
                envFile,
                (API && *API) ? "ok" : "missing",
                (USER_KEY && *USER_KEY) ? "ok" : "missing",
                (HOUSE_LINK && *HOUSE_LINK) ? "ok" : "missing",
                (CGI_PART && *CGI_PART) ? "ok" : "missing",
                (POWER_PART && *POWER_PART) ? "ok" : "missing");
        return 0;
    }

    return 1;
}

//Build sensor and power URLs
static int buildLinks(void) {
    int n = snprintf(CGILINK, sizeof(CGILINK), "%s%s", HOUSE_LINK, CGI_PART);
    if (n < 0 || (size_t)n >= sizeof(CGILINK)) {
        fprintf(stderr, "CGI link is too long\n");
        return 0;
    }

    n = snprintf(SHUTOFFLINK, sizeof(SHUTOFFLINK), "%s%s", HOUSE_LINK, POWER_PART);
    if (n < 0 || (size_t)n >= sizeof(SHUTOFFLINK)) {
        fprintf(stderr, "Power link is too long\n");
        return 0;
    }

    return 1;
}

//Take an advisory lock so only one notifier instance can run at a time
static int acquireInstanceLock(void) {
    lockFd = open(LOCK_PATH, O_RDWR | O_CREAT, 0644);
    if (lockFd < 0) {
        fprintf(stderr, "Failed to open lock file %s: %s\n", LOCK_PATH, strerror(errno));
        return 0;
    }

    struct flock lock;
    memset(&lock, 0, sizeof(lock));
    lock.l_type = F_WRLCK;
    lock.l_whence = SEEK_SET;

    if (fcntl(lockFd, F_SETLK, &lock) != 0) {
        if (errno == EACCES || errno == EAGAIN) {
            fprintf(stderr, "Another %s instance is already running\n", LOCK_PATH);
        } else {
            fprintf(stderr, "Failed to lock %s: %s\n", LOCK_PATH, strerror(errno));
        }

        close(lockFd);
        lockFd = -1;
        return 0;
    }

    if (ftruncate(lockFd, 0) == 0) {
        char pidText[64];
        int n = snprintf(pidText, sizeof(pidText), "%lld\n", (long long)getpid());
        if (n > 0 && (size_t)n < sizeof(pidText)) {
            ssize_t written = write(lockFd, pidText, (size_t)n);
            (void)written;
        }
    }

    atexit(releaseInstanceLock);
    return 1;
}

//Release the advisory instance lock on clean process exit
static void releaseInstanceLock(void) {
    if (lockFd >= 0) {
        close(lockFd);
        lockFd = -1;
    }
}

//Append one line to the rolling log
static int appendLog(const char* event, int house, int oa, int power, Recommendation rec, const char* detail) {
    FILE* file = fopen(LOG_PATH, "a");
    if (!file) {
        fprintf(stderr, "Failed to open log file %s\n", LOG_PATH);
        return 0;
    }

    //Get the current time and format it as a timestamp
    time_t now = time(NULL);
    struct tm local;
    char timestamp[32];

    //Get the local time and format it as a timestamp, or use "unknown" if it fails
    if (localtime_r(&now, &local)) {
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local);
    } else {
        snprintf(timestamp, sizeof(timestamp), "unknown");
    }

    //Write the log entry with all relevant fields
    fprintf(file, "%lld|%s|%d|%d|%d|%s|", (long long)now, timestamp, house, oa, power, recommendationName(rec));
    writeLogField(file, event ? event : "");
    fputc('|', file);
    writeLogField(file, detail ? detail : "");
    fputc('\n', file);

    //Close the file and return success
    fclose(file);
    return 1;
}

//Small helper function to write a safe log field
static void writeLogField(FILE* file, const char* value) {
    for (const char* p = value; *p; p++) {
        if (*p == '\n' || *p == '\r' || *p == '|') {
            fputc(' ', file);
        } else {
            fputc(*p, file);
        }
    }
}

//Keep only recent log entries
static void trimLogFile(void) {
    FILE* input = fopen(LOG_PATH, "r");
    if (!input) {
        return;
    }

    char tempPath[512];
    int n = snprintf(tempPath, sizeof(tempPath), "%s.tmp", LOG_PATH);
    if (n < 0 || (size_t)n >= sizeof(tempPath)) {
        fclose(input);
        return;
    }

    FILE* output = fopen(tempPath, "w");
    if (!output) {
        fclose(input);
        return;
    }

    time_t cutoff = time(NULL) - (time_t)LOG_RETENTION_DAYS * 24 * 60 * 60;
    char line[1024];

    while (fgets(line, sizeof(line), input)) {
        char* end = NULL;
        long long loggedAt = strtoll(line, &end, 10);

        if (end == line || loggedAt >= (long long)cutoff) {
            fputs(line, output);
        }
    }

    fclose(input);
    fclose(output);

    if (rename(tempPath, LOG_PATH) != 0) {
        remove(tempPath);
    }
}
