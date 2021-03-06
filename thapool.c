﻿/*
thapool.c
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
#ifndef THA_POOL_C_
#define THA_POOL_C_
#ifdef __cplusplus
extern "C"
{
#endif

#include "thapool.h"

#include <stdlib.h>
#include <stdio.h>


/* add queue */
struct c_ThreadPool_queue_
{
    c_ThreadPool_node*  head;
    c_ThreadPool_node*  tail;
    volatile uint32_t   size;
    mtx_t               q_mutex;
    cnd_t               cond;
    mtx_t               wait_mutex;
};

/* pop buffer */
typedef struct c_ThreadPool_Buffer_ {
    c_ThreadPool_node*      nodeBuffer[DEFAULT_BUFFER_SIZE];
    uint32_t                stock;
    uint32_t                index;
    mtx_t                   b_mutex;
}c_ThreadPool_Buffer;

typedef struct c_ThreadPool_thrd_t_{
	thrd_t              thread;
	c_ThreadPool_st*    parent;//TODO:親へのポインタ、空になったら終了する
}c_ThreadPool_thrd;


typedef struct c_ThreadPool_task_size_ {
    uint32_t    size;
    mtx_t       mutex;/* タスク操作ロック */
}c_ThreadPool_task_size;

struct c_ThreadPool_struct_ {

    c_ThreadPool_thrd   *threads;
    volatile uint32_t   num_of_threads;

    /* 追加キュー */
    c_ThreadPool_queue      add_queue;
    /* 取り出しバッファ */
    c_ThreadPool_Buffer     get_buffer;
    /* 終了してないタスクの数を管理させる */
    c_ThreadPool_task_size  task_size;
    

    volatile bool       isRunning;
    mtx_t               pool_mutex;//構造体ロック
};
#define C_THREADPOOL_TASK_COUNT_ADD(_task_size)     mtx_lock(&((_task_size).mutex));++((_task_size).size);mtx_unlock(&((_task_size).mutex));
#define C_THREADPOOL_TASK_COUNT_DEC(_task_size)     mtx_lock(&((_task_size).mutex));--((_task_size).size);mtx_unlock(&((_task_size).mutex));

/* prototype */

static inline int c_ThreadPool_loop(void *data);
static inline int c_ThreadPool_loop_stop_cb(void *ptr);
static inline c_ThreadPool_node* c_ThreadPool_get_task(c_ThreadPool_st *pool, bool isBlock);
static inline bool c_ThreadPool_check_Buffer(c_ThreadPool_Buffer *buf);
static inline void c_ThreadPool_charge_Buffer(c_ThreadPool_Buffer *buf, c_ThreadPool_queue *queue);
static inline c_ThreadPool_node* c_ThreadPool_get_Buffer_front(c_ThreadPool_Buffer *buf);
static inline c_ThreadPool_node* c_ThreadPool_queue_pop_front(c_ThreadPool_queue *queue);
static inline int c_ThreadPool_queue_push_back(c_ThreadPool_queue *queue, c_ThreadPool_node* node);


/* 初期化処理 */
HEDER_INLINE c_ThreadPool_st* c_ThreadPool_init_pool(uint32_t num_of_threads) {
    /* プール作成 */
    c_ThreadPool_st *pool = malloc(sizeof(c_ThreadPool_st));
    if (pool == NULL) {
        return NULL;
    }
    memset(pool, 0, sizeof(c_ThreadPool_st));
    pool->isRunning = true;
    /* mutex init */
    if (mtx_init(&(pool->pool_mutex), mtx_plain)) {
        goto ERROR_TAG;
    }
    if (mtx_init(&(pool->add_queue.q_mutex), mtx_recursive)) {
        goto ERROR_TAG;
    }
    if (mtx_init(&(pool->add_queue.wait_mutex), mtx_plain)) {
        goto ERROR_TAG;
    }
    if (mtx_init(&(pool->get_buffer.b_mutex), mtx_plain)) {
        goto ERROR_TAG;
    }
    if (mtx_init(&(pool->task_size.mutex), mtx_plain)) {
        goto ERROR_TAG;
    }
    /* threadプール(タスク領域)初期化 */
    /* cnd init */
    if (cnd_init(&(pool->add_queue.cond))) {
        goto ERROR_TAG;
    }
    /* 各スレッド生成 */
    pool->threads = malloc(sizeof(c_ThreadPool_thrd) * num_of_threads);
    if (pool->threads == NULL) {
        goto ERROR_TAG;
    }

    /* thread pool Start */
    for (pool->num_of_threads = 0; pool->num_of_threads < num_of_threads; ++(pool->num_of_threads)) {
        /* 所有してるプールを登録 */
        pool->threads[pool->num_of_threads].parent = pool;
        /* thread起動 */
        if (thrd_create(&(pool->threads[pool->num_of_threads].thread), c_ThreadPool_loop, &(pool->threads[pool->num_of_threads]))) {
            c_ThreadPool_free(pool, 0);
            return NULL;
        }
    }
    /* 起動まで成功したら生成したプール構造体返す */
    return pool;
ERROR_TAG:
    free(pool);
    return NULL;
}

