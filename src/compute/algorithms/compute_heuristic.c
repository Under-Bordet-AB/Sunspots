#include "compute_heuristic.h"

#include <syslog.h>

#define NOCT_C 45.0 // Nominal Operating Cell Temperature (C)
#define CELL_REF_TEMP_C 25.0 // Cell reference temperature (C)
#define IRRADIANCE_REF_WM2 1000.0 // Reference irradiance (W/m^2) (for normalization)
#define TEMP_COEFF_PER_C -0.004 // Power temperature coefficient (-0.4% power per +1C above 25C)
#define PERFORMANCE_RATIO 0.85 // System performance ratio

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

    // Find the lowest and highest price out of all slots
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

    // Split current horizon prices into low/mid/high bands for simple rule decisions
    double price_span = max_price - min_price;
    double low_price_threshold = min_price + (0.33 * price_span);
    double high_price_threshold = min_price + (0.66 * price_span);

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
        double pv_cap_norm = clamp01((effective_irradiance_wm2 / IRRADIANCE_REF_WM2) * PERFORMANCE_RATIO * temp_derate);

        // Output controls for this slot (normalized 0..1)
        double direct_use = 0.0;
        double charge_battery = 0.0;
        double sell_excess = 0.0;
        double buy_electricity = 0.0;

        // High price: prioritize direct usage and selling over charging
        if (price_kwh >= high_price_threshold) {
            direct_use = 1.0 * pv_cap_norm;
            sell_excess = 0.5 * pv_cap_norm;
            charge_battery = 0.0;
            buy_electricity = (pv_cap_norm < 0.10) ? 0.15 : 0.0;
        // Low price: prioritize charging for later
        } else if (price_kwh <= low_price_threshold) {
            direct_use = 0.6 * pv_cap_norm;
            charge_battery = 0.4 * pv_cap_norm;
            sell_excess = 0.0;
            buy_electricity = (pv_cap_norm < 0.20) ? (1.0 / pv_cap_norm) - 0.5 : 0.0;
        // Mid price: keep a balanced strategy
        } else {
            direct_use = 0.8 * pv_cap_norm;
            charge_battery = 0.2 * pv_cap_norm;
            sell_excess = 0.0;
            buy_electricity = (pv_cap_norm < 0.10) ? 0.10 : 0.0;
        }

        direct_use = (1.0 * pv_cap_norm) + price_kwh;
        sell_excess = price_kwh;
        charge_battery = direct_use / price_kwh;
        buy_electricity = 1.0 - (pv_cap_norm + price_kwh);


        // Store clamped outputs in result buffers
        result_out->buy_electricity[time_slot] = clamp01(buy_electricity);
        result_out->direct_use[time_slot] = clamp01(direct_use);
        result_out->charge_battery[time_slot] = clamp01(charge_battery);
        result_out->sell_excess[time_slot] = clamp01(sell_excess);

        int logging = 0;
        if (logging == 1) {
            syslog(LOG_INFO, "time_slot: %d", time_slot);
            syslog(LOG_INFO, "---------------------------");
            syslog(LOG_INFO, "irradiance_wm2: %.3f", irradiance_wm2);
            syslog(LOG_INFO, "cloudiness_percent: %.3f", cloudiness_percent);
            syslog(LOG_INFO, "ambient_temp_c: %.3f", ambient_temp_c);
            syslog(LOG_INFO, "price_kwh: %.3f", price_kwh);
            syslog(LOG_INFO, "---------------------------");
            syslog(LOG_INFO, "coudiness_factor: %.3f", cloudiness_factor);
            syslog(LOG_INFO, "effective_irradiance_wm2: %.3f", effective_irradiance_wm2);
            syslog(LOG_INFO, "cell_temp_c: %.3f", cell_temp_c);
            syslog(LOG_INFO, "temp_derate: %.3f", temp_derate);
            syslog(LOG_INFO, "pv_cap_norm: %.3f", pv_cap_norm);
            syslog(LOG_INFO, "---------------------------");
            syslog(LOG_INFO, "direct_use: %.3f", direct_use);
            syslog(LOG_INFO, "charge_battery: %.3f", charge_battery);
            syslog(LOG_INFO, "sell_excess: %.3f", sell_excess);
            syslog(LOG_INFO, "buy_electricity: %.3f", buy_electricity);
            syslog(LOG_INFO, ">");
            syslog(LOG_INFO, ">");
        }
    }

    return 0;
}
