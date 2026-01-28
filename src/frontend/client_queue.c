#include "frontend/client_queue.h"
#include "frontend/http_constants.h"

int client_queue[QUEUE_SIZE];
int q_head = 0;
int q_tail = 0;

pthread_mutex_t q_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t q_cond = PTHREAD_COND_INITIALIZER;

void enqueue_client(int fd) {
    pthread_mutex_lock(&q_mutex);

    client_queue[q_tail] = fd;
    q_tail = (q_tail + 1) % QUEUE_SIZE;

    pthread_cond_signal(&q_cond);
    pthread_mutex_unlock(&q_mutex);
}

int dequeue_client() {
    pthread_mutex_lock(&q_mutex);

    while (q_head == q_tail) {
        pthread_cond_wait(&q_cond, &q_mutex);
    }

    int fd = client_queue[q_head];
    q_head = (q_head + 1) % QUEUE_SIZE;

    pthread_mutex_unlock(&q_mutex);
    return fd;
}