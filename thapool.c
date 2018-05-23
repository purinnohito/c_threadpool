
#include "thapool.h"

#include <stdlib.h>
#include <stdio.h>



struct c_ThreadPool_node_ 
{
    c_ThreadPool_node *forward;
    c_ThreadPool_node *next;

    c_ThreadPool_task *task;

    volatile bool     dynamic_alloc;
};

struct c_ThreadPool_buffer_
{
    //Bufferはメモリ確保を減らすため固定エリア＋溢れたら動的確保
    //c_ThreadPool_node   queue_fixed[DEFAULT_POOL_SIZE];
    c_ThreadPool_node   *queue_head;
    c_ThreadPool_node   *queue_tail;
    volatile UINT32     num_of_queue;
};

typedef struct c_ThreadPool_thrd_t_{
	thrd_t              thread;
	c_ThreadPool_st*    parent;//親へのポインタ、空になったら終了する
}c_ThreadPool_thrd;


struct c_ThreadPool_struct_ {

    c_ThreadPool_thrd   *threads;
    volatile UINT32     num_of_threads;

    //Bufferはメモリ確保を減らすため固定エリア＋溢れたら動的確保
    c_ThreadPool_buffer queue;

    volatile bool       isRunning;
    cnd_t               queue_cnd;
    mtx_t               queue_mutex;
};

struct task_c_ThreadPool_
{
    c_pool_task     *task_cb;
    void            *data;
};
#define C_THREADPOOL_TASK_INIT(task)    ((task)->data = (task)->task_cb = NULL)

/* prototype */

static inline int c_ThreadPool_loop(void *data);
static inline int c_ThreadPool_loop_stop_cb(void *ptr);
static inline c_ThreadPool_node *c_ThreadPool_get_node(c_ThreadPool_buffer *queue);
static inline c_ThreadPool_task* c_ThreadPool_get_task(c_ThreadPool_st *pool);
static inline void *c_ThreadPool_queue_pop_front(c_ThreadPool_buffer *queue);


// 初期化処理
c_ThreadPool_st* c_ThreadPool_init(UINT32 num_of_threads) {
    /* プール作成 */
    c_ThreadPool_st *pool = malloc(sizeof(c_ThreadPool_st));
    if (pool == NULL) {
        return NULL;
    }
    /* mutex init */
    if (mtx_init(&(pool->queue_mutex), mtx_plain)) {
        goto ERROR_TAG;
    }
    /* cnd init */
    if (cnd_init(&(pool->queue_cnd))) {
        goto ERROR_TAG;
    }
    /* threadプール(タスク領域)初期化 */
    memset(&pool->queue, 0, sizeof(pool->queue));
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
    //// 最初にc_ThreadPool_bufferをDEFAULT_POOL_SIZE個+st_Refcounterの領域確保
    //st_Refcounter* refPtr = refAlloc(sizeof(c_ThreadPool_buffer)*DEFAULT_POOL_SIZE)
ERROR_TAG:
    free(pool);
    return NULL;
}

// タスク追加
int c_ThreadPool_add_task(c_ThreadPool_st *pool, c_pool_task *task_cb, void *data)
{
    if (pool == NULL) {
        return -1;
    }
    if (mtx_lock(&(pool->queue_mutex))) {
        return -1;
    }

    // タスク構造体の取得(Bufferからor新規作成)
    c_ThreadPool_node *node = c_ThreadPool_get_node(&(pool->queue));
    if (node == NULL) {
        mtx_unlock(&(pool->queue_mutex));
        return -1;
    }
    /* ノードのタスクに追加 */
    node->task = malloc(sizeof(c_ThreadPool_task));
    if (node->task == NULL) {
        --(pool->queue.num_of_queue);
        if (node->dynamic_alloc) {
            free(node);
        }
        return -1;
    }
    node->task->data = data;
    node->task->task_cb = task_cb;

    /* ノードの結びつけ */
    if ((pool->queue.num_of_queue)++ == 0) {
        pool->queue.queue_head = pool->queue.queue_tail = node;
        if (cnd_broadcast(&(pool->queue_cnd))) {
            mtx_unlock(&(pool->queue_mutex));
            return -1;
        }
    }
    else {
        node->forward = pool->queue.queue_tail;
        pool->queue.queue_tail = node;
        pool->queue.queue_tail->forward->next = pool->queue.queue_tail;
    }
    if (mtx_unlock(&(pool->queue_mutex))) {
        --(pool->queue.num_of_queue);
        free(node->task);
        if (node->dynamic_alloc) {
            free(node);
        }
        return -1;
    }
    return 0;
}

