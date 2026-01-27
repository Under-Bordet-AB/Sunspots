#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "fetch/curl_client.h"

int main() {
    printf("=== Testing curl_client module ===\n\n");

    curl_client_t* client = NULL;

    // Test 1: Initialize client
    printf("Test 1: Initializing curl_client...\n");
    int result = curl_client_init(&client);
    if (result != 0) {
        printf("ERROR: curl_client_init failed with code %d\n", result);
        return 1;
    }
    printf("SUCCESS: curl_client initialized\n\n");

    // Test 2: Make a request to a test URL
    const char* test_url = "http://example.com";
    printf("Test 2: Making request to %s...\n", test_url);
    result = curl_client_make_request(&client, test_url);
    if (result != 0) {
        printf("ERROR: curl_client_make_request failed with code %d\n", result);
        curl_client_cleanup(&client);
        return 1;
    }
    printf("SUCCESS: Request initiated\n\n");

    // Test 3: Poll for response
    printf("Test 3: Polling for response...\n");
    int max_polls = 100;
    int poll_count = 0;

    while (poll_count < max_polls) {
        result = curl_client_poll(&client);
        if (result != 0) {
            printf("ERROR: curl_client_poll failed with code %d\n", result);
            curl_client_cleanup(&client);
            return 1;
        }

        // Check if still running
        int is_running = curl_client_is_running(&client);
        if (is_running < 0) {
            printf("ERROR: curl_client_is_running failed\n");
            curl_client_cleanup(&client);
            return 1;
        }

        if (is_running == 0) {
            printf("SUCCESS: Request completed after %d polls\n", poll_count + 1);
            break;
        }

        poll_count++;
        usleep(100000);  // Sleep 100ms between polls

        if (poll_count % 10 == 0) {
            printf("  Poll %d/%d (still running)...\n", poll_count, max_polls);
        }
    }

    if (poll_count >= max_polls) {
        printf("WARNING: Max polls reached, request may still be incomplete\n");
    }
    printf("\n");

    // Test 4: Read response
    printf("Test 4: Reading response...\n");
    char* response_buffer = NULL;
    result = curl_client_read_response(&client, &response_buffer);
    if (result != 0) {
        printf("ERROR: curl_client_read_response failed with code %d\n", result);
        curl_client_cleanup(&client);
        return 1;
    }

    if (response_buffer) {
        size_t len = strlen(response_buffer);
        printf("SUCCESS: Response received\n");
        printf("Response length: %zu bytes\n", len);
        printf("First 200 characters:\n");
        printf("---\n");
        for (size_t i = 0; i < 200 && i < len; i++) {
            putchar(response_buffer[i]);
        }
        printf("\n---\n\n");
        free(response_buffer);
    } else {
        printf("WARNING: No response received\n\n");
    }

    // Test 5: Reset and make another request
    printf("Test 5: Resetting client for reuse...\n");
    result = curl_client_reset(&client);
    if (result != 0) {
        printf("ERROR: curl_client_reset failed with code %d\n", result);
        curl_client_cleanup(&client);
        return 1;
    }
    printf("SUCCESS: Client reset\n\n");

    printf("Test 6: Making second request...\n");
    const char* test_url2 = "http://httpbin.org/get";
    result = curl_client_make_request(&client, test_url2);
    if (result != 0) {
        printf("ERROR: Second curl_client_make_request failed with code %d\n", result);
        curl_client_cleanup(&client);
        return 1;
    }

    // Poll for second request
    poll_count = 0;
    while (poll_count < max_polls) {
        result = curl_client_poll(&client);
        if (result != 0) {
            printf("ERROR: curl_client_poll failed on second request\n");
            curl_client_cleanup(&client);
            return 1;
        }

        if (curl_client_is_running(&client) == 0) {
            printf("SUCCESS: Second request completed\n");
            break;
        }

        poll_count++;
        usleep(100000);
    }

    // Read second response
    response_buffer = NULL;
    result = curl_client_read_response(&client, &response_buffer);
    if (result == 0 && response_buffer) {
        printf("Second response received (%zu bytes)\n\n", strlen(response_buffer));
        free(response_buffer);
    }

    // Test 7: Cleanup
    printf("Test 7: Cleaning up client...\n");
    result = curl_client_cleanup(&client);
    if (result != 0) {
        printf("ERROR: curl_client_cleanup failed with code %d\n", result);
        return 1;
    }
    printf("SUCCESS: Client cleaned up\n\n");

    printf("=== All tests completed successfully ===\n");
    return 0;
}
