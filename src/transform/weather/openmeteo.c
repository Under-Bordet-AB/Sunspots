
#include "transform.h"
#include "weather_model.h"
#include "jansson.h"
#include <string.h>

transform_status_t transform_weather_openmeteo(const json_t *input, weather_data_t *out){
    if (!json_is_object(input)) { return TRANSFORM_INVALID_INPUT; }

    json_t *units_obj = json_object_get(input, "current_units");
    json_t *current_obj = json_object_get(input, "current");
    
    if (!units_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!current_obj) { return TRANSFORM_MISSING_FIELD; }

    json_t *time_unit_obj = json_object_get(units_obj, "time");
    json_t *temperature_unit_obj = json_object_get(units_obj, "temperature_2m");
    json_t *windspeed_unit_obj = json_object_get(units_obj, "wind_speed_10m");
    json_t *precipitation_unit_obj = json_object_get(units_obj, "precipitation");

    json_t *time_obj = json_object_get(current_obj, "time");
    json_t *temperature_obj = json_object_get(current_obj, "temperature_2m");
    json_t *windspeed_obj = json_object_get(current_obj, "wind_speed_10m");
    json_t *precipitation_obj = json_object_get(current_obj, "precipitation");

    if (!time_unit_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!temperature_unit_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!windspeed_unit_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!precipitation_unit_obj) { return TRANSFORM_MISSING_FIELD; }
    
    if (!json_is_string(time_unit_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_string(temperature_unit_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_string(windspeed_unit_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_string(precipitation_unit_obj)) { return TRANSFORM_BAD_FORMAT; }

    if (!time_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!temperature_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!windspeed_obj) { return TRANSFORM_MISSING_FIELD; }
    if (!precipitation_obj) { return TRANSFORM_MISSING_FIELD; }
    
    if (!json_is_string(time_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_real(temperature_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_real(windspeed_obj)) { return TRANSFORM_BAD_FORMAT; }
    if (!json_is_real(precipitation_obj)) { return TRANSFORM_BAD_FORMAT; }

    const char *time_unit = json_string_value(time_unit_obj);
    const char *temperature_unit = json_string_value(temperature_unit_obj);
    const char *windspeed_unit = json_string_value(windspeed_unit_obj);
    const char *precipitation_unit = json_string_value(precipitation_unit_obj);

    const char *time_value = json_string_value(time_obj);
    float temperature_value = json_real_value(temperature_obj);
    float windspeed_value = json_real_value(windspeed_obj);
    float precipitation_value = json_real_value(precipitation_obj);

    if (strcmp(time_unit, "iso8601") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(temperature_unit, "°C") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(windspeed_unit, "km/h") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(precipitation_unit, "mm") != 0) { return TRANSFORM_UNSUPPORTED; }

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