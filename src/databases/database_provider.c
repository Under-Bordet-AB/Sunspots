#include "database_provider.h"

#include "db_mock.h"
#include "db_sql.h"
#include "db_sdk.h"
#include "db_json.h"

const i_database *get_database(database_implementation_t implementation)
{
    switch (implementation) {
        case DB_MOCK:
            return &db_mock;
            break;
        case DB_SQL:
            return &db_sql;
            break;
        case DB_SDK:
            return &db_sdk;
            break;
        case DB_JSON:
            return &db_json;
            break;
    }

	return &db_mock;
}