// タスク追加
HEDER_INLINE int c_ThreadPool_add_task(c_ThreadPool_st *pool, c_pool_task *task_cb, void *data)
{
	return c_ThreadPool_add_task_ex(pool,task_cb,data,NULL);
}

HEDER_INLINE int c_ThreadPool_add_task_ex(c_ThreadPool_st *pool, c_pool_task *task_cb, void *data, c_pool_task *datafree_cb)
{
    int error = 0;
    if (pool == NULL) {
        return -1;
    }
    /* タスク完了待ち中は待機 */
    if (mtx_lock(&(pool->add_queue.wait_mutex))) {
        return -1;
    }
    /**
    * キューの生成
    */
    c_ThreadPool_node *node = malloc(sizeof(c_ThreadPool_node));
    if (node == NULL) {
        error = -1;
        goto ADD_TASK_ERROR;
    }
    /* ノードのタスクに追加 */
    node->data = data;
    node->task_cb = task_cb;
    node->datafree_cb = datafree_cb;
    
    int push_error = c_ThreadPool_queue_push_back(&(pool->add_queue), node);
    if (push_error) {
        error = push_error;
        goto ADD_TASK_ERROR;
    }
    if (cnd_signal(&(pool->add_queue.cond))) {
        error = -2;
        goto ADD_TASK_ERROR;
    }
    C_THREADPOOL_TASK_COUNT_ADD(pool->task_size);
    mtx_unlock(&(pool->add_queue.wait_mutex));
    return 0;
ADD_TASK_ERROR:
    mtx_unlock(&(pool->add_queue.wait_mutex));
    return error;
}

// wait(タスク完了待ち)
HEDER_INLINE bool c_ThreadPool_waitTaskComplete(c_ThreadPool_st *pool) {
    if (mtx_lock(&(pool->add_queue.wait_mutex))) {
        return false;
    }
    bool empty=false;
    while (pool->isRunning) {
        mtx_lock(&(pool->task_size.mutex));
        empty = pool->task_size.size == 0;
        mtx_unlock(&(pool->task_size.mutex));
        if (empty) {
            break;
        }
    }
    mtx_unlock(&(pool->add_queue.wait_mutex));
    return empty != false;
}

HEDER_INLINE c_ThreadPool_node* c_ThreadPool_get_next_task(c_ThreadPool_st *pool)
{
    return c_ThreadPool_get_task(pool, false);
}


// 開放処理
HEDER_INLINE void c_ThreadPool_free(c_ThreadPool_st *pool, int stop_mode)
{
    if (stop_mode & c_ThreadPool_wait_complete) {
        c_ThreadPool_waitTaskComplete(pool);
    }
    if (stop_mode & c_ThreadPool_blocking) {
        c_ThreadPool_loop_stop_cb(pool);
    }
    else {
        thrd_t thr;
        if (thrd_create(&thr, c_ThreadPool_loop_stop_cb, pool)) {
            pool->isRunning = false;
        }
        thrd_detach(thr);
    }
}

/**
* スレッドプールのタスクプール
*
* @param c_ThreadPool_st*
* @return 0
*/
static inline int c_ThreadPool_loop(void *data)
{
    c_ThreadPool_node *task;
    c_ThreadPool_thrd *thrd = (c_ThreadPool_thrd*)data;
    c_ThreadPool_st* pool = thrd->parent;
    while (pool->isRunning && thrd->parent) {
        task = c_ThreadPool_get_task(pool, true);
        if (!task) {
            break;
        }
        if (task->task_cb) {
            task->task_cb(task->data);
        }
        if(task->datafree_cb){
    	    task->datafree_cb(task->data);
    	}
        free(task);
    }
    //TODO:parent空なら？スレッド開放
    return 0;
}

