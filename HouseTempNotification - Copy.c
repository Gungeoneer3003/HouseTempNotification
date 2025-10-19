#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define API "api"//Obscured for privacy
#define user "userAPI"//Obscured for privacy
#define houseLink "houseLink" //Obscured for privacy

//Global timespec var
struct timespec ts = {300, 0};

//Prototypes
size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
void houseSense(int* house, int* oa, int* power);   //Modify this so it passes three referenced var's
int checkVariance(int initial, int final);
char* checkDifference(int diff, int power);
void turnOffFans();
void send_pushover_notification(const char* message);

struct Memory {
    char* response;
    size_t size;
};

int main(void) {
    char* message;
    int house, oa, power, initialDiff = 0, finalDiff = 0;
    int varianceFlag; //Boolean

    while (1) {
        houseSense(&house, &oa, &power);
        finalDiff = oa - house;

        varianceFlag = checkVariance(initialDiff, finalDiff);
        if (varianceFlag) {
            message = checkDifference(finalDiff, power);
            if (strcmp(message,"STOP"))
                send_pushover_notification(message);
            else
                printf("Successful skip\n"); 
        }
        else {
            printf("No variance found\n");
        }

        initialDiff = finalDiff;
        nanosleep(&ts, NULL);
    }

    return 0;
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

        curl_easy_setopt(curl, CURLOPT_URL, houseLink);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
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
        int x = ts.tv_sec;
        ts.tv_sec = 1;
        nanosleep(&ts, NULL);
        ts.tv_sec = x;
    }

    printf("Failed to read valid data from house sensor after 10 attempts.\n");
    return;
}

//Both of these are differences, but comparison prevents repeat msg's
int checkVariance(int initial, int final) {
    if (initial <= 0 && final > 0)       //It was cooler out, but it got hotter
        return 1;
    else if (initial >= 0 && final < 0)  //It was hotter out, but it got cooler
        return 1;
    else
        return 0; //No direction change, so no msg
}

//Difference is understood as out - in, so (-) var would be cool out
char* checkDifference(int diff, int power) {
    if (power) { //This case will be for if the house fan is running
        if (diff > 0) {
            turnOffFans();                  //Turn off the fan
            return "Hotter out than in";    //Close windows
        }
        else
            return "STOP";  //Windows closed already
    }
    else { //This case will be for if the house fan is not running
        if (diff < 0)
            return "Cooler out than in";    //Open windows
        else
            return "STOP";  //Windows open already
    }
}

//Function to turn off the fans
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

        curl_easy_setopt(curl, CURLOPT_URL, "commandToTurnOff"); //Obscured for privacy
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
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
        int x = ts.tv_sec;
        ts.tv_sec = 1;
        nanosleep(&ts, NULL);
        ts.tv_sec = x;
    }

    return;
}

//Function for sending a notification
void send_pushover_notification(const char* message) {
    CURL* curl;
    CURLcode res;

    char postfields[512];
    snprintf(postfields, sizeof(postfields), "token=%s&user=%s&message=%s", API, user, message);

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
