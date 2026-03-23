#ifndef FORECAST_TRANSFORM_H
#define FORECAST_TRANSFORM_H

#include "transform.h"
#include "forecast_model.h"
#include "cJSON.h"

/**
 * @brief Convert Open-Meteo hourly forecast JSON to internal model.
 *
 * Parses the "hourly" section of the input JSON and fills @p out with
 * an array of weather_data_t.
 *
 * @param input JSON object containing forecast data.
 * @param out Output structure to populate (should be initialized).
 *
 * @return transform_status_t Status code indicating success or failure.
 *
 * @retval TRANSFORM_OK Success.
 * @retval TRANSFORM_INVALID_INPUT Input is NULL or not an object.
 * @retval TRANSFORM_MISSING_FIELD Required fields are missing.
 * @retval TRANSFORM_BAD_FORMAT Invalid field types or array mismatch.
 * @retval TRANSFORM_UNSUPPORTED Unsupported units.
 *
 * @note Memory for @p out->weather_arr is allocated internally and must
 *       be freed with forecast_data_dispose().
 */
transform_status_t transform_openmeteo_forecast(const cJSON *input, forecast_data_t *out);

#endif