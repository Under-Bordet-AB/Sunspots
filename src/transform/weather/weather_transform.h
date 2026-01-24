#ifndef WEATHER_TRANSFORM_H
#define WEATHER_TRANSFORM_H

#include "transform.h"
#include "weather_model.h"
#include "jansson.h"

transform_status_t transform_weather_openmeteo(const json_t *input, weather_data_t *out);


#endif