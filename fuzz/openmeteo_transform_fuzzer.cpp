#include <stddef.h>
#include <stdint.h>

#include <vector>

extern "C" {
#include "cJSON.h"
#include "weather_model.h"
#include "weather_transform.h"
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    if (data == NULL || size > 262144U) {
        return 0;
    }

    std::vector<char> payload(data, data + size);
    payload.push_back('\0');

    cJSON *root = cJSON_Parse(payload.data());
    if (root == NULL) {
        return 0;
    }

    weather_data_t out;
    weather_data_init(&out);

    (void)transform_openmeteo_weather(root, &out);
    (void)transform_openmeteo_solar(root, &out);

    cJSON_Delete(root);
    return 0;
}

#include "afl_driver.h"
