#ifndef I_DATABASE_H
#define I_DATABASE_H

typedef enum {
	DATABASE_OK = 0,
	DATABASE_INVALID_INPUT,
	DATABASE_NOT_FOUND,
	DATABASE_WRITE_ERROR,
	DATABASE_READ_ERROR,
	DATABASE_NOT_IMPLEMENTED
} database_status_t;

typedef struct {
	database_status_t (*save_latest_temperature)(double temperature_c);
	database_status_t (*save_latest_irradiance)(double solar_radiation_w_per_m2);
	database_status_t (*save_latest_cloudiness)(double cloud_cover_percent);
	// database_status_t (*save_elpris_24h)(double price);

	database_status_t (*get_latest_temperature)(double *out_temperature_c);
	database_status_t (*get_latest_irradiance)(double *out_solar_radiation_w_per_m2);
	database_status_t (*get_latest_cloudiness)(double *out_cloud_cover_percent);
	// database_status_t (*get_elpris_this_quarter)(double *out_price);
} i_database;

#endif