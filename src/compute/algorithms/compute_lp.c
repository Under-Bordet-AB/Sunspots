#include "compute_lp.h"

#include <math.h>
#include <stdlib.h>

#define DIRECT_COL(t) (1 + (t))
#define BUY_COL(slots, t) (1 + (slots) + (t))
#define CHARGE_COL(slots, t) (1 + 2 * (slots) + (t))
#define SELL_COL(slots, t) (1 + 3 * (slots) + (t))

#define SOLAR_ROW(t) (1 + (t))
#define ACTIVITY_ROW(slots, t) (1 + (slots) + (t))

static double clamp01(double x)
{
    if (x < 0.0) return 0.0;
    if (x > 1.0) return 1.0;
    return x;
}

static double normalize(double value, double min_v, double max_v)
{
    if (max_v <= min_v) {
        return 0.5;
    }
    return clamp01((value - min_v) / (max_v - min_v));
}

int compute_lp(const compute_data_t* data_in, result_t* result_out) {
    if (!data_in || !result_out) {
        return -1;
    }

    const int slots = data_in->horizon_len;
    if (slots <= 0 || slots > SERIES_LEN) {
        return -1;
    }

    double max_irradiance = 0.0;
    double min_price = INFINITY;
    double max_price = -INFINITY;
    double sun_cap[SERIES_LEN] = {0.0};
    double cheap_cap[SERIES_LEN] = {0.0};

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        if (!isfinite(data_in->price_kwh[time_slot]) ||
            !isfinite(data_in->irradiance[time_slot]) ||
            !isfinite(data_in->cloudiness[time_slot])) {
            return -1;
        }

        if (data_in->irradiance[time_slot] > max_irradiance) {
            max_irradiance = data_in->irradiance[time_slot];
        }
        if (data_in->price_kwh[time_slot] < min_price) {
            min_price = data_in->price_kwh[time_slot];
        }
        if (data_in->price_kwh[time_slot] > max_price) {
            max_price = data_in->price_kwh[time_slot];
        }
    }

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        const double irradiance = (data_in->irradiance[time_slot] < 0.0) ? 0.0 : data_in->irradiance[time_slot];
        const double irradiance_norm = (max_irradiance > 0.0) ? clamp01(irradiance / max_irradiance) : 0.0;
        const double cloud_norm = clamp01(data_in->cloudiness[time_slot] / 100.0);
        const double clear_sky_score = 1.0 - cloud_norm;
        const double sun_score = clamp01(0.75 * irradiance_norm + 0.25 * clear_sky_score);

        const double price_norm = normalize(data_in->price_kwh[time_slot], min_price, max_price);
        const double cheap_score = 1.0 - price_norm;

        sun_cap[time_slot] = 100.0 * sun_score;
        cheap_cap[time_slot] = 100.0 * cheap_score;
    }

    glp_prob *problem = glp_create_prob();
    if (!problem) {
        return -1;
    }

    glp_set_prob_name(problem, "sunspots_recommendation_lp");
    glp_set_obj_dir(problem, GLP_MAX);

    glp_add_cols(problem, 4 * slots);
    glp_add_rows(problem, 2 * slots);

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        const double price_norm = normalize(data_in->price_kwh[time_slot], min_price, max_price);
        const double cheap_score = 1.0 - price_norm;
        const double pricey_score = price_norm;

        glp_set_col_bnds(problem, DIRECT_COL(time_slot), GLP_DB, 0.0, 100.0);
        if (cheap_cap[time_slot] <= 0.0) {
            glp_set_col_bnds(problem, BUY_COL(slots, time_slot), GLP_FX, 0.0, 0.0);
        } else {
            glp_set_col_bnds(problem, BUY_COL(slots, time_slot), GLP_DB, 0.0, cheap_cap[time_slot]);
        }
        glp_set_col_bnds(problem, CHARGE_COL(slots, time_slot), GLP_DB, 0.0, 100.0);
        glp_set_col_bnds(problem, SELL_COL(slots, time_slot), GLP_DB, 0.0, 100.0);

        glp_set_obj_coef(problem, DIRECT_COL(time_slot), 0.65 + 0.35 * cheap_score);
        glp_set_obj_coef(problem, BUY_COL(slots, time_slot), 0.30 + 0.70 * cheap_score);
        glp_set_obj_coef(problem, CHARGE_COL(slots, time_slot), 0.40 + 0.60 * cheap_score);
        glp_set_obj_coef(problem, SELL_COL(slots, time_slot), 0.20 + 0.80 * pricey_score);

        glp_set_row_bnds(problem, SOLAR_ROW(time_slot), GLP_UP, 0.0, sun_cap[time_slot]);
        glp_set_row_bnds(problem, ACTIVITY_ROW(slots, time_slot), GLP_UP, 0.0, 100.0);
    }

    const int max_non_zero = 7 * slots;
    int *row_idx = (int *)malloc((size_t)(max_non_zero + 1) * sizeof(int));
    int *col_idx = (int *)malloc((size_t)(max_non_zero + 1) * sizeof(int));
    double *value = (double *)malloc((size_t)(max_non_zero + 1) * sizeof(double));

    if (!row_idx || !col_idx || !value) {
        free(row_idx);
        free(col_idx);
        free(value);
        glp_delete_prob(problem);
        return -1;
    }

    int nz = 1;
    for (int time_slot = 0; time_slot < slots; time_slot++) {
        row_idx[nz] = SOLAR_ROW(time_slot);
        col_idx[nz] = DIRECT_COL(time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = SOLAR_ROW(time_slot);
        col_idx[nz] = CHARGE_COL(slots, time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = SOLAR_ROW(time_slot);
        col_idx[nz] = SELL_COL(slots, time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = ACTIVITY_ROW(slots, time_slot);
        col_idx[nz] = DIRECT_COL(time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = ACTIVITY_ROW(slots, time_slot);
        col_idx[nz] = BUY_COL(slots, time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = ACTIVITY_ROW(slots, time_slot);
        col_idx[nz] = CHARGE_COL(slots, time_slot);
        value[nz++] = 1.0;

        row_idx[nz] = ACTIVITY_ROW(slots, time_slot);
        col_idx[nz] = SELL_COL(slots, time_slot);
        value[nz++] = 1.0;
    }

    glp_load_matrix(problem, nz - 1, row_idx, col_idx, value);
    free(row_idx);
    free(col_idx);
    free(value);

    glp_smcp simplex_params;
    glp_init_smcp(&simplex_params);
    simplex_params.msg_lev = GLP_MSG_OFF;

    if (glp_simplex(problem, &simplex_params) != 0) {
        glp_delete_prob(problem);
        return -1;
    }

    {
        int solve_status = glp_get_status(problem);
        if (solve_status != GLP_OPT && solve_status != GLP_FEAS) {
            glp_delete_prob(problem);
            return -1;
        }
    }

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        result_out->direct_use[time_slot] = glp_get_col_prim(problem, DIRECT_COL(time_slot));
        result_out->buy_electricity[time_slot] = glp_get_col_prim(problem, BUY_COL(slots, time_slot));
        result_out->charge_battery[time_slot] = glp_get_col_prim(problem, CHARGE_COL(slots, time_slot));
        result_out->sell_excess[time_slot] = glp_get_col_prim(problem, SELL_COL(slots, time_slot));
    }

    glp_delete_prob(problem);

    return 0;
}
