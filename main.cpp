extern "C" {
    extern void c_threadpool_example();
    extern void future_example();
}
extern void cpp_threadpool_example();

int main()
{
    c_threadpool_example();
    future_example();
    cpp_threadpool_example();
    return 0;
}

