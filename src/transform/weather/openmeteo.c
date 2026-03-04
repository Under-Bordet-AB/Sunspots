
#include "transform.h"
#include "weather_model.h"
#include "forecast_model.h"
#include "cJSON.h"
#include "json_utils.h"
#include "unit_utils.h"
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

transform_status_t transform_openmeteo_weather(const cJSON *input, weather_data_t *out){
    JSON_REQUIRE(cJSON_IsObject(input), TRANSFORM_INVALID_INPUT);

    cJSON *units_obj;
    cJSON *current_obj;

    JSON_GET_REQUIRED(input, "current_units",
                      units_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(input, "current",
                      current_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);
                      
    const char *time_unit;
    const char *temperature_unit;
    const char *cloud_cover_unit;

    JSON_GET_STRING(units_obj, "time",
                    time_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "temperature_2m",
                    temperature_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "cloud_cover",
                    cloud_cover_unit, TRANSFORM_BAD_FORMAT);

    if (strcmp(time_unit, "iso8601") != 0 && strcmp(time_unit, "unixtime") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(temperature_unit, "°C") != 0 && strcmp(temperature_unit, "°F") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(cloud_cover_unit, "%") != 0) { return TRANSFORM_UNSUPPORTED; }


    cJSON *time_obj;
    cJSON *temperature_obj;
    cJSON *cloud_cover_obj;

    if (strcmp(time_unit, "iso8601") == 0){
        JSON_GET_REQUIRED(current_obj, "time",
                        time_obj, cJSON_IsString,
                        TRANSFORM_BAD_FORMAT);
    } else {
        JSON_GET_REQUIRED(current_obj, "time",
                        time_obj, cJSON_IsNumber,
                        TRANSFORM_BAD_FORMAT);
    }

    JSON_GET_REQUIRED(current_obj, "temperature_2m",
                      temperature_obj, cJSON_IsNumber,
                      TRANSFORM_BAD_FORMAT);

    JSON_GET_REQUIRED(current_obj, "cloud_cover",
                      cloud_cover_obj, cJSON_IsNumber,
                      TRANSFORM_BAD_FORMAT);
    
    float temperature_value = (float)temperature_obj->valuedouble;
    float cloud_cover_value   = (float)cloud_cover_obj->valuedouble;

    int unix_time;
    if (strcmp(time_unit, "iso8601") == 0){
        const char *time_value = time_obj->valuestring;
        unix_time = iso8601_to_unix(time_value);
        if (unix_time == -1) { return TRANSFORM_BAD_FORMAT; }
    } else {
        unix_time = time_obj->valueint;
    }

    if (strcmp(temperature_unit, "°F") == 0){
        temperature_value = fahrenheit_to_celsius(temperature_value);
    }

    out->timestamp_unix = (unix_time > out->timestamp_unix) ? unix_time : out->timestamp_unix;
    out->temperature_c = temperature_value;
    out->has_temperature = true;
    out->cloud_cover_percent = cloud_cover_value;
    out->has_cloud_cover = true;

    return TRANSFORM_OK;
}

transform_status_t transform_openmeteo_solar(const cJSON *input, weather_data_t *out){
    JSON_REQUIRE(cJSON_IsObject(input), TRANSFORM_INVALID_INPUT);

    cJSON *hourly_units_obj;
    cJSON *hourly_obj;

    JSON_GET_REQUIRED(input, "hourly_units", hourly_units_obj, cJSON_IsObject, TRANSFORM_MISSING_FIELD);
    JSON_GET_REQUIRED(input, "hourly", hourly_obj, cJSON_IsObject, TRANSFORM_MISSING_FIELD);

    const char *time_unit;
    const char *radiation_unit;

    JSON_GET_STRING(hourly_units_obj, "time", time_unit, TRANSFORM_MISSING_FIELD);
    JSON_GET_STRING(hourly_units_obj, "shortwave_radiation", radiation_unit, TRANSFORM_MISSING_FIELD);

    if (strcmp(time_unit, "iso8601") != 0 && strcmp(time_unit, "unixtime") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(radiation_unit, "W/m²") != 0) { return TRANSFORM_UNSUPPORTED; }

    cJSON *time_array;
    cJSON *radiation_array;

    JSON_GET_REQUIRED(hourly_obj, "time", time_array, cJSON_IsArray, TRANSFORM_MISSING_FIELD);
    JSON_GET_REQUIRED(hourly_obj, "shortwave_radiation", radiation_array, cJSON_IsArray, TRANSFORM_MISSING_FIELD);
    
    int num_entries = cJSON_GetArraySize(time_array);
    if (num_entries != 24 && num_entries != cJSON_GetArraySize(radiation_array)){
        return TRANSFORM_BAD_FORMAT;
    }

    bool is_unix_time = (strcmp(time_unit, "unixtime") == 0);
    const int64_t now_utc = (int64_t)time(NULL);
    double selected_radiation = -1.0;
    int selected_time = 0;
    int have_selected = 0;
    int fallback_time = 0;
    double fallback_radiation = -1.0;
    int have_fallback = 0;

    for (int i = 0; i < num_entries; i++){
        int unix_time;
        if (is_unix_time){
            unix_time = cJSON_GetArrayItem(time_array, i)->valueint;
        } else {
            const char *iso_time = cJSON_GetArrayItem(time_array, i)->valuestring;
            unix_time = iso8601_to_unix(iso_time);
            if (unix_time == -1){
                continue;
            }
        }

        cJSON *radiation_obj = cJSON_GetArrayItem(radiation_array, i);
        if (!cJSON_IsNumber(radiation_obj)){
            continue;
        }
        double radiation_value = radiation_obj->valuedouble;

        if (unix_time <= now_utc) {
            if (!have_selected || unix_time > selected_time) {
                selected_time = unix_time;
                selected_radiation = radiation_value;
                have_selected = 1;
            }
        } else if (!have_selected) {
            if (!have_fallback || unix_time < fallback_time) {
                fallback_time = unix_time;
                fallback_radiation = radiation_value;
                have_fallback = 1;
            }
        }
    }

    if (!have_selected && have_fallback) {
        selected_time = fallback_time;
        selected_radiation = fallback_radiation;
        have_selected = 1;
    }

    if (!have_selected || selected_radiation < 0.0){
        return TRANSFORM_MISSING_FIELD;
    }

    /* Keep current-weather timestamp when already set, otherwise use selected solar slot. */
    if (out->timestamp_unix <= 0) {
        out->timestamp_unix = selected_time;
    }
    out->solar_radiation_W_per_m2 = selected_radiation;
    out->has_solar_radiation = true;

    return TRANSFORM_OK;
}

transform_status_t transform_openmeteo_forecast(const cJSON *input, forecast_data_t *out){
    JSON_REQUIRE(cJSON_IsObject(input), TRANSFORM_INVALID_INPUT);

    cJSON *units_obj;
    cJSON *hourly_obj;

    JSON_GET_REQUIRED(input, "hourly_units",
                      units_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(input, "hourly",
                      hourly_obj, cJSON_IsObject,
                      TRANSFORM_MISSING_FIELD);

    const char *time_unit;
    const char *radiation_unit;
    const char *temperature_unit;
    const char *cloud_cover_unit;

    JSON_GET_STRING(units_obj, "time",
                    time_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "shortwave_radiation",
                    radiation_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "temperature_2m",
                    temperature_unit, TRANSFORM_BAD_FORMAT);

    JSON_GET_STRING(units_obj, "cloud_cover",
                    cloud_cover_unit, TRANSFORM_BAD_FORMAT);

                    
    if (strcmp(time_unit, "iso8601") != 0 && strcmp(time_unit, "unixtime") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(radiation_unit, "W/m²") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(temperature_unit, "°C") != 0 && strcmp(temperature_unit, "°F") != 0) { return TRANSFORM_UNSUPPORTED; }
    if (strcmp(cloud_cover_unit, "%") != 0) { return TRANSFORM_UNSUPPORTED; }

    bool is_unix_time = (strcmp(time_unit, "unixtime") == 0);

    cJSON *time_array;
    cJSON *radiation_array;
    cJSON *temperature_array;
    cJSON *cloud_cover_array;

    JSON_GET_REQUIRED(hourly_obj, "time",
                      time_array, cJSON_IsArray,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(hourly_obj, "shortwave_radiation",
                      radiation_array, cJSON_IsArray,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(hourly_obj, "temperature_2m",
                      temperature_array, cJSON_IsArray,
                      TRANSFORM_MISSING_FIELD);

    JSON_GET_REQUIRED(hourly_obj, "cloud_cover",
                      cloud_cover_array, cJSON_IsArray,
                      TRANSFORM_MISSING_FIELD);

    int num_entries = cJSON_GetArraySize(time_array);
    if (cJSON_GetArraySize(radiation_array) != num_entries ||
        cJSON_GetArraySize(temperature_array) != num_entries ||
        cJSON_GetArraySize(cloud_cover_array) != num_entries){
        return TRANSFORM_BAD_FORMAT;
    }

    out->weather_arr = (weather_data_t *)malloc(num_entries * sizeof(weather_data_t));
    out->no_data_points = num_entries;

    for (int i = 0; i < num_entries; i++){
        weather_data_t *item = &out->weather_arr[i];
        weather_data_init(item);
        int unix_time;
        if (is_unix_time){
            unix_time = cJSON_GetArrayItem(time_array, i)->valueint;
        } else {
            const char *iso_time = cJSON_GetArrayItem(time_array, i)->valuestring;
            unix_time = iso8601_to_unix(iso_time);
            if (unix_time == -1){
                continue;
            }
        }
        item->timestamp_unix = unix_time;

        cJSON *radiation = cJSON_GetArrayItem(radiation_array, i);
        if (cJSON_IsNumber(radiation)){
            item->solar_radiation_W_per_m2 = radiation->valuedouble;
            item->has_solar_radiation = true;
        }

        cJSON *temperature = cJSON_GetArrayItem(temperature_array, i);
        if (cJSON_IsNumber(temperature)){
            item->temperature_c = temperature->valuedouble;
            item->has_temperature = true;
        }

        cJSON *cloud_cover = cJSON_GetArrayItem(cloud_cover_array, i);
        if (cJSON_IsNumber(cloud_cover)){
            item->cloud_cover_percent = cloud_cover->valuedouble;
            item->has_cloud_cover = true;
        }
    }

    return TRANSFORM_OK;
}
