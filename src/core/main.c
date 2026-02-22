/**
 * @file main.c
 */

#include "daemon.h"
#include <stdlib.h>

int main(int argc, char **argv)
{
    (void)argc; 
    (void)argv;
	
    daemon_var_t *sunspots = NULL;

    daemon_init(&sunspots);
    daemon_run(sunspots);
    daemon_deinit(&sunspots);

    return EXIT_SUCCESS;
}
