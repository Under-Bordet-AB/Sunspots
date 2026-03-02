# Daemon Live Logger

A lightweight utility that writes "live logs" to a file. The program leverages the fact that all processes spawned by the daemon are assigned the `"sunspots_system"` environment variable. It parses the UNIX domain socket path defined within that JSON configuration to determine where to send its messages.

## Usage
To use the logger in your module, include the `daemon_logger.h` header file from the `./src/core/` directory.

## Usage
To use the logger in your module simply include the headerfiler `daemon_logger.h` from `./src/core/`. Call the function `daemon_logger_send("Module name", "Message from module");`.<br>The output is written to `.logs/daemon.log`. 

### Compile flags
The `daemon.log` get truncated based on a predeterimed size. This size can be changed using the following flags:<br>
```C
/* use flags to set size of file before truncation
   Defines are found inside daemon_logger.c */
#if   defined(BUF_64)
    #define LOG_BUF_SIZE 65536
#elif defined(BUF_32)
    #define LOG_BUF_SIZE 32768
#elif defined(BUF_16)
    #define LOG_BUF_SIZE 16384
#elif defined(BUF_8)
    #define LOG_BUF_SIZE 8192
#elif defined(BUF_4)
    #define LOG_BUF_SIZE 4096
#elif defined(BUF_2)
    #define LOG_BUF_SIZE 2048
#else
    #define LOG_BUF_SIZE 1024
#endif

```

## Build
`gcc daemon_logger.c cJSON.c -o daemon_logger [-BUF_16]`
