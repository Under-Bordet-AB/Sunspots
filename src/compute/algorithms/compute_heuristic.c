#include "compute_heuristic.h"
#include <stddef.h>

#define NOCT_C 45.0 // Nominal Operating Cell Temperature (C)
#define CELL_REF_TEMP_C 25.0 // Cell reference temperature (C)
#define IRRADIANCE_REF_WM2 400.0 // Reference irradiance (W/m^2) (for normalization)
#define TEMP_COEFF_PER_C -0.004 // Power temperature coefficient (-0.4% power per +1C above 25C)

#define PRICE_HIGH 2.0 // Example high price for normalization
#define PRICE_LOW 0.1 // Example low price for normalization

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

static double normalize_range(double value, double low, double high) {
    if (high <= low) {
        return 0.0;
    }
    return clamp01((value - low) / (high - low));
}

int compute_heuristic(compute_data_t* data_in, result_t* result_out) {
    if (!data_in || !result_out) {
        return -1;
    }

    int slots = data_in->horizon_len;
    if (slots <= 0 || slots > SERIES_LEN) {
        return -1;
    }

    double pv_cap_norm[slots];
    for (int time_slot = 0; time_slot < slots; time_slot++) {
        // Read weather + price signal for this slot
        double irradiance_wm2 = data_in->irradiance[time_slot];
        double cloudiness_percent = data_in->cloudiness[time_slot];
        double ambient_temp_c = data_in->temperature[time_slot];
        double price_kwh = data_in->price_kwh[time_slot];

        // Normalize cloudiness variable
        double cloudiness_factor = clamp01(1.0 - (cloudiness_percent / 100.0));

        // Multiply irradiance with cloudiness factor
        double effective_irradiance_wm2 = irradiance_wm2 * cloudiness_factor;
        if (effective_irradiance_wm2 < 0.0) {
            effective_irradiance_wm2 = 0.0;
        }

        // Energy loss caused by temperature
        double cell_temp_c = ambient_temp_c + ((NOCT_C - 20.0) / 800.0) * effective_irradiance_wm2;
        double temp_derate = 1.0 + TEMP_COEFF_PER_C * (cell_temp_c - CELL_REF_TEMP_C);
        if (temp_derate < 0.0) {
            temp_derate = 0.0;
        }

        // Final normalized PV availability [0,1] after weather + system losses
        pv_cap_norm[time_slot] = clamp01((effective_irradiance_wm2 / IRRADIANCE_REF_WM2) * temp_derate);
    }

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        // Output controls for this slot (normalized 0..1)
        double price_kwh = data_in->price_kwh[time_slot];
        double pv = pv_cap_norm[time_slot];

        double direct_use = 0.0;
        double charge_battery = 0.0;
        double sell_excess = 0.0;
        double buy_electricity = 0.0;

        double price_signal = normalize_range(price_kwh, PRICE_LOW, PRICE_HIGH);
        double cheapness = 1.0 - price_signal;
        double expensiveness = price_signal;

        double solar_budget = pv;
        double grid_budget = 1.0 - solar_budget;

        direct_use = solar_budget * (0.5 + 0.5 * expensiveness);

        double solar_left = solar_budget - direct_use;
        charge_battery = solar_left * cheapness;
        sell_excess = solar_left * expensiveness;

        buy_electricity = grid_budget * cheapness;

        // Store clamped outputs in result buffers
        result_out->buy_electricity[time_slot] = clamp01(buy_electricity);
        result_out->direct_use[time_slot] = clamp01(direct_use);
        result_out->charge_battery[time_slot] = clamp01(charge_battery);
        result_out->sell_excess[time_slot] = clamp01(sell_excess);
    }

    return 0;
}
