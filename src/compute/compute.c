#include "compute.h"

int calculate_simple(data_t* data, result_t** result) {
    if (data == NULL || result == NULL) {
        return -1;
    }

    *result = malloc(sizeof(result_t));
    memset(*result, 0, sizeof(result_t));

    double cloudiness = data->cloudiness;
    if (cloudiness > 1.0) {
        cloudiness /= 100.0;
    }
    if (cloudiness < 0.0) {
        cloudiness = 0.0;
    } else if (cloudiness > 1.0) {
        cloudiness = 1.0;
    }

    double battery_pct = data->battery_charge;
    if (battery_pct <= 1.0) {
        battery_pct *= 100.0;
    }
    if (battery_pct < 0.0) {
        battery_pct = 0.0;
    } else if (battery_pct > 100.0) {
        battery_pct = 100.0;
    }

    double effective_solar = data->irradiance * (1.0 - cloudiness);

    const double solar_use_threshold = 200.0;
    const double battery_low_threshold = 20.0;
    const double battery_high_threshold = 90.0;
    const double battery_sell_threshold = 60.0;
    const double price_low_threshold = 0.5;
    const double price_high_threshold = 1.5;

    int solar_available = (effective_solar >= solar_use_threshold);
    int price_low = (data->spot_price <= price_low_threshold);
    int price_high = (data->spot_price >= price_high_threshold);
    int battery_low = (battery_pct <= battery_low_threshold);
    int battery_high = (battery_pct >= battery_high_threshold);

    (*result)->use_solar = solar_available ? 1 : 0;

    if (solar_available) {
        if (!battery_high) {
            (*result)->charge_battery = 1;
        }

        if (battery_high && price_high) {
            (*result)->sell_excess = 1;
        }

        (*result)->buy_electricity = 0;
    } else {
        if (price_low) {
            (*result)->buy_electricity = 1;
            if (!battery_high) {
                (*result)->charge_battery = 1;
            }
        } else if (!battery_low) {
            (*result)->charge_battery = 2;
            if (price_high && battery_pct >= battery_sell_threshold) {
                (*result)->sell_excess = 1;
            }
        } else {
            (*result)->buy_electricity = 1;
        }
    }

    return 0;
}