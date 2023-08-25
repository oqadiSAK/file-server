#include "../include/queue.h"

queue_t *queue_create()
{
    queue_t *queue = malloc(sizeof(queue_t));
    queue->data = malloc(sizeof(void *) * 1);
    queue->front = 0;
    queue->rear = -1;
    queue->capacity = 1;
    queue->size = 0;
    return queue;
}

void queue_destroy(queue_t *queue)
{
    free(queue->data);
    free(queue);
}

void queue_enqueue(queue_t *queue, void *item)
{
    if (queue->size == queue->capacity)
    {
        queue->capacity *= 2;
        queue->data = realloc(queue->data, sizeof(void *) * queue->capacity);
    }
    queue->rear = (queue->rear + 1) % queue->capacity;
    queue->data[queue->rear] = item;
    queue->size++;
}

void *queue_dequeue(queue_t *queue)
{
    if (queue->size == 0)
    {
        return NULL;
    }
    void *item = queue->data[queue->front];
    queue->front = (queue->front + 1) % queue->capacity;
    queue->size--;
    return item;
}

void *queue_peek(queue_t *queue)
{
    if (queue->size == 0)
    {
        return NULL;
    }
    return queue->data[queue->front];
}

int queue_size(queue_t *queue)
{
    return queue->size;
}