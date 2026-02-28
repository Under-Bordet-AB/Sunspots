#include "compute_lp.h"

#include <math.h>
#include <stdlib.h>

#define BUY_COL(t) (1 + (t))
#define DIRECT_COL(slots, t) (1 + (slots) + (t))
#define CHARGE_COL(slots, t) (1 + 2 * (slots) + (t))
#define SELL_COL(slots, t) (1 + 3 * (slots) + (t))

#define NOCT_C 45.0
#define CELL_REF_TEMP_C 25.0
#define IRRADIANCE_REF_WM2 1000.0
#define TEMP_COEFF_PER_C -0.004
#define PERFORMANCE_RATIO 0.85
#define SLOT_HOURS 0.25

static double clamp01(double value) {
    if (value < 0.0) {
        return 0.0;
    }
    if (value > 1.0) {
        return 1.0;
    }
    return value;
}

int compute_lp(const compute_data_t* data_in, result_t* result_out) {
    if (!data_in || !result_out) {
        return -1;
    }

    // Initialize

    glp_prob *problem = glp_create_prob();
    if (!problem) {
        return -1;
    }

    glp_set_prob_name(problem, "sunspots_recommendations");
    glp_set_obj_dir(problem, GLP_MIN);

    // Bounds, coefficients and constraints

    int slots = data_in->horizon_len;
    if (slots <= 0 || slots > SERIES_LEN) {
        glp_delete_prob(problem);
        return -1;
    }

    glp_add_cols(problem, 4 * slots);
    glp_add_rows(problem, 2 * slots);

    int nz = 1 + (7 * slots);
    int* row_idx = (int*)malloc((size_t)nz * sizeof(int));
    int* col_idx = (int*)malloc((size_t)nz * sizeof(int));
    double* value = (double*)malloc((size_t)nz * sizeof(double));
    if (!row_idx || !col_idx || !value) {
        free(row_idx);
        free(col_idx);
        free(value);
        glp_delete_prob(problem);
        return -1;
    }

    int matrix_index = 1;

    for (int time_slot = 0; time_slot < slots; time_slot++) {
        int solar_row = 1 + time_slot;
        int activity_row = 1 + slots + time_slot;

        double irradiance_wm2 = data_in->irradiance[time_slot];
        double cloudiness_percent = data_in->cloudiness[time_slot];
        double ambient_temp_c = data_in->temperature[time_slot];
        double price_kwh = data_in->price_kwh[time_slot];

        double cloudiness_factor = clamp01(1.0 - (cloudiness_percent / 100.0));
        double effective_irradiance_wm2 = irradiance_wm2 * cloudiness_factor;
        if (effective_irradiance_wm2 < 0.0) {
            effective_irradiance_wm2 = 0.0;
        }

        double cell_temp_c = ambient_temp_c + ((NOCT_C - 20.0) / 800.0) * effective_irradiance_wm2;
        double temp_derate = 1.0 + TEMP_COEFF_PER_C * (cell_temp_c - CELL_REF_TEMP_C);
        if (temp_derate < 0.0) {
            temp_derate = 0.0;
        }

        double pv_power_per_unit = (effective_irradiance_wm2 / IRRADIANCE_REF_WM2) * PERFORMANCE_RATIO * temp_derate;
        double pv_cap_norm = clamp01(pv_power_per_unit);

        glp_set_row_bnds(problem, solar_row, GLP_UP, 0.0, pv_cap_norm);
        glp_set_row_bnds(problem, activity_row, GLP_UP, 0.0, 1.0);

        // Decision columns
        glp_set_col_bnds(problem, BUY_COL(time_slot), GLP_DB, 0.0, 1.0);
        glp_set_col_bnds(problem, DIRECT_COL(slots, time_slot), GLP_DB, 0.0, 1.0);
        glp_set_col_bnds(problem, CHARGE_COL(slots, time_slot), GLP_DB, 0.0, 1.0);
        glp_set_col_bnds(problem, SELL_COL(slots, time_slot), GLP_DB, 0.0, 1.0);

        // Decision coefficients
        glp_set_obj_coef(problem, BUY_COL(time_slot), price_kwh * SLOT_HOURS);
        glp_set_obj_coef(problem, DIRECT_COL(slots, time_slot), 0.0);
        glp_set_obj_coef(problem, CHARGE_COL(slots, time_slot), 0.0);
        glp_set_obj_coef(problem, SELL_COL(slots, time_slot), -price_kwh * SLOT_HOURS);

        row_idx[matrix_index] = solar_row;
        col_idx[matrix_index] = DIRECT_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = solar_row;
        col_idx[matrix_index] = CHARGE_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = solar_row;
        col_idx[matrix_index] = SELL_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = activity_row;
        col_idx[matrix_index] = BUY_COL(time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = activity_row;
        col_idx[matrix_index] = DIRECT_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = activity_row;
        col_idx[matrix_index] = CHARGE_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;

        row_idx[matrix_index] = activity_row;
        col_idx[matrix_index] = SELL_COL(slots, time_slot);
        value[matrix_index] = 1.0;
        matrix_index++;
    }

    glp_load_matrix(problem, matrix_index - 1, row_idx, col_idx, value);

    free(row_idx);
    free(col_idx);
    free(value);

    // Solve

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
        result_out->buy_electricity[time_slot] = glp_get_col_prim(problem, BUY_COL(time_slot));
        result_out->direct_use[time_slot] = glp_get_col_prim(problem, DIRECT_COL(slots, time_slot));
        result_out->charge_battery[time_slot] = glp_get_col_prim(problem, CHARGE_COL(slots, time_slot));
        result_out->sell_excess[time_slot] = glp_get_col_prim(problem, SELL_COL(slots, time_slot));
    }

    glp_delete_prob(problem);

    return 0;
}
