#ifndef WEATHER_MODEL_H
#define WEATHER_MODEL_H

#include <time.h>
#include <stdbool.h>


/**
 * @brief Represents a single weather data point.
 *
 * Contains timestamped weather measurements. Each value has a corresponding
 * flag indicating whether it is present.
 */
typedef struct {
    time_t timestamp_unix;
    double temperature_c;
    bool has_temperature;
    double cloud_cover_percent;
    bool has_cloud_cover;
    double solar_radiation_W_per_m2;
    bool has_solar_radiation;
} weather_data_t;


/**
 * @brief Initialize a weather_data_t structure.
 *
 * Sets all values to defaults and marks all fields as not present.
 *
 * @param data Pointer to the structure to initialize.
 */
void weather_data_init(weather_data_t *data);

#endif