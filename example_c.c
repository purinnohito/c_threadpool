#include "threads.h"
#include "thapool.h"

#include <stdio.h>
#include <stdlib.h>


#define ARRY_SIZE 100000

static int count = 0;
static mtx_t count_mutex;

struct array_elem{
	int     elm;
	int*    sum;
	mtx_t*  sum_mutex;
    char*   ptrStr;
};

static inline void add_task(void *ptr);
static inline void show_task(void *ptr);
static inline void free_task(void *ptr);
void c_threadpool_example()
{
	mtx_t  sum_mutex;
	int arr_elem_sum=0;
    struct array_elem *arr = malloc(sizeof(struct array_elem)*ARRY_SIZE);
    c_ThreadPool_st *pool;

    volatile ret, failed_count = 0;

    mtx_init(&count_mutex, mtx_plain);
    mtx_init(&(sum_mutex), mtx_plain);
    for (int i = 0; i < ARRY_SIZE; i++) {
        arr[i].elm= i;
        arr[i].sum = &arr_elem_sum;
        arr[i].sum_mutex=&sum_mutex;
    }

    /* Create a thapool of 10 thread workers. */
    if ((pool = c_ThreadPool_init_pool(10)) == NULL) {
        printf("Error! Failed to create a thread pool struct.\n");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < ARRY_SIZE; i++) {
        if (i % 10000 == 0) {
            /* add free func. */
            
            
            arr[i].ptrStr = malloc(sizeof(char) * 30);
            strcpy_s(arr[i].ptrStr, 29, "task count =");
            ret = c_ThreadPool_add_task_ex(pool, show_task, &arr[i], free_task);
        }
        else{
            /* add. */
            ret = c_ThreadPool_add_task(pool, add_task, &arr[i]);
        }

        if (ret == -1) {
            printf("An error had occurred while adding a task.");
            exit(EXIT_FAILURE);
        }

        if (ret == -2) {
            failed_count++;
        }
    }
    /* Stop (wait complete) pool. */
    c_ThreadPool_free(pool, WAIT_COMPLETE);

    printf("Example FINISH.\n");
    printf("%d / %d task executed.\n", count, ARRY_SIZE);
    printf("failed : %d / %d .\n", failed_count, ARRY_SIZE);
    printf("add sum total %d.", arr_elem_sum);
    free(arr);
    getchar();
}

static inline void promise_task(void *ptr);
//static inline void* async_task_func(void *ptr);
//static inline void async_free_task(void *ptr);
void future_example() {
    promise_t* promise = make_promise();
    /* promise - future */
    c_ThreadPool_st *pool = c_ThreadPool_init_pool(1);
    c_ThreadPool_add_task(pool, promise_task, promise);
    c_ThreadPool_free(pool, WAIT_COMPLETE);
    int* getFuture = (int*)get_future(promise);
    printf("promise - getFutue int=%d", (*getFuture));
    free(getFuture);

    /* async  - future */
    //int *data = malloc(sizeof(int));
    //*data = 512;
    //promise = async_futuer(promise_async, async_task_func, data, async_free_task);
    //getFuture = (int*)get_future(promise);
    //printf("promise - getFutue int=%d", (*getFuture));
    //free(getFuture);

    getchar();
}


static inline void show_task(void *ptr)
{
    struct array_elem* elment = (struct array_elem*)ptr;

    mtx_lock(&count_mutex);
    printf("%s %d.\n", elment->ptrStr, count);
    count++;
    mtx_unlock(&count_mutex);
}

static inline void add_task(void *ptr)
{
    struct array_elem* elment = (struct array_elem*)ptr;
    int *sum = (int*)(elment->sum);
    for (int i = 0; i < 1000; i++) {
        (elment->elm)++;
    }
    mtx_lock(elment->sum_mutex);
    *sum += elment->elm;
    mtx_unlock(elment->sum_mutex);

    mtx_lock(&count_mutex);
    count++;
    mtx_unlock(&count_mutex);
}

static inline void free_task(void *ptr) {
    struct array_elem* elment = (struct array_elem*)ptr;
    free((elment->ptrStr));
}

static inline void promise_task(void *ptr) {
    thrd_sleep(&(struct timespec) { .tv_sec = 1 }, NULL);
    printf("1sec sleep\n");
    promise_t* promise = (promise_t*)ptr;
    int *future = malloc(sizeof(int));
    *future = 256;
    set_promise(promise, future);
}

//static inline void* async_task_func(void *ptr) {
//    int *future = malloc(sizeof(int));
//    *future = *((int*)ptr) *2;
//    return future;
//}
//
//static inline void async_free_task(void *ptr) {
//    free(ptr);
//}