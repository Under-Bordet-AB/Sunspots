#ifndef COMPUTE_H
#define COMPUTE_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct data_t {
    double irradiance;
    double cloudiness;
    double temperature;

    double spot_price;

    double battery_charge;
} data_t;

typedef struct result_t {
    char* time;

    int buy_electricity; // 0 = Don't buy, 1 = Buy
    int use_solar; // 0 = Don't use solar, 1 = Use solar
    int charge_battery; // 0 = Don't charge battery, 1 = Charge battery, 2 = Discharge battery
    int sell_excess; // 0 = Don't sell excess, 1 = Sell excess
} result_t;

int load_data(data_t** data);
int calculate_result(data_t* data, result_t* result);
int store_result(result_t* result);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        return EXIT_FAILURE;
    }

    while (1) {
        //load_data();
        //calculate_result();
        //store_result();

        sleep(900);
    }

    return 0;
}

int calculate_result(data_t* data, result_t* result) {
    if (data == NULL || result == NULL) {
        return -1;
    }

    memset(result, 0, sizeof(*result));

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

    result->use_solar = solar_available ? 1 : 0;

    if (solar_available) {
        if (!battery_high) {
            result->charge_battery = 1;
        }

        if (battery_high && price_high) {
            result->sell_excess = 1;
        }

        result->buy_electricity = 0;
    } else {
        if (price_low) {
            result->buy_electricity = 1;
            if (!battery_high) {
                result->charge_battery = 1;
            }
        } else if (!battery_low) {
            result->charge_battery = 2;
            if (price_high && battery_pct >= battery_sell_threshold) {
                result->sell_excess = 1;
            }
        } else {
            result->buy_electricity = 1;
        }
    }

    return 0;
}

#endif

/*
LEOP-systemet ska tillhandahålla följande funktionalitet för energioptimering.
Energidata och analys
• Hämta väderdata: solinstrålning, molnighet, temperatur
• Beräkna förväntad solcellsproduktion per 15 minuter
• Lagra prognoser lokalt med cache och TTL (Time-To-Live)
• Hämta spotprisdata per 15 minuter
• Matcha spotprisdata mot solprognos
• Beräkna optimala tider för elförbrukning, energilagring och försäljning
Optimering
Systemet ska generera en tidsbaserad energiplan (24-72 timmar) som visar:
• När systemet bör köpa el från nätet
• När egen solproduktion ska användas direkt
• När batteri ska laddas respektive urladdas
• När försäljning av överskottsproduktion är gynnsam
*/