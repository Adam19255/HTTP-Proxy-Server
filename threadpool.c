#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "threadpool.h"

threadpool* create_threadpool(int num_threads_in_pool) {
    // Checks if the given number of threads is within a valid range.
    if (num_threads_in_pool <= 0 || num_threads_in_pool > MAXT_IN_POOL) {
        perror("Invalid pool size\n");
        return NULL;
    }

    // Allocating memory for the threadpool structure.
    threadpool* pool = (threadpool*)malloc(sizeof(threadpool));
    if (pool == NULL) {
        perror("malloc\n");
        exit(1);
    }

    // Initialize the fields of the threadpool structure.
    pool->num_threads = num_threads_in_pool;
    pool->qsize = 0;
    pool->qhead = pool->qtail = NULL;
    pool->shutdown = pool->dont_accept = 0;

    // Initialize the mutex for the queue.
    if (pthread_mutex_init(&pool->qlock, NULL) != 0) {
        perror("pthread_mutex_init\n");
        exit(1);
    }

    // Initialize the condition variables for non-empty and empty queue.
    if (pthread_cond_init(&pool->q_not_empty, NULL) != 0 || pthread_cond_init(&pool->q_empty, NULL) != 0) {
        perror("pthread_cond_init\n");
        exit(1);
    }

    // Allocate memory for an array of threads.
    pool->threads = (pthread_t*)malloc(num_threads_in_pool * sizeof(pthread_t));
    if (pool->threads == NULL) {
        perror("malloc\n");
        exit(1);
    }

    // Create the specified number of threads, each executing the do_work function, with the pool as an argument.
    for (int i = 0; i < num_threads_in_pool; ++i) {
        if (pthread_create(&pool->threads[i], NULL, do_work, (void*)pool) != 0) {
            perror("pthread_create\n");
            exit(1);
        }
    }

    return pool;
}

void dispatch(threadpool* from_me, dispatch_fn dispatch_to_here, void* arg) {
    // Enter critical section
    // Lock the queue mutex to ensure thread-safe access to the queue.
    pthread_mutex_lock(&from_me->qlock);

    if (from_me->dont_accept) {
        perror("Task dispatch not accepted during destruction\n");
        // Unlocking the mutex.
        pthread_mutex_unlock(&from_me->qlock);
        return;
    }

    // Allocating memory for a new work_t structure, and initializing it with the provided routine and argument.
    work_t* work = (work_t*)malloc(sizeof(work_t));
    if (work == NULL) {
        perror("malloc\n");
        exit(1);
    }

    work->routine = dispatch_to_here;
    work->arg = arg;
    work->next = NULL;


    // If the queue is empty, we add the new task to both the head and tail, signaling that the queue is not empty
    if (from_me->qsize == 0) {
        from_me->qhead = from_me->qtail = work;
        pthread_cond_signal(&from_me->q_not_empty);
    }
        // If the queue is not empty, we add the new task to the tail of the queue.
    else {
        from_me->qtail->next = work;
        from_me->qtail = work;
    }

    // Updating the queue size
    from_me->qsize++;

    pthread_mutex_unlock(&from_me->qlock);
    // Exit critical section
}

void* do_work(void* p) {
    threadpool* pool = (threadpool*)p;

    while (1) {
        // Enter critical section
        pthread_mutex_lock(&pool->qlock);

        // If there are no jobs and shutdown flag is not on, then the current thread goto sleep
        while (pool->qsize == 0 && !pool->shutdown) {
            pthread_cond_wait(&pool->q_not_empty, &pool->qlock);
        }

        // If shutdown flag is on, then unlock the lock and exit
        if (pool->shutdown) {
            pthread_mutex_unlock(&pool->qlock);
            pthread_exit(NULL);
        }

        // Dequeue a task from the head of the queue and update the queue pointers.
        work_t* work = pool->qhead;

        if (work == NULL){
            pthread_mutex_unlock(&pool->qlock);
        }
        else{
            pool->qsize--;
            pool->qhead = work->next;

            // If don't accept any job, and we've finished to process all the jobs in the queue
            // then wakeup the thread that wait at the destroy function
            if (pool->qsize == 0 && pool->dont_accept) {
                pool->qtail = NULL;
                pthread_cond_signal(&pool->q_empty);
            }

            pthread_mutex_unlock(&pool->qlock);
            // Exit critical section
            // Execute the thread routine
            work->routine(work->arg);
        }
        free(work);
    }
}

void destroy_threadpool(threadpool* destroyme) {
    // Enter critical section
    pthread_mutex_lock(&destroyme->qlock);
    // Set that the threadpool destruction has begun
    destroyme->dont_accept = 1;

    // If there are still jobs in the queue, then goto sleep
    while (destroyme->qsize > 0) {
        pthread_cond_wait(&destroyme->q_empty, &destroyme->qlock);
    }

    // Set the shutdown flag since there are no more jobs in the queue
    destroyme->shutdown = 1;
    // Wakeup all threads that wait while the qsize == 0
    pthread_cond_broadcast(&destroyme->q_not_empty);

    pthread_mutex_unlock(&destroyme->qlock);
    // End critical section

    // Wait for the exits of the threads
    for (int i = 0; i < destroyme->num_threads; ++i) {
        pthread_join(destroyme->threads[i], NULL);
    }

    // Destroy the mutex and condition variables.
    pthread_mutex_destroy(&destroyme->qlock);
    pthread_cond_destroy(&destroyme->q_not_empty);
    pthread_cond_destroy(&destroyme->q_empty);

    // Free the memory allocated for the array of threads and the thread pool.
    free(destroyme->threads);
    free(destroyme);
}
