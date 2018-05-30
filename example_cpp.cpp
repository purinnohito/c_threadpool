#include "cppThreadPool.hpp"
#include <vector>
#include <string>

#define ARRY_SIZE 100000

void cpp_threadpool_example()
{
    cppThreadPool cppPool;
    int count = 0;
    mtx_t count_mutex;
    mtx_t  sum_mutex;
    std::vector<int> arr;
    int sum = 0;

    int arr_elem_sum = 0;
    c_ThreadPool_st *pool;

    bool ret=true, ret2=true;
    int failed_count = 0;

    mtx_init(&count_mutex, mtx_plain);
    mtx_init(&(sum_mutex), mtx_plain);

    mtx_init(&(sum_mutex), mtx_plain);
    for (int i = 0; i < ARRY_SIZE; i++) {
        arr.push_back(i);
    }

    /* Create a thapool of 10 thread workers. */
    cppPool.initPool(10);
    std::string ptrStr("task count =");

    for (int i = 0; i < ARRY_SIZE; i++) {
        if (i % 10000 == 0) {
            /* add free func. */
            ret = cppPool.addTask([&, i](std::string* str) {
                mtx_lock(&count_mutex);
                printf("%s %d.\n", ((std::string*)str)->c_str(), count);
                count++;
                mtx_unlock(&count_mutex);
            }, &ptrStr);
        }
        else {
            /* add. */
            ret2 = cppPool.addTask([&,i] {
                mtx_lock(&sum_mutex);
                sum += arr[i];
                mtx_unlock(&sum_mutex);

                mtx_lock(&count_mutex);
                count++;
                mtx_unlock(&count_mutex);
            });
        }

        if (!ret) {
            printf("An error had occurred while adding a task.");
           // exit(EXIT_FAILURE);
        }

        if (!ret2) {
            failed_count++;
        }
    }
    /* Stop (wait complete) pool. */
   // c_ThreadPool_free(pool, WAIT_COMPLETE);

    printf("Example FINISH.\n");
    printf("%d / %d task executed.\n", count, ARRY_SIZE);
    printf("failed : %d / %d .\n", failed_count, ARRY_SIZE);
    printf("add sum total %d.", arr_elem_sum);
    getchar();
}
