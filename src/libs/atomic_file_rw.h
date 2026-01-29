#ifndef ATOMIC_FILE_RW_H
#define ATOMIC_FILE_RW_H

#include "cJSON.h"
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
/*
 * ======================================================================================
 * ATOMIC_FILE_RW_H - Single Header File Atomic Read/Write Library
 * ======================================================================================
 *
 * USAGE:
 *    Do this in ONE C/C++ file:
 *      #define ATOMIC_FILE_RW_IMPLEMENTATION
 *      #include "atomic_file_rw.h"
 *
 *    Do this in other files:
 *      #include "atomic_file_rw.h"
 *
 * ======================================================================================
 */

// Default file location (Project Root relative)
#define ATOMIC_FILE_DEFAULT_PATH ".db/database.jsonl"

// --- Standardized Constants ---

// Sources
#define AF_SOURCE_API_SMHI "API_smhi"
#define AF_SOURCE_SENSOR "Sensor_Network"
#define AF_SOURCE_CALCULATOR "Calculator_Svc"
#define AF_SOURCE_SYSTEM "System_Log"

// Types
#define AF_TYPE_TEMPERATURE "temperature"
#define AF_TYPE_HUMIDITY "humidity"
#define AF_TYPE_POWER "power_usage"
#define AF_TYPE_LOG "log_entry"
#define AF_TYPE_ERROR "error_report"

// --- Public API ---

/**
 * Saves a structured event to the file.
 * Format: {"timestamp": <now>, "source": "<source>", "type": "<type>", "data":
 * "<data>"}
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * WARNING: THIS FUNCTION BLOCKS (SLEEPS) UNTIL IT GETS THE LOCK!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * @param source    Origin of data (e.g., "API_smhi", "sensor_1").
 * @param type      Type of data (e.g., "temperature", "log").
 * @param data      String payload (e.g., "23.5" or "error message").
 * @return          0 on success, -1 on error (check errno).
 */
int af_save(const char *source, const char *type, const char *data);

/**
 * Reads the ENTIRE file content into a malloc'd buffer provided by the library.
 * This function handles opening, shared locking, reading, and closing
 * automatically.
 *
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 * WARNING: THIS FUNCTION BLOCKS (SLEEPS) IF A WRITER IS ACTIVE!
 * !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
 *
 * @param out_size  Optional pointer to receive the data size in bytes.
 * @return          Malloc'd buffer containing file data (caller must free), or
 * NULL on error.
 */
char *af_read(size_t *out_size);

#endif // ATOMIC_FILE_RW_H

// --- Implementation ---
#if defined(ATOMIC_FILE_RW_IMPLEMENTATION)

int af_save(const char *source, const char *type, const char *data) {
  if (!source || !type || !data) {
    errno = EINVAL;
    return -1;
  }
  const char *filename = ATOMIC_FILE_DEFAULT_PATH;

  // Format using cJSON
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "timestamp", (double)time(NULL));
  cJSON_AddStringToObject(root, "source", source);
  cJSON_AddStringToObject(root, "type", type);

  // We treat data as a string here.
  cJSON_AddStringToObject(root, "data", data);

  char *json_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);

  if (!json_str) {
    errno = ENOMEM;
    return -1;
  }

  // Append newline
  size_t len = strlen(json_str);
  char *final_str = (char *)malloc(len + 2);
  if (!final_str) {
    free(json_str);
    errno = ENOMEM;
    return -1;
  }
  memcpy(final_str, json_str, len);
  final_str[len] = '\n';
  final_str[len + 1] = '\0';
  free(json_str);

  // 1. Open with O_APPEND
  int fd = open(filename, O_WRONLY | O_CREAT | O_APPEND, 0666);
  if (fd == -1) {
    free(final_str);
    return -1;
  }

  // 2. Acquire Exclusive Lock (Blocks until available)
  if (flock(fd, LOCK_EX) == -1) {
    int saved_errno = errno;
    close(fd);
    free(final_str);
    errno = saved_errno;
    return -1;
  }

  // 3. Write Data
  ssize_t written = write(fd, final_str, len + 1);

  // 4. Close (Releases Lock)
  close(fd);
  free(final_str);

  if (written == -1)
    return -1;
  return 0;
}

char *af_read(size_t *out_size) {
  const char *filename = ATOMIC_FILE_DEFAULT_PATH;

  // 1. Open
  int fd = open(filename, O_RDONLY);
  if (fd == -1)
    return NULL; // File might not exist yet

  // 2. Lock
  if (flock(fd, LOCK_SH) == -1) {
    close(fd);
    return NULL;
  }

  // 3. Get Size
  struct stat st;
  if (fstat(fd, &st) == -1) {
    close(fd);
    return NULL;
  }
  size_t size = st.st_size;
  if (out_size)
    *out_size = size;

  if (size == 0) {
    close(fd);
    char *empty = (char *)malloc(1);
    if (!empty)
      return NULL;
    empty[0] = '\0';
    return empty;
  }

  // 4. Allocate
  char *buffer = (char *)malloc(size + 1);
  if (!buffer) {
    close(fd);
    return NULL;
  }

  // 5. Read
  ssize_t total_read = 0;
  while ((size_t)total_read < size) {
    ssize_t r = read(fd, buffer + total_read, size - total_read);
    if (r == -1) {
      if (errno == EINTR)
        continue;
      free(buffer);
      close(fd);
      return NULL;
    }
    if (r == 0)
      break;
    total_read += r;
  }
  buffer[total_read] = '\0';

  // 6. Close (Unlocks)
  close(fd);
  return buffer;
}

#endif // ATOMIC_FILE_RW_IMPLEMENTATION