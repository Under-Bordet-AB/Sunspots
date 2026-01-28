
#include "transform.h"
#include "weather_model.h"
#include "cJSON.h"
#include "json_utils.h"
#include <string.h>
#include <stdio.h>

transform_status_t transform_weather_openmeteo(const cJSON *input, weather_data_t *out){
    JSON_REQUIRE(cJSON_IsObject(input), TRANSFORM_INVALID_INPUT);

    cJSON *units_obj;
    cJSON *current_obj;

    JSON_GET_REQUIRED(input, "current_units",
                      units_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(input, "current",
                      current_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);

    /* --- unit fields --- */
    const char *time_unit;
    const char *temperature_unit;
    const char *windspeed_unit;
    const char *precipitation_unit;

    JSON_GET_STRING(units_obj, "time",
                    time_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "temperature_2m",
                    temperature_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "wind_speed_10m",
                    windspeed_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "precipitation",
                    precipitation_unit, TRANSFORM_BAD_FORMAT);

    /* --- value fields --- */
    cJSON *time_obj;
    cJSON *temperature_obj;
    cJSON *windspeed_obj;
    cJSON *precipitation_obj;

    JSON_GET_REQUIRED(current_obj, "time",
                      time_obj, cJSON_IsString,
                      TRANSFORM_BAD_FORMAT);

    JSON_GET_REQUIRED(current_obj, "temperature_2m",
                      temperature_obj, cJSON_IsNumber,
                      TRANSFORM_BAD_FORMAT);

    JSON_GET_REQUIRED(current_obj, "wind_speed_10m",
                      windspeed_obj, cJSON_IsNumber,
                      TRANSFORM_BAD_FORMAT);

    JSON_GET_REQUIRED(current_obj, "precipitation",
                      precipitation_obj, cJSON_IsNumber,
                      TRANSFORM_BAD_FORMAT);

    const char *time_value = time_obj->valuestring;
    float temperature_value = (float)temperature_obj->valuedouble;
    float windspeed_value   = (float)windspeed_obj->valuedouble;
    float precipitation_value = (float)precipitation_obj->valuedouble;

    /* --- validate units --- */
    if (strcmp(time_unit, "iso8601") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(temperature_unit, "°C") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(windspeed_unit, "km/h") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(precipitation_unit, "mm") != 0) { return TRANSFORM_UNSUPPORTED; }
    
    /* --- parse timestamp --- */
    struct tm t = {};
    int count = sscanf(time_value, "%d-%d-%dT%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min);
    if (count != 5){
        return TRANSFORM_BAD_FORMAT;
    }
    t.tm_year -= 1900;
    t.tm_mon -= 1;

    out->timestamp = timegm(&t);
    out->temperature_c = temperature_value;
    out->windspeed_mps = windspeed_value/3.6f;
    out->precipitation_mm = precipitation_value;

    return TRANSFORM_OK;
}