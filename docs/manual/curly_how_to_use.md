# Curly

## Features

- Non-blocking asynchronous HTTP requests
- Simple polling-based API
- Automatic memory management
- Reusable client instances

## Installation

```bash
# Requires libcurl development headers
sudo apt-get install libcurl4-openssl-dev  # Debian/Ubuntu
sudo yum install libcurl-devel             # RedHat/CentOS
```

## Usage

```c
#include "curly.h"

curly_t* client;
curly_init(&client);

curly_make_request(&client, "http://example.com");

while (curly_poll(&client) == 0) {
    if (curly_is_running(&client) == 0) {
        break;
    }
}

char* response = NULL;
curly_read_response(&client, &response);
printf("%s\n", response);

free(response);
curly_cleanup(&client);
```

## API Reference

- `curly_init()` - Initialize the client
- `curly_make_request()` - Start an HTTP GET request
- `curly_poll()` - Poll for request progress
- `curly_is_running()` - Check if request is still running
- `curly_read_response()` - Read response data
- `curly_reset()` - Reset client for reuse
- `curly_cleanup()` - Free all resources
