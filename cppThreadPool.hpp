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
    std::shared_ptr<c_ThreadPool_st> _thPool;
    //std::shared_ptr<std::set<MyFunc_st> > _myfuncst;
    bool    _freeBlock;
public:
    cppThreadPool():_freeBlock(c_ThreadPool_wait_complete) {}
    cppThreadPool(uint32_t thCnt):_freeBlock(c_ThreadPool_wait_complete) { initPool(thCnt); }
    ~cppThreadPool() { _thPool.reset(); }

    //bool addTask(_F func) { return addTaskFunc([func](void*){ func(); },(void*)NULL); }
    template<class _F, class _P>
    bool addTask(_F func, _P* data) { return addTaskFunc(func,data); }

    bool initPool(uint32_t thCnt) {
        bool& fBlock = _freeBlock;
        _thPool.reset(c_ThreadPool_init_pool(thCnt),
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
    template<class _F, class _P>
    bool addTaskFunc(_F func, void* data) {
        MyFunc_st* fst = new MyFunc_st<_P>;
        fst->func = func;
        fst->data = data;
        int ret = c_ThreadPool_add_task(_thPool.get(), cppThreadPool::lamdaCall<_P>, fst);
        return ret == 0;
    }

    template<class _P>
    static void lamdaCall(void* data) {
        auto fSt = static_cast<MyFunc_st<_P>*>(data);
        if (fSt) {
            fSt->func(fSt->data);
        }
        delete fSt;
    }
};

//}


#endif /* CPP_THAPOOL_H_ */
