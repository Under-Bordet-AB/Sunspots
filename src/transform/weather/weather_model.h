#ifndef WEATHER_MODEL_H
#define WEATHER_MODEL_H

#include <time.h>

typedef struct {
    time_t timestamp;
    double temperature_c;
    double windspeed_mps;
    double precipitation_mm;
} weather_data_t;

#endif