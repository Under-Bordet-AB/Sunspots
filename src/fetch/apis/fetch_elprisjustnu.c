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
#include "../../transform/price/price_model.h"
#include "../../transform/price/price_transform.h"

#define API_URL "https://www.elprisetjustnu.se/api/v1/prices/%04d/%02d-%02d_SE3.json"

int normalize_data(char* raw_in, price_data_t** out);
int save_to_database(price_data_t* price_data);
void cleanup(void);

int main() {
    atexit(cleanup);

    openlog("SUNSPOTS_FETCH_ELPRISJUSTNU", LOG_PID, LOG_DAEMON);

    syslog(LOG_INFO, "Fetch API - Elprisjustnu - Starting...");

    char* buffer = NULL;

    char url[128];
    time_t now = time(NULL);
    struct tm *t = localtime(&now);

    snprintf(url, sizeof(url), API_URL, t->tm_year + 1900, t->tm_mon + 1, t->tm_mday);

    if (fetch_from_url(url, &buffer, 30) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't fetch from API.");
        exit(EXIT_FAILURE);
    }

    if (!buffer) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Buffer is NULL.");
        exit(EXIT_FAILURE);
    }

    price_data_t* price_data = NULL;
    if (normalize_data(buffer, &price_data) < 0) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't normalize data.");
        free(buffer);
        exit(EXIT_FAILURE);
    }

    if ((save_to_database(price_data) < 0)) {
        syslog(LOG_WARNING, "Fetch API - Elprisjustnu - Couldn't save data to database.");
        price_data_dispose(price_data);
        free(buffer);
        exit(EXIT_FAILURE);
    }

    syslog(LOG_INFO, "Fetch API - Elprisjustnu - Data successfully normalized and saved!");
    
    if (price_data != NULL) price_data_dispose(price_data);
    if (buffer != NULL) free(buffer);

    exit(EXIT_SUCCESS);
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
    for (int i = 0; i < price_data->no_data_points; i++) {
        // save each datapoint with its respective timestamp (in its timeslot database wise)
    }

    return 0;
}

void cleanup(void) {
    closelog();
}
