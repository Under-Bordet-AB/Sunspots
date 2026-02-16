#ifndef PRICE_TRANSFORM_H
#define PRICE_TRANSFORM_H

#include "transform.h"
#include "price_model.h"
#include "cJSON.h"

transform_status_t transform_elprisetjustnu_price(const cJSON *input, price_data_t *out);


#endif