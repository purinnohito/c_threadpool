/*
thapool.h
- ThreadPool & future promis

Written in 2018/05/26 by purinnohito

To the extent possible under law, the author(s)
have dedicated all copyright and related and neighboring
rights to this software to the public domain worldwide.
This software is distributed without any warranty.

You should have received a copy of the CC0 Public Domain
Dedication along with this software.If not, see
<https://creativecommons.org/publicdomain/zero/1.0/deed.ja>.
*/
#ifndef THAPOOL_H_
#define THAPOOL_H_

//TODO:ヘッダオンリーモード用定数判定
#ifdef THAPOOL_HEADER_ONLY
#define HEDER_INLINE    static inline
#else
#define HEDER_INLINE 
#endif // !THAPOOL_HEADER_ONLY


#include <stdint.h>
#ifdef __cplusplus
extern "C"
{
#endif
#include "threads.h"
#include <stdbool.h>


//TODO:ヘッダオンリーモードで公開関数にstatic inlineつける定数宣言

typedef struct c_ThreadPool_node_ c_ThreadPool_node;
typedef struct c_ThreadPool_queue_  c_ThreadPool_queue;
typedef struct c_ThreadPool_struct_ c_ThreadPool_st;
typedef void(c_pool_task)(void*);
typedef void*(async_task)(void*);
#define DEFAULT_BUFFER_SIZE 1000

/**
* Create a newly allocated thread pool.
*/
HEDER_INLINE c_ThreadPool_st* c_ThreadPool_init_pool(uint32_t num_of_threads);

/**
* Add routines to be executed by the thread pool.

*/
HEDER_INLINE int c_ThreadPool_add_task(c_ThreadPool_st *pool, c_pool_task *task_cb, void *data);

/* ALL task join */
HEDER_INLINE bool c_ThreadPool_waitTaskComplete(c_ThreadPool_st *pool);

enum c_ThreadPool_stop_mode {
    c_ThreadPool_noBlocking = 0,
    c_ThreadPool_blocking = 1,
    c_ThreadPool_wait_complete = 2 | c_ThreadPool_blocking
};
#define NO_BLOCKING         c_ThreadPool_noBlocking
#define BLOCKING            c_ThreadPool_blocking
#define WAIT_COMPLETE       c_ThreadPool_wait_complete
/**
* Stop all worker threads(stop and exit).Free all allocated memory.
* Blocking!= 0, calling this function will block until all worker threads are terminated.
*/
HEDER_INLINE void c_ThreadPool_free(c_ThreadPool_st *pool, int stop_mode);

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
HEDER_INLINE int set_promise(promise_t* n_futuer, void *result);

/**
async処理
*/
HEDER_INLINE promise_t* async_futuer(int state, async_task *routine, void *data);

/**
async(pool)処理
*/
HEDER_INLINE promise_t* async_pool(c_ThreadPool_st *pool, async_task *routine, void *data, int blocking);



/**
future処理
*/
HEDER_INLINE void* get_future(promise_t* n_futuer);

/**
TODO:並列for処理
*/


#ifdef THAPOOL_HEADER_ONLY
#include "thapool.c"
#endif // !THAPOOL_HEADER_ONLY


#ifdef __cplusplus
}
#endif
#endif /* THAPOOL_H_ */
