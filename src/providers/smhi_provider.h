#ifndef SMHI_PROVIDER_H
#define SMHI_PROVIDER_H

#include "sdk/sunspots_sdk.h"

int smhi_emit_from_json(sssdk_runtime* rt, const char* json, long timestamp);

#endif
