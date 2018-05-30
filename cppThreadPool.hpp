/*
cppThreadPool.hpp
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
#ifndef CPP_THAPOOL_H_
#define CPP_THAPOOL_H_

#include "thapool.h"
#include <memory>
#include <functional>
#include <set>


//namespace cpp_pool {

class cppThreadPool {
    typedef std::unique_ptr<c_ThreadPool_st, std::function<void(c_ThreadPool_st*)> > PoolPtr;
    PoolPtr _thPool;
    int    _freeBlock;
public:
    cppThreadPool() :_freeBlock(c_ThreadPool_wait_complete) {}
    cppThreadPool(uint32_t thCnt) :_freeBlock(c_ThreadPool_wait_complete) { initPool(thCnt); }
    ~cppThreadPool() { _thPool.reset(); }

    template<class _F>
    bool addTask(_F func) { return addTaskFunc([func](void*) { func(); }, (void*)NULL); }
    template<class _F, class _P>
    bool addTask(_F func, _P* data) { return addTaskFunc(func, data); }

    bool  WaitAllComplete() {
        return c_ThreadPool_waitTaskComplete(_thPool.get());
    }
    typedef std::function<void(c_ThreadPool_node*)>    node_delegate;
    typedef std::unique_ptr<c_ThreadPool_node, node_delegate> unque_node_ptr;
    unque_node_ptr getNextTask() {
        return unque_node_ptr(c_ThreadPool_get_next_task(_thPool.get()),
            node_delegate([](c_ThreadPool_node* task) {
            if (task) {
                if (task->datafree_cb) {
                    task->datafree_cb(task->data);
                }
                free(task);
            }
        }));
    }
    //TODO:join
    //TODO:threads
    //TODO:get_thPool
    //TODO:async pool

    bool initPool(uint32_t thCnt) {
        auto& fBlock = _freeBlock;
        _thPool = PoolPtr(c_ThreadPool_init_pool(thCnt),
            [&fBlock](c_ThreadPool_st* pPool) {
            c_ThreadPool_free(pPool, fBlock);
        });
        return static_cast<bool>(_thPool);
    }
private:
    template<class _P>
    struct MyFunc_st
    {
        std::function<void(_P*)> func;
        _P* data;
    };
    template<class _F, class _P >
    bool addTaskFunc(_F func, _P* data) {
        auto fst = new MyFunc_st<_P>;
        fst->func = std::function<void(_P*)>(func);
        fst->data = data;
        int ret = c_ThreadPool_add_task_ex(_thPool.get(), cppThreadPool::lamdaCall<_P>, fst, cppThreadPool::delCall<_P>);
        return ret == 0;
    }

    template<class _P>
    static void lamdaCall(void* data) {
        auto fSt = static_cast<MyFunc_st<_P>*>(data);
        if (fSt) {
            fSt->func(fSt->data);
        }
    }
    template<class _P>
    static void delCall(void* data) {
        auto fSt = static_cast<MyFunc_st<_P>*>(data);
        delete fSt;
    }
};

//}


#endif /* CPP_THAPOOL_H_ */
