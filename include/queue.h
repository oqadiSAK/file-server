#ifndef QUEUE_H
#define QUEUE_H

#include <stdlib.h>

typedef struct
{
    void **data;
    int front;
    int rear;
    int capacity;
    int size;
} queue_t;

queue_t *queue_create();
void queue_destroy(queue_t *queue);
void queue_enqueue(queue_t *queue, void *item);
void *queue_dequeue(queue_t *queue);
void *queue_peek(queue_t *queue);
int queue_size(queue_t *queue);

#endif