/**
* ワーカータスクプールの停止処理
* @param ptr Pool to stop worker thread
* @return 0
*/
static inline int c_ThreadPool_loop_stop_cb(void *ptr)
{
    c_ThreadPool_st *pool = (c_ThreadPool_st*)ptr;
    if (mtx_lock(&(pool->pool_mutex))) {
        return 0;
    }
    pool->isRunning = false;

    if (cnd_broadcast(&(pool->add_queue.cond))) {
        return 0;
    }
    if (mtx_unlock(&(pool->pool_mutex))) {
        return 0;
    }
    UINT32 nowTh = pool->num_of_threads;
    for (UINT32 i = 0; i < nowTh; i++) {
        thrd_join(pool->threads[i].thread, NULL);
        --(pool->num_of_threads);
    }
    c_ThreadPool_node* task = NULL;
    while (task = c_ThreadPool_get_task(pool, false)) {
    	if(task->datafree_cb){
    	    task->datafree_cb(task->data);
    	}
        free(task);
    }
    free(pool->threads);
    free(pool);

    return 0;
}

/**
* pop queue
*
* @param    queue pointer
* @return   c_ThreadPool_node* node or NULL 
*/
static inline c_ThreadPool_node* c_ThreadPool_queue_pop_front(c_ThreadPool_queue *queue)
{
    c_ThreadPool_node *head = NULL;
    if (queue->size == 0) {
        return head;
    }
    head = queue->head;
    queue->head = head->next;
    if (--(queue->size) == 0) {
        queue->head = queue->tail = NULL;
    }
    return head;
}

/* push queue */
static inline int c_ThreadPool_queue_push_back(c_ThreadPool_queue *queue, c_ThreadPool_node* node)
{
    if (mtx_lock(&(queue->q_mutex))) {
        return -1;
    }
    /* ノードの結びつけ */
    if (!queue->head) {
        queue->head = node;
        node->next = NULL;
    }
    else {
        queue->tail->next = node;
    }
    queue->tail = node;
    ++(queue->size);
    if (mtx_unlock(&(queue->q_mutex))) {
        return -2;
    }
    return 0;
}

/**
* Get c_ThreadPool_task
* @param c_ThreadPool_st
* @return c_ThreadPool_task* or NULL
*/
static inline c_ThreadPool_node* c_ThreadPool_get_task(c_ThreadPool_st *pool, bool isBlock)
{
    if (mtx_lock(&(pool->get_buffer.b_mutex))) {
        return NULL;
    }
    /* Bufferの取得が必要かのチェック */
    if (c_ThreadPool_check_Buffer(&(pool->get_buffer))) {
        if (mtx_lock(&(pool->add_queue.q_mutex))) {
            goto UNLOCK_GET_TASK;
        }
        /* pool->num_of_queue empty loop */
        while (pool->add_queue.size == 0) {
            if (!pool->isRunning) {
                mtx_unlock(&(pool->add_queue.q_mutex));
                cnd_broadcast(&(pool->add_queue.cond));
                goto UNLOCK_GET_TASK;
            }
            if (isBlock == false) {
                mtx_unlock(&(pool->add_queue.q_mutex));
                goto UNLOCK_GET_TASK;
            }
            /* Block until a new task comes in */
            if (cnd_wait(&(pool->add_queue.cond), &(pool->add_queue.q_mutex))) {
                mtx_unlock(&(pool->add_queue.q_mutex));
                goto UNLOCK_GET_TASK;
            }
        }
        /* キューからBufferにデータ移動 */
        c_ThreadPool_charge_Buffer(&(pool->get_buffer), &(pool->add_queue));
        mtx_unlock(&(pool->add_queue.q_mutex));
    }
    /* Bufferから取り出し */
    c_ThreadPool_node* task = c_ThreadPool_get_Buffer_front(&(pool->get_buffer));
    mtx_unlock(&(pool->get_buffer.b_mutex));
    C_THREADPOOL_TASK_COUNT_DEC(pool->task_size);
    return task;
UNLOCK_GET_TASK:
    mtx_unlock(&(pool->get_buffer.b_mutex));
    return NULL;
}

/* Bufferの取得が必要かのチェック */
static inline bool c_ThreadPool_check_Buffer(c_ThreadPool_Buffer *buf) {
    return  buf->index == buf->stock;
}

/* キューからBufferにデータ移動 */
static inline void c_ThreadPool_charge_Buffer(c_ThreadPool_Buffer *buf, c_ThreadPool_queue *queue)
{
    buf->stock = buf->index = 0;
    c_ThreadPool_node* getNode;
    do
    {
        getNode = c_ThreadPool_queue_pop_front(queue);
        if (!getNode) {
            break;
        }
        buf->nodeBuffer[(buf->stock)++] = getNode;
    } while (buf->stock < DEFAULT_BUFFER_SIZE);
}

