#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <time.h>
#include <stdio.h>
#include <string.h>
#include "unit_utils.h"

double fahrenheit_to_celsius(double fahrenheit) {
    return (fahrenheit - 32) * 5.0 / 9.0;
}

int iso8601_to_unix(const char* iso8601_time) {
    struct tm t = {0};

    char *err = strptime(iso8601_time, "%Y-%m-%dT%H:%M:%S", &t);

    if (err == NULL) {
        // Try without seconds
        err = strptime(iso8601_time, "%Y-%m-%dT%H:%M", &t);
        if (err == NULL) {
            return -1;
        }
    }
    const char *day_time = iso8601_time+9; //step past date
    const char *tz = strchr(day_time, '+');
    if (tz == NULL) { tz = strchr(day_time, '-'); }
    int offset_seconds = 0;

    if (tz != NULL && (*tz == '+' || *tz == '-')){
        char tz_sign = *tz;
        int tz_hour, tz_min;
        if (sscanf(tz+1, "%2d:%2d", &tz_hour, &tz_min) != 2){
            return -1;
        }

        offset_seconds = 3600 * tz_hour + 60 * tz_min;
        if (tz_sign == '-'){
            offset_seconds = -offset_seconds;
        }
    }

    return (int)timegm(&t) - offset_seconds;
}
