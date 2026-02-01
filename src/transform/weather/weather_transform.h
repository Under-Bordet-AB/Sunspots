#ifndef WEATHER_TRANSFORM_H
#define WEATHER_TRANSFORM_H

#include "transform.h"
#include "weather_model.h"
#include "cJSON.h"

transform_status_t transform_openmeteo_weather(const cJSON *input, weather_data_t *out);
transform_status_t transform_openmeteo_solar(const cJSON *input, weather_data_t *out);


#endif