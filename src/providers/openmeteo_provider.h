#ifndef OPENMETEO_PROVIDER_H
#define OPENMETEO_PROVIDER_H

#include "sdk/sunspots_sdk.h"

int openmeteo_emit_from_json(sssdk_runtime* rt, const char* json, long timestamp);

#endif
