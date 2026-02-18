#ifndef PRICE_MODEL_H
#define PRICE_MODEL_H

#include <time.h>
#include <stdbool.h>

typedef struct {
    time_t *timestamp_unix;
    double *price_SEK_per_kWh;
    int no_data_points;
    int region;
} price_data_t;

void price_data_init(price_data_t *data, int region);
void price_data_dispose(price_data_t *data);

#endif