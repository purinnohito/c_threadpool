#ifndef THAPOOL_H_
#define THAPOOL_H_

#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "threads.h"
#include <stdbool.h>


typedef struct c_ThreadPool_node_ c_ThreadPool_node;
typedef struct c_ThreadPool_buffer_ c_ThreadPool_buffer;
typedef struct c_ThreadPool_struct_ c_ThreadPool_st;
typedef struct task_c_ThreadPool_ c_ThreadPool_task;
typedef void(c_pool_task)(void*);
typedef void*(async_task)(void*);
#define DEFAULT_POOL_SIZE 500

/**
* Create a newly allocated thread pool.
*/
c_ThreadPool_st* c_ThreadPool_init(UINT32 num_of_threads);

/**
* Add routines to be executed by the thread pool.

*/
int c_ThreadPool_add_task(c_ThreadPool_st *pool, c_pool_task *task_cb, void *data);

/**
* Stop all worker threads(stop and exit).Free all allocated memory.
* Blocking!= 0, calling this function will block until all worker threads are terminated.
*/
void c_ThreadPool_free(c_ThreadPool_st *pool, bool blocking);

enum promise_state {
    promise_non,
    promise_finish,
    promise_async,
    promise_deferred
};

typedef struct promise_object_
{
    mtx_t future_mutex;
    cnd_t cond;
    volatile int    state;  //
    async_task *callbackFunc;   //処理関数
    void *data;                 //引数データ
    void *result;               //結果
}promise_t;

/**
promise(生成)処理
*/
promise_t* make_promise();
/**
promise(set)処理
*/
int set_promise(promise_t* n_futuer, void *result);

/**
async処理
*/
promise_t* async_futuer(int state, async_task *routine, void *data);

/**
async(pool)処理
*/
promise_t* async_pool(c_ThreadPool_st *pool, async_task *routine, void *data, int blocking);



/**
future処理
*/
void* get_future(promise_t* n_futuer);

/**
TODO:並列for処理
*/


#ifdef __cplusplus
}
#endif
#endif /* THAPOOL_H_ */
