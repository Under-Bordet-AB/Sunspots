#ifndef DATABASE_PROVIDER_H
#define DATABASE_PROVIDER_H

#include "../interfaces/i_database.h"

const i_database *get_database(int implementation);

#endif
