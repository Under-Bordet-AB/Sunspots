
#include "transform.h"
#include "price_model.h"
#include "cJSON.h"
#include "json_utils.h"
#include "unit_utils.h"
#include <string.h>
#include <stdlib.h>

transform_status_t transform_elprisetjustnu_price(const cJSON *input, price_data_t *out){
    JSON_REQUIRE(cJSON_IsArray(input), TRANSFORM_INVALID_INPUT);

    int no_data_points = cJSON_GetArraySize(input);
    if (no_data_points != 96) { return TRANSFORM_BAD_FORMAT; }

    out->timestamp_unix = (time_t *)malloc(sizeof(time_t)*no_data_points);
    out->price_SEK_per_kWh = (double *)malloc(sizeof(double)*no_data_points);
    out->no_data_points = no_data_points;

    for (int i = 0; i < no_data_points; i++){
        cJSON *item = cJSON_GetArrayItem(input, i);

        cJSON *time = cJSON_GetObjectItem(item, "time_start");
        cJSON *price = cJSON_GetObjectItem(item, "SEK_per_kWh");

        if (!time || !price) {
            out->timestamp_unix[i] = 0;
            out->price_SEK_per_kWh[i] = -1;
            continue;
        } else {
            out->timestamp_unix[i] = iso8601_to_unix(time->valuestring);
            out->price_SEK_per_kWh[i] = price->valuedouble;
        }
    }

    return TRANSFORM_OK;
}