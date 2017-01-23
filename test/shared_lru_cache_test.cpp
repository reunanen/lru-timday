#include "../shared_lru_cache_using_std.h"
#include <unordered_map>
#include <iostream>
#include <iomanip>

typedef shared_lru_cache_using_std<int, uint64_t, std::unordered_map> cache;

uint64_t fibonacci(int x)
{
    uint64_t a = 1;
    uint64_t b = 1;
    uint64_t sum = 1;
    for (int i = 1; i < x; ++i) {
        sum = a + b;
        b = a;
        a = sum;
        //std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return sum;
}

uint64_t repeated_fibonacci(int x)
{
    // spend some time here first...
#ifdef _DEBUG
    const int repeat_count = 100;
#else
    const int repeat_count = 1000000;
#endif

    std::vector<uint64_t> results;

    for (int i = 0; i < repeat_count; ++i) {
        results.push_back(fibonacci(x));
    }

    return fibonacci(x);
}

void calculate(int n, cache* cache)
{
    for (int i = 1; i <= 10 * n; ++i) {
        const int x = i * n;
        const auto result = (*cache)(x);
        assert(result == fibonacci(x));
    }
}

int main(int argc, char* argv[])
{
    std::cout << "Let's spend some system resources..." << std::endl;

    cache cache(repeated_fibonacci, 10);

    const int thread_count = 100;
    std::vector<std::thread> threads;

    for (int i = 1; i <= thread_count; ++i) {
        std::thread thread(calculate, i, &cache);
        threads.push_back(std::move(thread));
    }

    for (int i = 1; i <= thread_count; ++i) {
        threads[i - 1].join();
        std::cout << "\r" << (i * 100 / thread_count) << " %";
    }

    std::cout << std::endl;

	return 0;
}

