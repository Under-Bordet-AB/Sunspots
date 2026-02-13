#include "database_provider.h"

#include "db_mock.h"
// #include "db_sql"
// #include "db_sdk"
// #include "db_json"

enum {
    DB_MOCK,
    DB_SQL,
    DB_SDK,
    DB_JSON
};

const i_database *get_database(int implementation)
{
    switch (implementation) {
        case DB_MOCK:
        &db_mock;
        break;
        case DB_SQL:
        // &db_sql;
        break;
        case DB_SDK:
        // &db_sdk;
        break;
        case DB_JSON:
        // &db_json;
        break;
    }

	return &db_mock;
}
