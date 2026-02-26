#ifndef CLIENT_QUEUE_H
#define CLIENT_QUEUE_H

#include <pthread.h>

#include "http_constants.h"

extern int* client_queue;
extern int q_head;
extern int q_tail;

extern pthread_mutex_t q_mutex;
extern pthread_cond_t q_cond;

int enqueue_client(int fd);
int dequeue_client();

#endif