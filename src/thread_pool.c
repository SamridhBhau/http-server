#include "thread_pool.h"
#include <unistd.h>
#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int stop = 1;

void post_has_work(struct threadpool *pool){
  pthread_mutex_lock(&pool->queue_status);
  pthread_cond_signal(&(pool->work_available));
  pthread_mutex_unlock(&pool->queue_status);
}

void broadcast_has_work(struct threadpool *pool){
  pthread_mutex_lock(&pool->queue_status);
  pthread_cond_broadcast(&(pool->work_available));
  pthread_mutex_unlock(&pool->queue_status);
}

void wait_has_work(struct threadpool *pool){
  pthread_mutex_lock(&(pool->queue_status));
  while(isEmpty(&pool->work_queue) && (stop == 1)){
    pthread_cond_wait(&(pool->work_available) , &(pool->queue_status));
  }
  pthread_mutex_unlock(&(pool->queue_status));
}

int add_work( struct threadpool *pool, void (*f)(void *args), void *arg_p){

  struct work w;
  w.work = f;
  w.args = arg_p;

  pthread_mutex_lock(&(pool->queue_rw_mutex));

  struct queue *work_queue = &(pool->work_queue);

  if (enqueue(work_queue, &w) == 0){
    pthread_mutex_unlock(&(pool->queue_rw_mutex));
    return 0;
  }

  // Send Signal that queue has work
  post_has_work(pool);
  pthread_mutex_unlock(&(pool->queue_rw_mutex));

  return 1;
}

int get_work(struct threadpool *pool, struct work *work_p){

  int ret;
  int prev_length;
  pthread_mutex_lock(&pool->queue_rw_mutex);
  struct queue *queue = &pool->work_queue;

  prev_length = queue_get_length(queue);
  ret = dequeue(queue, work_p);

  if (ret == 0){
    pthread_mutex_unlock(&pool->queue_rw_mutex);
    return 0;
  }

  // Send signal if queue has more work
  if (prev_length > 1){
    post_has_work(pool);
  }
  pthread_mutex_unlock(&pool->queue_rw_mutex);

  return 1;
}

int threadpool_init(struct threadpool *pool, size_t p_size){
  if(queue_init(&(pool->work_queue), sizeof(struct work)) == 0)
    return 0;

  if(queue_init(&(pool->thread_queue), sizeof(pthread_t)) == 0)
    return 0;

  pthread_mutex_init(&(pool->queue_status), NULL);
  pthread_cond_init(&(pool->work_available), NULL);

  pthread_mutex_init(&(pool->queue_rw_mutex), NULL);

  pthread_mutex_init(&pool->count_lock, NULL);
  pthread_cond_init(&(pool->all_threads_idle), NULL);

  pool->threads_working = 0;
  pool->threads_created = 0;

  for (size_t i = 0; i < p_size; i++){
    pthread_t thread;
    enqueue(&pool->thread_queue, &thread);
    pthread_create(&thread, NULL, consumer, pool);
    pthread_detach(thread);
  }

  while(pool->threads_created != p_size){
  }

  return 1;
}

void *consumer(void *p){
  struct threadpool *pool = (struct threadpool *)p;
  struct queue *work_queue = &(pool->work_queue);

  pthread_mutex_lock(&pool->count_lock);
  pool->threads_created += 1;
  pthread_mutex_unlock(&pool->count_lock);

  while(stop != 0){

    wait_has_work(pool);

    pthread_mutex_lock(&pool->count_lock);
    pool->threads_working++;
    pthread_mutex_unlock(&pool->count_lock);

    struct work c_work;

    if (get_work(pool, &c_work) == 1){
      void (*func)(void *) = c_work.work;
      void *arg = c_work.args;
      func(arg);
    }
    
    pthread_mutex_lock(&pool->count_lock);
    pool->threads_working--;
    if (pool->threads_working == 0){
      pthread_cond_signal(&pool->all_threads_idle);
    }
    pthread_mutex_unlock(&pool->count_lock);
  }

  pthread_mutex_lock(&pool->count_lock);
  pool->threads_created--;
  pthread_mutex_unlock(&pool->count_lock);

  return 0;
}


void threadpool_wait(struct threadpool *pool){
  pthread_mutex_lock(&pool->count_lock);
  while((!isEmpty(&pool->work_queue)) || pool->threads_working){
    pthread_cond_wait(&pool->all_threads_idle, &pool->count_lock);
  }
  pthread_mutex_unlock(&pool->count_lock);

}

int threadpool_destroy(struct threadpool *p){
  // clear work queue
  pthread_mutex_lock(&p->queue_rw_mutex);
  queue_clear(&p->work_queue);
  pthread_mutex_unlock(&p->queue_rw_mutex);

  // stop while loop
  stop = 0; 
  while (p->threads_created > 0){
    broadcast_has_work(p);
    sleep(1);
  }

  printf("Destroyed!\n");
  return 1;
}