/* BUFFERからデータ取得 */
static inline c_ThreadPool_node* c_ThreadPool_get_Buffer_front(c_ThreadPool_Buffer *buf)
{
    return buf->nodeBuffer[(buf->index)++];
}

enum {
    promise_non = 0,
    promise_finish = promise_deferred + 1
};


struct promise_object_
{
    mtx_t                   future_mutex;
    cnd_t                   cond;
    volatile promise_state  state;  //
    async_task              *callbackFunc;  //処理関数
    c_pool_task             *datafree_cb;   //開放処理
    c_ThreadPool_st         *pool;
    void                    *data;          //引数データ
    void                    *result;        //結果
};


/**
promise(生成)処理
*/
HEDER_INLINE promise_t* make_promise()
{
    promise_t* n_futuer = malloc(sizeof(promise_t));
    n_futuer->callbackFunc = NULL;
    n_futuer->data = NULL;
    n_futuer->result = NULL;
    n_futuer->pool = NULL;
    n_futuer->state = promise_non;
    if (mtx_init(&(n_futuer->future_mutex), mtx_plain)) {
        free(n_futuer);
        return NULL;
    }
    if (cnd_init(&(n_futuer->cond))) {
        free(n_futuer);
        return NULL;
    }
    return n_futuer;
}
/**
promise(set)処理
*/
HEDER_INLINE int set_promise(promise_t* n_futuer, void *result) {
    if (mtx_lock(&(n_futuer->future_mutex))) {
        return 0;
    }
    n_futuer->state = promise_finish;
    n_futuer->result = result;
    cnd_signal(&n_futuer->cond);
    mtx_unlock(&(n_futuer->future_mutex));
    return 1;
}

// async内部処理
static inline int asyncFunc(void *data)
{
    if (data == NULL) {
        return 0;
    }
    promise_t* n_futuer = (promise_t*)data;
    void *result = n_futuer->callbackFunc(n_futuer->data);
    if (!set_promise(n_futuer, result)) {
    }
    if (n_futuer->datafree_cb) {
        n_futuer->datafree_cb(n_futuer->data);
    }
    return 0;
}

/**
async処理
*/
HEDER_INLINE promise_t* async_futuer(promise_state state, async_task *routine, void *data, c_pool_task *datafree_cb)
{
    if (!routine) {
        return NULL;
    }
    promise_t* n_futuer = NULL;
    if (state == promise_deferred) {
        n_futuer = make_promise();
        n_futuer->callbackFunc = routine;
        n_futuer->data = data;
    }
    else {
        c_ThreadPool_st *pool = c_ThreadPool_init_pool(1);
        n_futuer = async_pool(pool, routine, data, datafree_cb);
        n_futuer->pool = pool;
    }
    return n_futuer;
}

/**
async(pool)処理
*/
HEDER_INLINE promise_t* async_pool(c_ThreadPool_st *pool, async_task *routine, void *data, c_pool_task *datafree_cb)
{
    if (pool == NULL || !routine) {
        return NULL;
    }
    promise_t* n_futuer = make_promise();
    n_futuer->callbackFunc = routine;
    n_futuer->data = data;
    n_futuer->datafree_cb = datafree_cb;
    c_ThreadPool_add_task(pool, asyncFunc, n_futuer);
    return n_futuer;
}



/**
future処理
*/
HEDER_INLINE void* get_future(promise_t* n_futuer)
{
    void* result = NULL;
    if (n_futuer->state == promise_deferred) {
        // 遅延実行
        result = n_futuer->callbackFunc(n_futuer->data);
    }
    else {
    	mtx_lock(&(n_futuer->future_mutex));
        //別スレッドからの非同期実行
        while (n_futuer->state != promise_finish)
        {
           cnd_wait(&n_futuer->cond, &n_futuer->future_mutex);
        }
        result = n_futuer->result;
        mtx_unlock(&(n_futuer->future_mutex));
        if (n_futuer->pool) {
            c_ThreadPool_free(n_futuer->pool, WAIT_COMPLETE);
        }
    }
    mtx_destroy(&n_futuer->future_mutex);
    cnd_destroy(&n_futuer->cond);
    free(n_futuer);
    return result;
}

/**
TODO:並列for処理
*/



#ifdef __cplusplus
}
#endif
#endif // !THA_POOL_C_
