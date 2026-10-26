#pragma once
#include <chrono>

struct BenchmarkResult {
    long long iterations;
    double took_seconds;
    double iterations_per_second;
    double ms_per_iteration;
};

// measures how much it takes to execute fun()
template <typename F>
long long MeasureTime(F fun) {
    auto start = std::chrono::high_resolution_clock::now();
    
    fun();
    
    auto end = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
}

// measures how many times fun() can be executed in max_seconds
template <typename F>
BenchmarkResult MeasureIterations(double max_seconds, F fun) {
    auto max_duration = std::chrono::duration<double>(max_seconds);
    
    long long iterations = 0;
    auto start = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed;

    const long long BATCH = 1024; 

    while (true) {
        for (long long i = 0; i < BATCH; ++i) {
            fun();
        }
        iterations += BATCH;

        elapsed = std::chrono::high_resolution_clock::now() - start;
        if (elapsed >= max_duration) {
            break;
        }
    }

    double took = elapsed.count(); // might exceed max_seconds if fun() takes much time by itself
    double ips = iterations / took; // iterations per second
    double mspi = 1000.0 / ips; // ms per iteration

    return { iterations, took, ips, mspi };
}