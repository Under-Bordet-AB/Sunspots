#ifndef WEATHER_TRANSFORM_H
#define WEATHER_TRANSFORM_H

#include "transform.h"
#include "weather_model.h"
#include "cJSON.h"


/**
 * @brief Convert Open-Meteo current weather JSON to internal model.
 *
 * Parses the "current" section of the input JSON and fills @p out
 * with available weather values.
 *
 * @param input JSON object containing current weather data.
 * @param out Output structure to populate (should be initialized).
 *
 * @return transform_status_t Status code indicating success or failure.
 *
 * @retval TRANSFORM_OK Success.
 * @retval TRANSFORM_INVALID_INPUT Input is NULL or not an object.
 * @retval TRANSFORM_MISSING_FIELD Required fields are missing.
 * @retval TRANSFORM_BAD_FORMAT Invalid field types or values.
 * @retval TRANSFORM_UNSUPPORTED Unsupported units or formats.
 *
 * @note Fields in @p out are only set if present in the input JSON.
 */
transform_status_t transform_openmeteo_weather(const cJSON *input, weather_data_t *out);


#endif