#ifndef COMPUTE_MODEL_H
#define COMPUTE_MODEL_H

#define SERIES_LEN 96

typedef struct compute_data_t {
    double irradiance[SERIES_LEN];
    double cloudiness[SERIES_LEN];
    double temperature[SERIES_LEN];
    double price_kwh[SERIES_LEN];

    int horizon_len;
} compute_data_t;

typedef struct result_t {
    double buy_electricity[SERIES_LEN];
    double direct_use[SERIES_LEN];
    double charge_battery[SERIES_LEN];
    double sell_excess[SERIES_LEN];
} result_t;

#endif