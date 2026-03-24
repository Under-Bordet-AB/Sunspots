#ifndef FORECAST_MODEL_H
#define FORECAST_MODEL_H

#include "weather_model.h"

/**
 * @brief Container for forecast weather data.
 *
 * Holds a dynamically allocated array of weather data points
 * along with the number of elements stored.
 */
typedef struct {
    weather_data_t *weather_arr;
    int no_data_points;
} forecast_data_t;

/**
 * @brief Initializes a forecast_data_t structure.
 *
 * Sets the internal array pointer to NULL and the number of data points to 0.
 *
 * @param data Pointer to the forecast_data_t structure to initialize.
 */
void forecast_data_init(forecast_data_t *data);


/**
 * @brief Releases resources used by a forecast_data_t structure.
 *
 * Frees the internal weather data array if allocated and resets
 * the structure to a safe empty state.
 *
 * @param data Pointer to the forecast_data_t structure to dispose.
 */
void forecast_data_dispose(forecast_data_t *data);

#endif