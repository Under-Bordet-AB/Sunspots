#define _GNU_SOURCE
#define _XOPEN_SOURCE 700
#include <time.h>
#include "unit_utils.h"

double fahrenheit_to_celsius(double fahrenheit) {
    return (fahrenheit - 32) * 5.0 / 9.0;
}

int iso8601_to_unix(const char* iso8601_time) {
    struct tm t = {0};

    if (strptime(iso8601_time, "%Y-%m-%dT%H:%M", &t) == NULL) {
        return -1;
    }

    return (int)timegm(&t);
}
