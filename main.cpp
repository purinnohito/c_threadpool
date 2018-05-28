extern "C" {
    extern void c_threadpool_example();
    extern void future_example();
}

int main()
{
    c_threadpool_example();
    future_example();
    return 0;
}
