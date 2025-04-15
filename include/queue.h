#ifndef QUEUE_H
#define QUEUE_H
#include <stdbool.h>
#include <stddef.h>

struct queue_node {
  void *data;
  struct queue_node *next;
};

struct queue{
  size_t queue_length;
  size_t data_size;
  struct queue_node *head;
  struct queue_node *tail;
};

bool isEmpty(struct queue*);
int dequeue(struct queue*, void *);
int enqueue(struct queue*, void *);
int queue_clear(struct queue*);
int queue_peek(struct queue* q, void *data);
int queue_init(struct queue*q, size_t data_size);
int queue_get_length(struct queue *q);
int queue_destroy(struct queue *q);

#endif
