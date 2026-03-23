#ifndef PRICE_TRANSFORM_H
#define PRICE_TRANSFORM_H

#include "transform.h"
#include "price_model.h"
#include "cJSON.h"


/**
 * @brief Convert Elpriset Just Nu JSON price data into internal format.
 *
 * Expects a JSON array of 96 entries, each containing:
 * - "time_start" (ISO8601 string)
 * - "SEK_per_kWh" (numeric price)
 *
 * The function converts timestamps to Unix time and stores results
 * in @p out.
 *
 * @param input JSON array of price data.
 * @param out Output structure to populate (must be initialized).
 *
 * @return transform_status_t Status code indicating success or failure.
 *
 * @retval TRANSFORM_OK Success.
 * @retval TRANSFORM_INVALID_INPUT Input is NULL or not a JSON array.
 * @retval TRANSFORM_BAD_FORMAT Invalid array size or malformed entries.
 *
 * @note Allocates memory for timestamp and price arrays.
 *       Caller must free using price_data_dispose().
 */
transform_status_t transform_elprisetjustnu_price(const cJSON *input, price_data_t *out);


#endif