#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

#include "../fetch_utils.h"

#include "../../libs/curly.h"
#include "../../libs/json/cJSON.h"

#include "../../sdk/ss_sdk.h"

#include "../../transform/transform.h"
#include "../../transform/weather/forecast_model.h"
#include "../../transform/weather/forecast_transform.h"

#define API_URL "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&hourly=shortwave_radiation,temperature_2m,cloud_cover&forecast_hours=26&current_weather=true"

double g_latitude = 52.52;
double g_longitude = 18.06;
// int g_panel_angle = 90;

int fetch_env_vars();
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
    char url[256];

    if (fetch_env_vars() < 0) {
        syslog(LOG_ERR, "Fetch API - Openmeteo Forecast - Unable to fetch environment variables.");
         goto done;
    }

    snprintf(
        url,
        sizeof(url),
        API_URL,
        g_latitude,
        g_longitude
    );

    if (fetch_from_url(url, &buffer, 30) < 0) {
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

int fetch_env_vars() {
    const char* blob_system = getenv("SUNSPOTS_SYSTEM");

    if (blob_system == NULL || blob_system[0] == '\0') {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - SUNSPOTS_SYSTEM missing.");
        return -1;
    }

    cJSON* root = cJSON_Parse(blob_system);
    if (!root) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Invalid SUNSPOTS_SYSTEM JSON.");
        return -1;
    }

    cJSON* system_obj = cJSON_GetObjectItemCaseSensitive(root, "system");
    if (!cJSON_IsObject(system_obj)) {
        system_obj = root;
    }

    cJSON* location_obj = cJSON_GetObjectItemCaseSensitive(system_obj, "location");
    if (!cJSON_IsObject(location_obj)) {
        location_obj = root;
    }

    cJSON* latitude_obj = cJSON_GetObjectItemCaseSensitive(location_obj, "latitude");
    cJSON* longitude_obj = cJSON_GetObjectItemCaseSensitive(location_obj, "longitude");

    if (!cJSON_IsNumber(latitude_obj)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Missing/invalid latitude.");
        cJSON_Delete(root);
        return -1;
    }

    if (!cJSON_IsNumber(longitude_obj)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - Missing/invalid longitude.");
        cJSON_Delete(root);
        return -1;
    }

    g_latitude = latitude_obj->valuedouble;
    g_longitude = longitude_obj->valuedouble;

    syslog(LOG_INFO, "Fetch API - Openmeteo Forecast - Config loaded: latitude=%.2f longitude=%.2f", g_latitude, g_longitude);

    cJSON_Delete(root);
    return 0;
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
    if (forecast_data == NULL || forecast_data->no_data_points <= 0 || forecast_data->weather_arr == NULL) {
        return -1;
    }
    const ss_sdk_data_kind data_type = SS_SDK_DATA_FORECAST;

    for (int i = 0; i < forecast_data->no_data_points; i++) {
        weather_data_t *point = &forecast_data->weather_arr[i];

        if (point->timestamp_unix <= 0) {
            continue;
        }

        if (point->has_temperature) {
            ss_sdk_record record;
            ss_sdk_status status = ss_sdk_record_make_f64(
                &record,
                SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
                point->temperature_c,
                (int64_t)point->timestamp_unix,
                SS_SDK_DATA_FORECAST);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - record_make temperature failed at i=%d status=%d", i, (int)status);
                return -1;
            }

            status = ss_sdk_db_write_record(&record);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - db_write temperature failed at i=%d status=%d", i, (int)status);
                return -1;
            }
        }

        if (point->has_cloud_cover) {
            ss_sdk_record record;
            ss_sdk_status status = ss_sdk_record_make_f64(
                &record,
                SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
                point->cloud_cover_percent,
                (int64_t)point->timestamp_unix,
                SS_SDK_DATA_FORECAST);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - record_make cloud_cover failed at i=%d status=%d", i, (int)status);
                return -1;
            }

            status = ss_sdk_db_write_record(&record);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - db_write cloud_cover failed at i=%d status=%d", i, (int)status);
                return -1;
            }
        }

        if (point->has_solar_radiation) {
            ss_sdk_record record;
            ss_sdk_status status = ss_sdk_record_make_f64(
                &record,
                SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
                point->solar_radiation_W_per_m2,
                (int64_t)point->timestamp_unix,
                SS_SDK_DATA_FORECAST);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - record_make shortwave_radiation failed at i=%d status=%d", i, (int)status);
                return -1;
            }

            status = ss_sdk_db_write_record(&record);
            if (status != SS_SDK_OK) {
                syslog(LOG_WARNING, "Fetch API - Openmeteo Forecast - db_write shortwave_radiation failed at i=%d status=%d", i, (int)status);
                return -1;
            }
        }
    }

    return 0;
}

void cleanup(void) {
    closelog();
}
