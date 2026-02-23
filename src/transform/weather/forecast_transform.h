#ifndef FORECAST_TRANSFORM_H
#define FORECAST_TRANSFORM_H

#include "transform.h"
#include "forecast_model.h"
#include "cJSON.h"

transform_status_t transform_openmeteo_forecast(const cJSON *input, forecast_data_t *out);

#endif