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
#include "../../transform/weather/weather_model.h"
#include "../../transform/weather/weather_transform.h"

#define API_URL "https://api.open-meteo.com/v1/forecast?latitude=%.2f&longitude=%.2f&current=temperature_2m,cloud_cover,shortwave_radiation"

double g_latitude = 52.52;
double g_longitude = 18.06;
// int g_panel_angle = 90;

int fetch_env_vars();
int normalize_data(char* raw_in, weather_data_t** out);
int save_to_database(weather_data_t* price_data);
void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_OPENMETEO", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Fetch API - Openmeteo - Starting...");

    int rc = EXIT_FAILURE;
    char* buffer = NULL;
    weather_data_t* weather_data = NULL;
    char url[256];

    if (fetch_env_vars() < 0) {
        syslog(LOG_ERR, "Fetch API - Openmeteo - Unable to fetch environment variables.");
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
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't fetch from API.");
        goto done;
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Buffer is NULL.");
        goto done;
    }

    if (normalize_data(buffer, &weather_data) < 0) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't normalize data.");
        goto done;
    }

    if ((save_to_database(weather_data) < 0)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Couldn't save data to database.");
        goto done;
    }

    syslog(LOG_INFO, "Fetch API - Openmeteo - Data successfully normalized and saved!");
    rc = EXIT_SUCCESS;
    
done:
    if (weather_data != NULL) free(weather_data);
    if (buffer != NULL) free(buffer);

    return rc;
}

int fetch_env_vars() {
    const char* blob_system = getenv("SUNSPOTS_SYSTEM");

    if (blob_system == NULL || blob_system[0] == '\0') {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - SUNSPOTS_SYSTEM missing.");
        return -1;
    }

    cJSON* root = cJSON_Parse(blob_system);
    if (!root) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Invalid SUNSPOTS_SYSTEM JSON.");
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
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Missing/invalid latitude.");
        cJSON_Delete(root);
        return -1;
    }

    if (!cJSON_IsNumber(longitude_obj)) {
        syslog(LOG_WARNING, "Fetch API - Openmeteo - Missing/invalid longitude.");
        cJSON_Delete(root);
        return -1;
    }

    g_latitude = latitude_obj->valuedouble;
    g_longitude = longitude_obj->valuedouble;

    syslog(LOG_INFO, "Fetch API - Openmeteo - Config loaded: latitude=%.2f longitude=%.2f", g_latitude, g_longitude);

    cJSON_Delete(root);
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

    // if (transform_openmeteo_solar(json_obj, data) != TRANSFORM_OK) {
    //     cJSON_Delete(json_obj);
    //     free(data);
    //     return -1;
    // }

    if (transform_openmeteo_weather(json_obj, data) != TRANSFORM_OK) {
        cJSON_Delete(json_obj);
        free(data);
        return -1;
    }

    cJSON_Delete(json_obj);
    *out = data;
    
    return 0;
}

int save_to_database(weather_data_t* weather_data) {
    if (weather_data == NULL) {
        return -1;
    }

    if (weather_data->timestamp_unix <= 0) {
        return -1;
    }

    if (weather_data->has_temperature) {
        ss_sdk_record record;
        ss_sdk_status status = ss_sdk_record_make_f64(
            &record,
            SS_METRIC_WEATHER_TEMPERATURE_AIR_2M_C,
            weather_data->temperature_c,
            (int64_t)weather_data->timestamp_unix,
            SS_SDK_DATA_OBSERVATION);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - record_make temperature failed status=%d", (int)status);
            return -1;
        }

        status = ss_sdk_db_write_record(&record);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - db_write temperature failed status=%d", (int)status);
            return -1;
        }
    }

    if (weather_data->has_cloud_cover) {
        ss_sdk_record record;
        ss_sdk_status status = ss_sdk_record_make_f64(
            &record,
            SS_METRIC_WEATHER_CLOUD_COVER_TOTAL_PCT,
            weather_data->cloud_cover_percent,
            (int64_t)weather_data->timestamp_unix,
            SS_SDK_DATA_OBSERVATION);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - record_make cloud_cover failed status=%d", (int)status);
            return -1;
        }

        status = ss_sdk_db_write_record(&record);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "Fetch API - Openmeteo - db_write cloud_cover failed status=%d", (int)status);
            return -1;
        }
    }

    // if (weather_data->has_solar_radiation) {
    //     ss_sdk_record record;
    //     ss_sdk_status status = ss_sdk_record_make_f64(
    //         &record,
    //         SS_METRIC_WEATHER_RADIATION_SHORTWAVE_WM2,
    //         weather_data->solar_radiation_W_per_m2,
    //         (int64_t)weather_data->timestamp_unix,
    //         SS_SDK_DATA_OBSERVATION);
    //     if (status != SS_SDK_OK) {
    //         syslog(LOG_WARNING, "Fetch API - Openmeteo - record_make shortwave_radiation failed status=%d", (int)status);
    //         return -1;
    //     }

    //     status = ss_sdk_db_write_record(&record);
    //     if (status != SS_SDK_OK) {
    //         syslog(LOG_WARNING, "Fetch API - Openmeteo - db_write shortwave_radiation failed status=%d", (int)status);
    //         return -1;
    //     }
    // }

    return 0;
}

void cleanup(void) {
    closelog();
}
