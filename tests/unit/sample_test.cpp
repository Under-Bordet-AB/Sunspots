#include <gtest/gtest.h>

extern "C" {
#include "cJSON.h"
#include "weather_model.h"
#include "weather_transform.h"
}

TEST(SampleTest, ShowsBasicArrangeActAssertFlow) {
  // Arrange: provide deterministic input payload and output state.
  const char* payload =
      "{"
      "\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"\\u00b0C\",\"cloud_cover\":\"%\"},"
      "\"current\":{\"time\":\"2026-02-13T10:00\",\"temperature_2m\":5.0,\"cloud_cover\":42}"
      "}";

  cJSON* json = cJSON_Parse(payload);
  ASSERT_NE(json, nullptr);

  weather_data_t out;
  weather_data_init(&out);

  // Act: call one function under test.
  const transform_status_t status = transform_openmeteo_weather(json, &out);

  // Assert: verify both return code and key output fields.
  EXPECT_EQ(status, TRANSFORM_OK);
  EXPECT_TRUE(out.has_temperature);
  EXPECT_TRUE(out.has_cloud_cover);
  EXPECT_DOUBLE_EQ(out.temperature_c, 5.0);
  EXPECT_DOUBLE_EQ(out.cloud_cover_percent, 42.0);

  cJSON_Delete(json);
}
