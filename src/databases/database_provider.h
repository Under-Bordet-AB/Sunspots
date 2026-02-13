#ifndef DATABASE_PROVIDER_H
#define DATABASE_PROVIDER_H

#include "../interfaces/i_database.h"

typedef enum {
	DB_MOCK = 0,
	DB_SQL,
	DB_SDK,
	DB_JSON
} database_implementation_t;

const i_database *get_database(database_implementation_t implementation);

#endif
