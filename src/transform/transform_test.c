
#include "weather_transform.h"
#include "forecast_transform.h"
#include <stdio.h>
#include <stdlib.h>
#include "cJSON.h"

cJSON *load_forecast_json(const char *filename) {
    FILE *file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long length = ftell(file);
    fseek(file, 0, SEEK_SET);

    if (length <= 0) {
        fclose(file);
        return NULL;
    }

    // Allocate buffer (+1 for null terminator)
    char *buffer = malloc(length + 1);
    if (!buffer) {
        fclose(file);
        return NULL;
    }

    // Read file
    size_t read_size = fread(buffer, 1, length, file);
    fclose(file);

    if (read_size != length) {
        free(buffer);
        return NULL;
    }

    buffer[length] = '\0';

    // Parse JSON
    cJSON *json = cJSON_Parse(buffer);
    free(buffer);

    if (!json) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr) {
            fprintf(stderr, "JSON parse error before: %s\n", error_ptr);
        }
        return NULL;
    }

    return json;
}

int main(){
    cJSON *forecast_obj = load_forecast_json("forecast.json");
    cJSON *solar_obj = load_forecast_json("solar_iso.json");

    forecast_data_t forecast;
    forecast_data_init(&forecast);

    transform_openmeteo_forecast(forecast_obj, &forecast);
    weather_data_t *w = &forecast.weather_arr[0];//forecast.no_data_points-1

    printf("time: %ld\r\nradiation: %f\r\ntemp: %f\r\ncloud: %f\r\n", w->timestamp_unix, w->solar_radiation_W_per_m2, w->temperature_c, w->cloud_cover_percent);
    printf("\r\n");
    forecast_data_dispose(&forecast);

    weather_data_t solar;
    weather_data_init(&solar);

    transform_openmeteo_solar(solar_obj, &solar);

    printf("time: %ld\r\nsolar: %f\r\n", solar.timestamp_unix, solar.solar_radiation_W_per_m2);
    
    cJSON_Delete(forecast_obj);
    cJSON_Delete(solar_obj);
    return 0;
}