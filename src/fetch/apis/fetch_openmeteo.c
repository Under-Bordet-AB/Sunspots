#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>

#include "../fetch_utils.h"

#include "../../libs/curly.h"
#include "../../libs/json/cJSON.h"

#include "../../transform/transform.h"
#include "../../transform/weather/weather_model.h"
#include "../../transform/weather/weather_transform.h"

#define API_URL "https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.41&current=temperature_2m,cloud_cover"

int normalize_data(char* raw_in, weather_data_t** out);
int save_to_database(weather_data_t* price_data);
void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_OPENMETEO", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Fetch API - Openmeteo - Starting...");

    char* buffer = NULL;

    if (fetch_from_url(API_URL, &buffer, 30) < 0) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't fetch from API.");
        exit(EXIT_FAILURE);
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Buffer is NULL.");
        exit(EXIT_FAILURE);
    }

    weather_data_t* weather_data = NULL;
    if (normalize_data(buffer, &weather_data) < 0) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't normalize data.");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    if ((save_to_database(weather_data) < 0)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't save data to database.");
        free(weather_data);
        free(buffer);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Fetch API - Openmeteo - Data successfully normalized and saved!");
    
    if (weather_data != NULL) free(weather_data);
    if (buffer != NULL) free(buffer);

    return 0;
}

int normalize_data(char* raw_in, weather_data_t** out) {
    if (!raw_in || !out) {
        return -1;
    }

    *out = NULL;

    weather_data_t* data = malloc(sizeof(weather_data_t));
    if (!data) {
        return -1;
    }

    weather_data_init(data);

    cJSON* json_obj = cJSON_Parse(raw_in);
    if (!json_obj) {
        free(data);
        return -1;
    }

    if (transform_openmeteo_weather(json_obj, data) != TRANSFORM_OK) {
        cJSON_Delete(json_obj);
        free(data);
        return -1;
    }

    if (transform_openmeteo_solar(json_obj, data) != TRANSFORM_OK) {
        cJSON_Delete(json_obj);
        free(data);
        return -1;
    }

    cJSON_Delete(json_obj);
    *out = data;
    
    return 0;
}

int save_to_database(weather_data_t* price_data) {
    return 0;
}

void cleanup(void) {
    closelog();
}
