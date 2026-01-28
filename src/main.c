#include <stdlib.h>
#include <sys/wait.h>

// #include "fetch/fetch_manager.h"

// int main() {
//     fetch_init();
//     atexit(fetch_cleanup);

//     if (fetch_pid > 0) {
//         int status;
//         waitpid(fetch_pid, &status, 0);
//     }

//     return 0;
// }