#include <stddef.h>
#include <stdint.h>

#include <cmath>
#include <cstring>

extern "C" {
#include "compute.h"
}

static double decode_double(const uint8_t *data, size_t size, size_t offset)
{
    uint64_t raw = 0;
    for (size_t i = 0; i < sizeof(raw); ++i) {
        if (offset + i < size) {
            raw |= ((uint64_t)data[offset + i]) << (8U * i);
        }
    }
    double out = 0.0;
    std::memcpy(&out, &raw, sizeof(out));
    if (!std::isfinite(out)) {
        out = 0.0;
    }
    return out;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL || size == 0 || size > 4096U) {
        return 0;
    }

    data_t in = {0};
    result_t out = {0};

    in.irradiance = decode_double(data, size, 0);
    in.cloudiness = decode_double(data, size, 8);
    in.temperature = decode_double(data, size, 16);
    in.spot_price = decode_double(data, size, 24);
    in.battery_charge = decode_double(data, size, 32);

    (void)calculate_simple(&in, &out);
    return 0;
}

#include "afl_driver.h"
