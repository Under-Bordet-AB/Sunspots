#include <stddef.h>

#include "db_json.h"

static database_status_t db_json_save_latest_temperature(double temperature_c)
{
	(void)temperature_c;
	return DATABASE_OK;
}

static database_status_t db_json_save_latest_irradiance(double solar_radiation_w_per_m2)
{
	(void)solar_radiation_w_per_m2;
	return DATABASE_OK;
}

static database_status_t db_json_save_latest_cloudiness(double cloud_cover_percent)
{
	(void)cloud_cover_percent;
	return DATABASE_OK;
}

static database_status_t db_json_get_latest_temperature(double *out_temperature_c)
{
	if (out_temperature_c == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_temperature_c = 0.0;
	return DATABASE_OK;
}

static database_status_t db_json_get_latest_irradiance(double *out_solar_radiation_w_per_m2)
{
	if (out_solar_radiation_w_per_m2 == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_solar_radiation_w_per_m2 = 0.0;
	return DATABASE_OK;
}

static database_status_t db_json_get_latest_cloudiness(double *out_cloud_cover_percent)
{
	if (out_cloud_cover_percent == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_cloud_cover_percent = 0.0;
	return DATABASE_OK;
}

i_database db_json = {
	.save_latest_temperature = db_json_save_latest_temperature,
	.save_latest_irradiance = db_json_save_latest_irradiance,
	.save_latest_cloudiness = db_json_save_latest_cloudiness,
	.get_latest_temperature = db_json_get_latest_temperature,
	.get_latest_irradiance = db_json_get_latest_irradiance,
	.get_latest_cloudiness = db_json_get_latest_cloudiness,
};
