
#include "price_model.h"
#include "stdlib.h"

void price_data_init(price_data_t *data, int region){
    if (!data) { return; }
    data->timestamp_unix = NULL;
    data->price_SEK_per_kWh = NULL;
    data->no_data_points = 0;
    data->region = region;
}

void price_data_dispose(price_data_t *data){
    if (!data) { return; }
    if (data->timestamp_unix) { free(data->timestamp_unix); }
    if (data->price_SEK_per_kWh) { free(data->price_SEK_per_kWh); }
    data->no_data_points = 0;
    data->region = -1;
}