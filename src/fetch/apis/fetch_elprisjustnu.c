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
#include "../../transform/price/price_model.h"
#include "../../transform/price/price_transform.h"

#define API_URL "https://www.elprisetjustnu.se/api/v1/prices/%04d/%02d-%02d_SE3.json"

int normalize_data(char* raw_in, price_data_t** out);
int save_to_database(price_data_t* price_data);
static int fetch_and_store_for_date(const struct tm* date_parts);
void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_ELPRISJUSTNU", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Fetch API - Elprisjustnu - Starting...");

    int rc = EXIT_FAILURE;
    time_t now = time(NULL);
    struct tm t = {0};
    struct tm tomorrow = {0};
    struct tm *t_ptr = localtime(&now);

    if (!t_ptr) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - localtime failed.");
        goto done;
    }

    t = *t_ptr;
    tomorrow = t;
    tomorrow.tm_mday += 1;
    if (mktime(&tomorrow) == (time_t)-1) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - mktime failed for tomorrow.");
        goto done;
    }

    if (fetch_and_store_for_date(&t) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Failed for today.");
        goto done;
    }

    if (fetch_and_store_for_date(&tomorrow) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Failed for tomorrow.");
        goto done;
    }

    syslog(LOG_INFO, "Fetch API - Elprisjustnu - Data successfully normalized and saved for today and tomorrow!");
    rc = EXIT_SUCCESS;

done:
    return rc;
}

static int fetch_and_store_for_date(const struct tm* date_parts) {
    if (!date_parts) {
        return -1;
    }

    char* buffer = NULL;
    price_data_t* price_data = NULL;
    char url[128];

    snprintf(url, sizeof(url), API_URL, date_parts->tm_year + 1900, date_parts->tm_mon + 1, date_parts->tm_mday);

    if (fetch_from_url(url, &buffer, 30) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't fetch from API.");
        goto cleanup_and_fail;
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Buffer is NULL.");
        goto cleanup_and_fail;
    }

    if (normalize_data(buffer, &price_data) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't normalize data.");
        goto cleanup_and_fail;
    }

    if ((save_to_database(price_data) < 0)) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't save data to database.");
        goto cleanup_and_fail;
    }

    if (price_data != NULL) {
        price_data_dispose(price_data);
        free(price_data);
    }
    if (buffer != NULL) free(buffer);

    return 0;

cleanup_and_fail:
    if (price_data != NULL) {
        price_data_dispose(price_data);
        free(price_data);
    }
    if (buffer != NULL) free(buffer);
    return -1;
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
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - record_make failed at i=%d status=%d", i, (int)status);
            return -1;
        }

        status = ss_sdk_db_write_record(&record);
        if (status != SS_SDK_OK) {
            syslog(LOG_WARNING, "Fetch API - Elprisjustnu - db_write failed at i=%d status=%d", i, (int)status);
            return -1;
        }
    }

    return 0;
}

void cleanup(void) {
    closelog();
}
