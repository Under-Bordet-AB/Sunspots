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

int data_init(data_t** data);
int result_init(result_t** result);
int data_dispose(data_t** data);
int result_dispose(result_t** result);

int calculate_simple(data_t* data, result_t* result);

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