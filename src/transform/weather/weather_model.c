
#include "weather_model.h"

void weather_data_init(weather_data_t *data){
    data->timestamp_unix = 0;
    data->temperature_c = 0;
    data->cloud_cover_percent = 0;
    data->solar_radiation_W_per_m2 = 0;
    data->has_temperature = false;
    data->has_cloud_cover = false,
    data ->has_solar_radiation = false;
}