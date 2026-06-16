#include <curl/curl.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static const char* envFile = "keys.env";

static const char* API = NULL;
static const char* USER_KEY = NULL;
static const char* HOUSE_LINK = NULL;
static const char* CGI_PART = NULL;
static const char* POWER_PART = NULL;

char* CLOSE = "Close the windows";
char* OPEN = "Open the windows";
char* STOP = "STOP";

char* STATE1 = " (Out:";
char* STATE2 = " In:";

char CGILINK[128];
char SHUTOFFLINK[128];

struct Memory {
    char* response;
    size_t size;
};

void loadDotenv(const char* filename);
char* trimWhitespace(char* str);
int initConfig(void);
int compareStates(int initial, int final);
char* checkDifference(int diff, int power, int house);
size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp);
void houseSense(int* house, int* oa, int* power);
void turnOffFans();
void sendPushover(const char* message);


//Loop function for sensing and messaging
int main(void) {
    printf("Starting House Temperature Notification System\n");
    char message[128];
    char state[32];
    char* status;
    int house, oa, power, initialDiff = 0, finalDiff = 0;
    int varianceFlag; //Boolean

    if (!initConfig()) {
        fprintf(stderr, "Failed to initialize configuration\n");
        return EXIT_FAILURE;
    }

    //Setup command links
    strcpy(CGILINK, HOUSE_LINK);
    strcpy(SHUTOFFLINK, HOUSE_LINK);

    strcat(CGILINK, CGI_PART);
    strcat(SHUTOFFLINK, POWER_PART);

    //Get the first state
    houseSense(&house, &oa, &power);
    initialDiff = oa - house;
    

    //Begin loop
    while (1) {
        sleep(300);

        //Get the next state
        houseSense(&house, &oa, &power);
        finalDiff = oa - house;

        //Check for a variance
        varianceFlag = compareStates(initialDiff, finalDiff);
        
        //Do something depending on the message
        if (varianceFlag) {
            status = checkDifference(finalDiff, power, house);

            if (strcmp(status,"STOP") != 0) {
                strcpy(message, status);

                sprintf(state, "(Out:%d In:%d)", oa, house);
                strcat(message, state);

                sendPushover(message);
            }
            else
                printf("Successful skip for %d %d %d\n", house, oa, power); 
        }
        else
            printf("No variance found between %d %d\n", initialDiff, finalDiff);

        initialDiff = finalDiff;
    }

    return 0;
}

//Both of these are differences, but comparison prevents repeat msg's
int compareStates(int initial, int final) {
    if (initial <= 0 && final > 0)       //It was cooler out, but it got hotter
        return 1;
    else if (initial >= 0 && final < 0)  //It was hotter out, but it got cooler
        return 1;
    else
        return 0; //No direction change, so no msg
}

char* checkDifference(int diff, int power, int house) {
    if (power) { //This case will be for if the house fan is running
        if (diff > 0) {
            turnOffFans();                  //Turn off the fan
            return CLOSE;    //Close windows
        }
        else
            return STOP;  //Windows closed already
    }
    else { //This case will be for if the house fan is not running
        if (diff < 0)
            return OPEN;    //Open windows
        else
            return STOP;  //Windows open already
    }
}

size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    struct Memory* mem = (struct Memory*)userp;

    char* ptr = realloc(mem->response, mem->size + total_size + 1);
    if (!ptr) return 0;

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, total_size);
    mem->size += total_size;
    mem->response[mem->size] = '\0';

    return total_size;
}

//Function for sending a notification
void sendPushover(const char* message) {
    CURL* curl;
    CURLcode res;
    char postfields[512];
    
    snprintf(postfields, sizeof(postfields), "token=%s&user=%s&message=%s", API, USER_KEY, message);

    curl = curl_easy_init();
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://api.pushover.net/1/messages.json");
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postfields);

        res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));

        curl_easy_cleanup(curl);
    }
}

