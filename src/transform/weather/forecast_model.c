
#include "forecast_model.h"
#include "stdlib.h"

void forecast_data_init(forecast_data_t *data){
    if (!data) { return; }
    data->weather_arr = NULL;
    data->no_data_points = 0;
}

void forecast_data_dispose(forecast_data_t *data){
    if (!data) { return; }
    if (data->weather_arr) { free(data->weather_arr); data->weather_arr = NULL; }
    data->no_data_points = 0;
}