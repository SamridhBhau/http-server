#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"

int queue_init(struct queue *q, size_t data_size){
  q->queue_length = 0;
  q->data_size = data_size;
  q->head = q->tail = NULL;

  return 1;
}

bool isEmpty(struct queue *q){
  if (q->queue_length == 0){
    return true;
  }
  return false;
}

int enqueue(struct queue *q, void *data){
  struct queue_node *q_node = malloc(sizeof(struct queue_node));
  q_node->data = malloc(q->data_size);
  memcpy(q_node->data, data, q->data_size);
  q_node->next = NULL;

  if (isEmpty(q)){
    q->head = q_node;
    q->tail = q_node;
    q->queue_length += 1;
    return 1;
  }

  q->tail->next = q_node;
  q->tail = q_node;
  q->queue_length += 1;
  return 1;
}

int dequeue(struct queue *q, void *data){
  if (isEmpty(q))
    return 0;
  
  memcpy(data, q->head->data, q->data_size);
  struct queue_node *node_out = q->head;

  if (q->queue_length == 1){
    q->head = NULL;
    q->tail = NULL;
  }
  else if (q->queue_length > 1) {
    q->head = q->head->next;
  }

  q->queue_length--;

  free(node_out->data);
  free(node_out);
  return 1;
}

int queue_clear(struct queue *q){

  while (q->head != NULL){
    struct queue_node *q_node = q->head;
    q->head = q->head->next;

    free(q_node->data);
    free(q_node);
  }

  return 1;
}

int queue_peek(struct queue* q, void *data){
  if (isEmpty(q))
    return 0;


  if (q->queue_length > 0){
    memcpy(data, q->head->data, q->data_size);
    return 1;
  }

  return 0;
}

int queue_get_length(struct queue *q){
  return q->queue_length;
}

int queue_destroy(struct queue *q){
  queue_clear(q);
  free(q);

  return 0;
}
