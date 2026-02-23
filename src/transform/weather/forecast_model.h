#ifndef FORECAST_MODEL_H
#define FORECAST_MODEL_H

#include "weather_model.h"

typedef struct {
    weather_data_t *weather_arr;
    int no_data_points;
} forecast_data_t;

void forecast_data_init(forecast_data_t *data);
void forecast_data_dispose(forecast_data_t *data);

#endif