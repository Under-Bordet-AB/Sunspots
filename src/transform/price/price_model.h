#ifndef PRICE_MODEL_H
#define PRICE_MODEL_H

#include <time.h>
#include <stdbool.h>

/**
 * @brief Represents electricity price data.
 *
 * Stores a time series of prices in SEK per kWh along with timestamps
 * and a region identifier.
 */
typedef struct {
    time_t *timestamp_arr_unix;
    double *price_arr_SEK_per_kWh;
    int no_data_points;
    int region;
} price_data_t;


/**
 * @brief Initialize a price_data_t structure.
 *
 * Sets pointers to NULL and initializes metadata.
 *
 * @param data Pointer to the structure to initialize.
 * @param region Region identifier to assign.
 */
void price_data_init(price_data_t *data, int region);


/**
 * @brief Free resources used by a price_data_t structure.
 *
 * Frees allocated arrays and resets the structure.
 *
 * @param data Pointer to the structure to dispose.
 */
void price_data_dispose(price_data_t *data);

#endif