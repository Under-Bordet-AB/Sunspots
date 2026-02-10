#ifndef FETCH_ENGINE_H
#define FETCH_ENGINE_H

#include "config/config.h"

int fetch_engine_run(int argc, char** argv, config* cfg, const config* common, const config* worker);

#endif
