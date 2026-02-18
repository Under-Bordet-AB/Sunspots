#include <gtest/gtest.h>

#include <cstring>

extern "C" {
#include "sdk/ss_sdk.h"
}

TEST(sdk_canonical_module, invalid_ids_return_null)
{
    EXPECT_EQ(ss_metric_meta_get((ss_metric_id)-1), (const ss_metric_meta *)NULL);
    EXPECT_EQ(ss_metric_meta_get((ss_metric_id)SS_METRIC_COUNT), (const ss_metric_meta *)NULL);
    EXPECT_EQ(ss_metric_name((ss_metric_id)-1), (const char *)NULL);
    EXPECT_EQ(ss_metric_name((ss_metric_id)SS_METRIC_COUNT), (const char *)NULL);
}

TEST(sdk_canonical_module, table_entries_are_complete_and_consistent)
{
    for (int id = 0; id < (int)SS_METRIC_COUNT; ++id) {
        const ss_metric_meta *meta = ss_metric_meta_get((ss_metric_id)id);
        ASSERT_NE(meta, (const ss_metric_meta *)NULL);
        EXPECT_EQ((int)meta->id, id);
        ASSERT_NE(meta->canonical_name, (const char *)NULL);
        EXPECT_NE(meta->canonical_name[0], '\0');
        ASSERT_NE(meta->unit, (const char *)NULL);
        EXPECT_NE(meta->unit[0], '\0');

        EXPECT_TRUE(
            meta->value_type == SS_SDK_VALUE_I64 ||
            meta->value_type == SS_SDK_VALUE_F64 ||
            meta->value_type == SS_SDK_VALUE_BOOL);

        const char *name = ss_metric_name((ss_metric_id)id);
        ASSERT_NE(name, (const char *)NULL);
        EXPECT_EQ(std::strcmp(name, meta->canonical_name), 0);
    }
}
