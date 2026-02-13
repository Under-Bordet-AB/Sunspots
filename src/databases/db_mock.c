#include <stddef.h>
#include "../interfaces/i_database.h"

static double mock_latest_temperature_c = 21.5;
static double mock_latest_irradiance_w_per_m2 = 650.0;
static double mock_latest_cloudiness_percent = 35.0;
static double mock_elpris_this_quarter = 1.25;

static database_status_t mock_save_latest_temperature(double temperature_c)
{
	mock_latest_temperature_c = temperature_c;
	return DATABASE_OK;
}

static database_status_t mock_save_latest_irradiance(double solar_radiation_w_per_m2)
{
	mock_latest_irradiance_w_per_m2 = solar_radiation_w_per_m2;
	return DATABASE_OK;
}

static database_status_t mock_save_latest_cloudiness(double cloud_cover_percent)
{
	mock_latest_cloudiness_percent = cloud_cover_percent;
	return DATABASE_OK;
}

/*
static database_status_t mock_save_elpris_24h(double price)
{
	mock_elpris_this_quarter = price;
	return DATABASE_OK;
}
*/

static database_status_t mock_get_latest_temperature(double *out_temperature_c)
{
	if (out_temperature_c == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_temperature_c = mock_latest_temperature_c;
	return DATABASE_OK;
}

static database_status_t mock_get_latest_irradiance(double *out_solar_radiation_w_per_m2)
{
	if (out_solar_radiation_w_per_m2 == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_solar_radiation_w_per_m2 = mock_latest_irradiance_w_per_m2;
	return DATABASE_OK;
}

static database_status_t mock_get_latest_cloudiness(double *out_cloud_cover_percent)
{
	if (out_cloud_cover_percent == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_cloud_cover_percent = mock_latest_cloudiness_percent;
	return DATABASE_OK;
}

/*
static database_status_t mock_get_elpris_this_quarter(double *out_price)
{
	if (out_price == NULL) {
		return DATABASE_INVALID_INPUT;
	}

	*out_price = mock_elpris_this_quarter;
	return DATABASE_OK;
}
*/

i_database db_mock = {
	.save_latest_temperature = mock_save_latest_temperature,
	.save_latest_irradiance = mock_save_latest_irradiance,
	.save_latest_cloudiness = mock_save_latest_cloudiness,
	//.save_elpris_24h = mock_save_elpris_24h,

	.get_latest_temperature = mock_get_latest_temperature,
	.get_latest_irradiance = mock_get_latest_irradiance,
	.get_latest_cloudiness = mock_get_latest_cloudiness,
	//.get_elpris_this_quarter = mock_get_elpris_this_quarter,
};
