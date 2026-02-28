#include "compute_heuristic.h"

#define NOCT_C 45.0
#define CELL_REF_TEMP_C 25.0
#define IRRADIANCE_REF_WM2 1000.0
#define TEMP_COEFF_PER_C -0.004
#define PERFORMANCE_RATIO 0.85

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

int compute_heuristic(const compute_data_t* data_in, result_t* result_out) {
    if (!data_in || !result_out) {
        return -1;
    }

    int slots = data_in->horizon_len;
    if (slots <= 0 || slots > SERIES_LEN) {
        return -1;
    }

    double min_price = data_in->price_kwh[0];
    double max_price = data_in->price_kwh[0];

    for (int time_slot = 1; time_slot < slots; time_slot++) {
        double price = data_in->price_kwh[time_slot];
        if (price < min_price) {
            min_price = price;
        }
        if (price > max_price) {
            max_price = price;
        }
    }

    double price_span = max_price - min_price;
    double low_price_threshold = min_price + (0.33 * price_span);
    double high_price_threshold = min_price + (0.66 * price_span);

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        double irradiance_wm2 = data_in->irradiance[time_slot];
        double cloudiness_percent = data_in->cloudiness[time_slot];
        double ambient_temp_c = data_in->temperature[time_slot];
        double price_kwh = data_in->price_kwh[time_slot];

        double cloudiness_factor = clamp01(1.0 - (cloudiness_percent / 100.0));
        double effective_irradiance_wm2 = irradiance_wm2 * cloudiness_factor;
        if (effective_irradiance_wm2 < 0.0) {
            effective_irradiance_wm2 = 0.0;
        }

        double cell_temp_c = ambient_temp_c + ((NOCT_C - 20.0) / 800.0) * effective_irradiance_wm2;
        double temp_derate = 1.0 + TEMP_COEFF_PER_C * (cell_temp_c - CELL_REF_TEMP_C);
        if (temp_derate < 0.0) {
            temp_derate = 0.0;
        }

        double pv_cap_norm = clamp01((effective_irradiance_wm2 / IRRADIANCE_REF_WM2) * PERFORMANCE_RATIO * temp_derate);

        double direct_use = 0.0;
        double charge_battery = 0.0;
        double sell_excess = 0.0;
        double buy_electricity = 0.0;

        if (price_kwh >= high_price_threshold) {
            direct_use = 0.7 * pv_cap_norm;
            sell_excess = 0.3 * pv_cap_norm;
            charge_battery = 0.0;
            buy_electricity = (pv_cap_norm < 0.10) ? 0.15 : 0.0;
        } else if (price_kwh <= low_price_threshold) {
            direct_use = 0.6 * pv_cap_norm;
            charge_battery = 0.4 * pv_cap_norm;
            sell_excess = 0.0;
            buy_electricity = (pv_cap_norm < 0.20) ? 0.35 : 0.0;
        } else {
            direct_use = 0.8 * pv_cap_norm;
            charge_battery = 0.2 * pv_cap_norm;
            sell_excess = 0.0;
            buy_electricity = (pv_cap_norm < 0.10) ? 0.10 : 0.0;
        }

        result_out->buy_electricity[time_slot] = clamp01(buy_electricity);
        result_out->direct_use[time_slot] = clamp01(direct_use);
        result_out->charge_battery[time_slot] = clamp01(charge_battery);
        result_out->sell_excess[time_slot] = clamp01(sell_excess);
    }

    return 0;
}