void turnOffFans() {
    CURL* curl;
    CURLcode res;
    struct Memory chunk;

    for (int attempt = 0; attempt < 10; attempt++) {
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "Failed to initialize curl\n");
            return; //No solution to this, must exit
        }

        chunk.response = NULL;
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, SHUTOFFLINK); //Obscured for privacy
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            printf("\tSuccessfully turned off fans\n");
            if (chunk.response) free(chunk.response);
            curl_easy_cleanup(curl);
            return;
        }
        else {
            fprintf(stderr, "curl_easy_perform() failed (Attempt %d of 10): %s\n", attempt + 1, curl_easy_strerror(res));
        }

        // Cleanup for the next attempt
        if (chunk.response)
            free(chunk.response);
        curl_easy_cleanup(curl);

        // Small delay before retrying to avoid hammering the server
        sleep(1);
    }

    return;
}

void houseSense(int* house, int* oa, int* power) {
    CURL* curl;
    CURLcode res;
    struct Memory chunk;

    // Try up to 10 times, skip after that
    for (int attempt = 0; attempt < 10; ++attempt) {
        curl = curl_easy_init();
        if (!curl) {
            fprintf(stderr, "Failed to initialize curl\n");
            return; //No solution to this, must exit
        }

        chunk.response = NULL;
        chunk.size = 0;

        curl_easy_setopt(curl, CURLOPT_URL, CGILINK);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) {
            char* key = "\"inside\":";
            char* start = strstr(chunk.response, key);
            if (start) {
                start += strlen(key);
                *house = atoi(start);
            }

            key = "\"oa\":";
            start = strstr(chunk.response, key);
            if (start) {
                start += strlen(key);
                *oa = atoi(start);
            }

            key = "\"power\":";
            start = strstr(chunk.response, key);
            if (start) {
                start += strlen(key);
                *power = atoi(start);
            }

            //Success, return
            free(chunk.response);
            curl_easy_cleanup(curl);
            return;
        }
        else {
            fprintf(stderr, "curl_easy_perform() failed (Attempt %d of 10): %s\n", attempt + 1, curl_easy_strerror(res));
        }

        // Cleanup for the next attempt
        if (chunk.response)
            free(chunk.response);
        curl_easy_cleanup(curl);

        // Small delay before retrying to avoid hammering the server
        sleep(1);
    }

    printf("Failed to read valid data from house sensor after 10 attempts.\n");
    return;
}

char* trimWhitespace(char* str) {
    if (!str) return NULL;
    while (isspace((unsigned char)*str)) str++;
    if (*str == '\0') return str;

    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = '\0';
    return str;
}

void loadDotenv(const char* filename) {
    FILE* file = fopen(filename, "r");
    if (!file) return;

    char line[512];
    while (fgets(line, sizeof(line), file)) {
        char* trimmed = trimWhitespace(line);
        if (*trimmed == '\0' || *trimmed == '#')
            continue;

        char* equals = strchr(trimmed, '=');
        if (!equals) continue;

        *equals = '\0';
        char* key = trimWhitespace(trimmed);
        char* value = trimWhitespace(equals + 1);

        if (*value == '"' && value[strlen(value)-1] == '"') {
            value[strlen(value)-1] = '\0';
            value++;
        }

        if (*key && *value)
            setenv(key, value, 1);
    }

    fclose(file);
}

int initConfig(void) {
    loadDotenv(envFile);

    API = getenv("API");
    USER_KEY = getenv("USER_KEY");
    HOUSE_LINK = getenv("HOUSE_LINK");
    CGI_PART = getenv("CGI_PART");
    POWER_PART = getenv("POWER_PART");

    if (!API || !USER_KEY || !HOUSE_LINK || !CGI_PART || !POWER_PART) {
        fprintf(stderr, "Missing configuration in %s or environment: API=%s USER_KEY=%s HOUSE_LINK=%s CGI_PART=%s POWER_PART=%s\n",
                envFile,
                API ? "ok" : "missing",
                USER_KEY ? "ok" : "missing",
                HOUSE_LINK ? "ok" : "missing",
                CGI_PART ? "ok" : "missing",
                POWER_PART ? "ok" : "missing");
        return 0;
    }

    return 1;
}

size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    struct Memory* mem = (struct Memory*)userp;

    char* ptr = realloc(mem->response, mem->size + total_size + 1);
    if (!ptr) return 0;

    mem->response = ptr;
    memcpy(&(mem->response[mem->size]), contents, total_size);
    mem->size += total_size;
    mem->response[mem->size] = '\0';

    return total_size;
}