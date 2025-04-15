#ifndef THREAD_POOL_H
#define THREAD_POOL_H

#include <stdbool.h>
#include "queue.h"
#include <pthread.h>

//TODO: Refactor
struct work{
  void (*work)(void *);
  void *args;
};

struct threadpool{
  struct queue work_queue;
  struct queue thread_queue;
  pthread_mutex_t queue_rw_mutex;  
  size_t threads_created;

  pthread_mutex_t queue_status;
  pthread_cond_t work_available;

  size_t threads_working;
  pthread_mutex_t count_lock;

  pthread_cond_t all_threads_idle;

};

int add_work(struct threadpool *p, void (*f)(void *), void *arg);
int threadpool_init(struct threadpool *p, size_t size);
void *consumer(void *pool);
void threadpool_wait(struct threadpool *p);
int threadpool_destroy(struct threadpool *p);

#endif
