#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <syslog.h>
#include <signal.h>
#include <pthread.h>
#include <time.h>
#include <stdint.h>

#include "../fetch_utils.h"

#include "../../libs/json/cJSON.h"

#include "../../sdk/ss_sdk.h"

#include "../../transform/transform.h"
#include "../../transform/price/price_model.h"
#include "../../transform/price/price_transform.h"

#define API_URL "https://www.elprisetjustnu.se/api/v1/prices/%04d/%02d-%02d_%s.json"

char g_spotprice_area[8] = "SE3";

int fetch_env_vars();
int normalize_data(char* raw_in, price_data_t** out);
int save_to_database(price_data_t* price_data);
void cleanup(void);

static int fetch_and_store_for_day(const struct tm* day, int day_offset);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_ELPRISJUSTNU", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Starting...");
    
    int rc = EXIT_FAILURE;

    if (fetch_env_vars() < 0) {
        syslog(LOG_WARNING, "Failed to set environment variables, using default=%s", g_spotprice_area);
    } else {
        syslog(LOG_INFO, "Fetching spot-price for area=%s", g_spotprice_area);
    }

    time_t now = time(NULL);
    struct tm base_tm = {0};
    struct tm* base_ptr = localtime(&now);

    if (!base_ptr) {
        syslog(LOG_WARNING, "localtime failed.");
        goto done;
    }

    base_tm = *base_ptr;

    for (int day_offset = 0; day_offset <= 1; day_offset++) {
        struct tm day_tm = base_tm;
        day_tm.tm_mday += day_offset;

        if (mktime(&day_tm) == (time_t)-1) {
            syslog(LOG_WARNING, "mktime failed for day_offset=%d.", day_offset);
            goto done;
        }

        if (fetch_and_store_for_day(&day_tm, day_offset) < 0) {
            goto done;
        }
    }

    rc = EXIT_SUCCESS;

done:
    return rc;
}

static int fetch_and_store_for_day(const struct tm* day, int day_offset) {
    char* buffer = NULL;
    price_data_t* price_data = NULL;
    char url[128];
    int rc = -1;

    snprintf(
        url,
        sizeof(url),
        API_URL,
        day->tm_year + 1900,
        day->tm_mon + 1,
        day->tm_mday,
        g_spotprice_area
    );

    if (fetch_from_url(url, &buffer, 30) < 0) {
        syslog(LOG_WARNING, "Couldn't fetch from API for day_offset=%d.", day_offset);
        goto done;
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Buffer is NULL for day_offset=%d.", day_offset);
        goto done;
    }

    if (normalize_data(buffer, &price_data) < 0) {
        syslog(LOG_WARNING, "Couldn't normalize data for day_offset=%d.", day_offset);
        goto done;
    }

    if (save_to_database(price_data) < 0) {
        syslog(LOG_WARNING, "Couldn't save data to database for day_offset=%d.", day_offset);
        goto done;
    }

    syslog(LOG_INFO, "Data successfully normalized and saved for day_offset=%d.", day_offset);
    rc = 0;

done:
    if (price_data != NULL) {
        price_data_dispose(price_data);
        free(price_data);
    }
    if (buffer != NULL) {
        free(buffer);
    }

    return rc;
}

int fetch_env_vars() {
    const char* blob_system = getenv("SUNSPOTS_SYSTEM");

    if (blob_system == NULL || blob_system[0] == '\0') {
        syslog(LOG_WARNING, "SUNSPOTS_SYSTEM missing.");
        return -1;
    }

    cJSON* root = cJSON_Parse(blob_system);
    if (!root) {
        syslog(LOG_WARNING, "Invalid SUNSPOTS_SYSTEM JSON.");
        return -1;
    }

    cJSON* system_obj = cJSON_GetObjectItemCaseSensitive(root, "system");
    if (!cJSON_IsObject(system_obj)) {
        system_obj = root;
    }

    cJSON* location_obj = cJSON_GetObjectItemCaseSensitive(system_obj, "location");
    cJSON* area_obj = cJSON_IsObject(location_obj) ? cJSON_GetObjectItemCaseSensitive(location_obj, "elprisomrade") : NULL;

    if (cJSON_IsString(area_obj) && area_obj->valuestring != NULL) {
        size_t n = strlen(area_obj->valuestring);
        if (n > 0 && n < sizeof(g_spotprice_area)) {
            strncpy(g_spotprice_area, area_obj->valuestring, sizeof(g_spotprice_area) - 1);
            g_spotprice_area[sizeof(g_spotprice_area) - 1] = '\0';
            syslog(LOG_INFO, "Using area=%s from SUNSPOTS_SYSTEM.", g_spotprice_area);
        } else {
            syslog(LOG_WARNING, "Invalid elprisomrade length.");
            cJSON_Delete(root);
            return -1;
        }
    } else {
        syslog(LOG_WARNING, "elprisomrade missing.");
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    return 0;
}

int normalize_data(char* raw_in, price_data_t** out) {
    if (!raw_in || !out) {
        return -1;
    }

    *out = NULL;

    price_data_t* data = malloc(sizeof(price_data_t));
    if (!data) {
        return -1;
    }

    price_data_init(data, 3);

    cJSON* json_obj = cJSON_Parse(raw_in);
    if (!json_obj) {
        price_data_dispose(data);
        free(data);
        return -1;
    }

    if (transform_elprisetjustnu_price(json_obj, data) != TRANSFORM_OK) {
        cJSON_Delete(json_obj);
        price_data_dispose(data);
        free(data);
        return -1;
    }

    cJSON_Delete(json_obj);
    *out = data;

    return 0;
}

int save_to_database(price_data_t* price_data) {
    if (price_data == NULL) {
        return -1;
    }

    if (price_data->no_data_points <= 0) {
        return -1;
    }

    if (price_data->timestamp_arr_unix == NULL || price_data->price_arr_SEK_per_kWh == NULL) {
        return -1;
    }

    for (int i = 0; i < price_data->no_data_points; i++) {
        ss_sdk_record record;
        ss_sdk_status status = ss_sdk_record_make_f64(
            &record,
            SS_METRIC_ENERGY_PRICE_SPOT_SEK_KWH,
            price_data->price_arr_SEK_per_kWh[i],
            (int64_t)price_data->timestamp_arr_unix[i],
            SS_SDK_DATA_FORECAST);

        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "record_make failed at i=%d status=%d", i, (int)status);
            return -1;
        }

        status = ss_sdk_db_write_record(&record);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "db_write failed at i=%d status=%d", i, (int)status);
            return -1;
        }
    }

    return 0;
}

void cleanup(void) {
    closelog();
}
