#ifndef COMPUTE_LP_H
#define COMPUTE_LP_H

#include <glpk.h>
#include "../compute_models.h"

int compute_lp(const compute_data_t* data_in, result_t* result_out);

#endif