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
#include "../../transform/weather/forecast_model.h"
#include "../../transform/weather/forecast_transform.h"

#define API_URL "https://api.open-meteo.com/v1/forecast?latitude=52.52&longitude=13.41&hourly=shortwave_radiation,temperature_2m,cloud_cover"

int normalize_data(char* raw_in, forecast_data_t** out);
int save_to_database(forecast_data_t* price_data);
void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_OPENMETEO_FORECAST", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Fetch API - Openmeteo Forecast - Starting...");

    int rc = EXIT_FAILURE;
    char* buffer = NULL;
    forecast_data_t* forecast_data = NULL;

    if (fetch_from_url(API_URL, &buffer, 30) < 0) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Couldn't fetch from API.");
        goto done;
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Buffer is NULL.");
        goto done;
    }

    if (normalize_data(buffer, &forecast_data) < 0) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Couldn't normalize data.");
        goto done;
    }

    if ((save_to_database(forecast_data) < 0)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Couldn't save data to database.");
        goto done;
    }

    syslog(LOG_INFO, "Fetch API - Openmeteo Forecast - Data successfully normalized and saved!");
    rc = EXIT_SUCCESS;
    
done:
    if (forecast_data != NULL) {
        forecast_data_dispose(forecast_data);
        free(forecast_data);
    }
    if (buffer != NULL) free(buffer);

    return rc;
}

int normalize_data(char* raw_in, forecast_data_t** out) {
    if (!raw_in || !out) {
        return -1;
    }

    *out = NULL;

    forecast_data_t* data = malloc(sizeof(forecast_data_t));
    if (!data) {
        return -1;
    }

    forecast_data_init(data);

    cJSON* json_obj = cJSON_Parse(raw_in);
    if (!json_obj) {
        forecast_data_dispose(data);
        free(data);
        return -1;
    }

    if (transform_openmeteo_forecast(json_obj, data) != TRANSFORM_OK) {
        cJSON_Delete(json_obj);
        forecast_data_dispose(data);
        free(data);
        return -1;
    }

    cJSON_Delete(json_obj);
    *out = data;
    
    return 0;
}

int save_to_database(forecast_data_t* forecast_data) {
    return 0;
}

void cleanup(void) {
    closelog();
}