// 開放処理
void c_ThreadPool_free(c_ThreadPool_st *pool, bool blocking)
{
    if (blocking) {
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
    c_ThreadPool_task *task;
    c_ThreadPool_thrd *thrd = (c_ThreadPool_thrd*)data;
    c_ThreadPool_st* pool = thrd->parent;
    while (pool->isRunning && thrd->parent) {
        task = c_ThreadPool_get_task(pool);
        if (!task) {
            break;
        }
        if (task->task_cb) {
            task->task_cb(task->data);
            C_THREADPOOL_TASK_INIT(task);
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
    if (mtx_lock(&(pool->queue_mutex))) {
        return 0;
    }
    pool->isRunning = false;

    if (cnd_broadcast(&(pool->queue_cnd))) {
        return 0;
    }
    if (mtx_unlock(&(pool->queue_mutex))) {
        return 0;
    }
    UINT32 nowTh = pool->num_of_threads;
    for (UINT32 i = 0; i < nowTh; i++) {
        thrd_join(pool->threads[i].thread, NULL);
        --pool->num_of_threads;
    }
    while (pool->queue.num_of_queue) {
        c_ThreadPool_task* task = (c_ThreadPool_task*)c_ThreadPool_queue_pop_front(&pool->queue);
        free(task);
        //if (cnd_wait(&(pool->queue_cnd), &(pool->queue_mutex))) {
        //    mtx_unlock(&(pool->queue_mutex));
        //    return 0;
        //}
    }
    free(pool->threads);
    free(pool);

    return 0;
}

/**
* キューの生成or取得
*/
static inline c_ThreadPool_node *c_ThreadPool_get_node(c_ThreadPool_buffer *queue)
{
    ///* キューが空なら配列の先頭を割り当てる */
    //if (queue->num_of_queue == 0) {
    //    return &queue->queue_fixed[0];
    //}
    ///* 末尾ポインタが動的確保でなければ、横が開いてるかをチェック */
    //if (!queue->queue_tail->dynamic_alloc &&
    //    queue->queue_tail >= &queue->queue_fixed[0] &&
    //    queue->queue_tail < &queue->queue_fixed[DEFAULT_POOL_SIZE-1] &&
    //    (queue->queue_tail + sizeof(c_ThreadPool_node))->task == NULL) {
    //    /* 横があいていれば使う */
    //    return (queue->queue_tail + sizeof(c_ThreadPool_node));
    //}
    ///* 先頭ポインタが動的確保でなければ、手前が開いてるかをチェック */
    //if (!queue->queue_head->dynamic_alloc &&
    //    queue->queue_head > &queue->queue_fixed[0] &&
    //    queue->queue_head <= &queue->queue_fixed[DEFAULT_POOL_SIZE - 1] &&
    //    (queue->queue_head - sizeof(c_ThreadPool_node))->task == NULL) {
    //    /* 手前があいていれば使う */
    //    return (queue->queue_head - sizeof(c_ThreadPool_node));
    //}
    ///* 配列の先頭チェック(空いてれば使う) */
    //if (queue->queue_fixed[0].task == NULL) {
    //    return &queue->queue_fixed[0];
    //}
    ///* 配列の中間チェック(空いてれば使う) */
    //if (queue->queue_fixed[DEFAULT_POOL_SIZE/2].task == NULL) {
    //    return &queue->queue_fixed[DEFAULT_POOL_SIZE / 2];
    //}
    /* 空きスペースが検索できなければmalloc */
    c_ThreadPool_node* data = malloc(sizeof(c_ThreadPool_node));
    data->dynamic_alloc = true;
    return data;
}

/**
* pop front queue
*
* @ param queue ptr
* @ return task, or NULL is returned on failure.
*/
static inline void *c_ThreadPool_queue_pop_front(c_ThreadPool_buffer *queue)
{
    if (queue->num_of_queue == 0) {
        return NULL;
    }
    void *data = queue->queue_head->task;
    if (queue->num_of_queue > 1) {
        c_ThreadPool_node *next = queue->queue_head->next;
        // dynamic_allocが有る(動的確保)なら開放
        if (queue->queue_head->dynamic_alloc) {
            free(queue->queue_head);
        }
        else {
            queue->queue_head->task = NULL;
        }
        queue->queue_head = next;
        queue->queue_head->forward = NULL;
    }

    if (--(queue->num_of_queue) == 0) {
        queue->queue_head = NULL;
        queue->queue_tail = NULL;
    }

    return data;
}

/**
* Get c_ThreadPool_task
* @param c_ThreadPool_st
* @return c_ThreadPool_task* or NULL
*/
static inline c_ThreadPool_task* c_ThreadPool_get_task(c_ThreadPool_st *pool)
{
    if (mtx_lock(&(pool->queue_mutex))) {
        return NULL;
    }
    /* pool->num_of_queue empty loop */
    while (!pool->queue.num_of_queue) {
        if (!pool->isRunning)
        {
            cnd_broadcast(&(pool->queue_cnd));
            mtx_unlock(&(pool->queue_mutex));
            return NULL;
        }
        /* Block until a new task comes in */
        if (cnd_wait(&(pool->queue_cnd), &(pool->queue_mutex))) {
            mtx_unlock(&(pool->queue_mutex));
            return NULL;
        }
    }
    c_ThreadPool_task* task = (c_ThreadPool_task*)c_ThreadPool_queue_pop_front(&(pool->queue));
    mtx_unlock(&(pool->queue_mutex));

    return task;
}


/**
promise(生成)処理
*/
promise_t* make_promise()
{
    promise_t* n_futuer = malloc(sizeof(promise_t));
    n_futuer->callbackFunc = NULL;
    n_futuer->data = NULL;
    n_futuer->result = NULL;
    n_futuer->state = 0;
    if (mtx_init(&(n_futuer->future_mutex), mtx_plain)) {
        free(n_futuer);
        return NULL;
    }
    return n_futuer;
}
/**
promise(set)処理
*/
int set_promise(promise_t* n_futuer, void *result) {
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
        return 0;
    }
    return 0;
}

/**
async処理
*/
promise_t* async_futuer(int state, async_task *routine, void *data)
{
    if (!routine) {
        return NULL;
    }
    promise_t* n_futuer = make_promise();
    n_futuer->callbackFunc = routine;
    n_futuer->data = data;
    n_futuer->state = state;
    if (n_futuer->state == promise_async) {
        thrd_t thr_det;
        if (thrd_create(&thr_det, asyncFunc, n_futuer)) {
            free(n_futuer);
            return NULL;
        }
        thrd_detach(&thr_det);
    }
    return n_futuer;
}

/**
async(pool)処理
*/
promise_t* async_pool(c_ThreadPool_st *pool, async_task *routine, void *data, int blocking)
{
    if (pool == NULL || !routine) {
        return NULL;
    }
    promise_t* n_futuer = make_promise();
    n_futuer->callbackFunc = routine;
    n_futuer->data = data;
    c_ThreadPool_add_task(pool, asyncFunc, n_futuer);
    return n_futuer;
}



/**
future処理
最適化禁止マーク必要
*/
void* get_future(promise_t* n_futuer)
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
    }
    mtx_destroy(&n_futuer->future_mutex);
    cnd_destroy(&n_futuer->cond);
    free(n_futuer);
    return result;
}

/**
TODO:並列for処理
*/
