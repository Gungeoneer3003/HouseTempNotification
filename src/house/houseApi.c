#include "houseApi.h"
#include <stdio.h>
#include "httpClient.h"
#include "jsonUtils.h"
#include "portable.h"
#include "settings.h"

//Read the sensor data from the house API, with retries
int houseReadSensor(const AppConfig* config, SensorReading* reading) {
    if (!config || !reading) {
        return 0;
    }

    //Try multiple times to read from the sensor
    for (int attempt = 0; attempt < SENSOR_RETRY_COUNT; ++attempt) {
        HttpResponse response;

        //Make the HTTP GET request to the CGI URL
        if (httpGet(config->cgi_url, "Sensor read", &response) && response.body) {
            int house = 0;
            int outside_air = 0;
            int attic = 0;
            int power = 0;

            //Parse the JSON response to extract the sensor values
            if (jsonParseInt(response.body, "inside", &house) &&
                jsonParseInt(response.body, "oa", &outside_air) &&
                jsonParseInt(response.body, "power", &power) &&
                jsonParseInt(response.body, "attic", &attic)) {
                
                reading->house = house;
                reading->outside_air = outside_air;
                reading->attic = attic;
                reading->power = power;

                //Clean up
                httpResponseFree(&response);
                return 1;
            }

            //Check if anything was missing
            fprintf(stderr, "Sensor response was missing inside/oa/attic/power fields: %s\n",
                    response.body);
        } else {
            //Log the failure
            fprintf(stderr, "Sensor read failed attempt %d of %d\n",
                    attempt + 1, SENSOR_RETRY_COUNT);
        }

        httpResponseFree(&response);
        portableSleepSeconds(1);
    }

    fprintf(stderr, "Failed to read valid data from house sensor after %d attempts\n",
            SENSOR_RETRY_COUNT);
    return 0;
}

//Try to turn off the fans
//This should happen at the same time as closing the windows
int houseTurnOffFans(const AppConfig* config) {
    if (!config) {
        return 0;
    }

    for (int attempt = 0; attempt < SENSOR_RETRY_COUNT; attempt++) {
        HttpResponse response;

        if (httpGet(config->shutoff_url, "Fan shutoff request", &response)) {
            httpResponseFree(&response);
            printf("Successfully turned off fans\n");
            return 1;
        }

        httpResponseFree(&response);
        fprintf(stderr, "Fan shutoff failed attempt %d of %d\n",
                attempt + 1, SENSOR_RETRY_COUNT);
        portableSleepSeconds(1);
    }

    return 0;
}

int pushoverSendMessage(const AppConfig* config, const char* message) {
    if (!config || !message) {
        return 0;
    }

    HttpFormField fields[] = {
        {"token", config->api_token},
        {"user", config->user_key},
        {"message", message},
    };

    HttpResponse response;
    int ok = httpPostForm("https://api.pushover.net/1/messages.json",
                          fields,
                          sizeof(fields) / sizeof(fields[0]),
                          "Pushover request",
                          &response);
    if (ok) {
        int status = 0;
        if (!response.body || !jsonParseInt(response.body, "status", &status) || status != 1) {
            fprintf(stderr, "Pushover response did not confirm success\n");
            ok = 0;
        }
    }

    httpResponseFree(&response);
    return ok;
}
