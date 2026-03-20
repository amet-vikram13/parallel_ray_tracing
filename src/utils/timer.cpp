#include "utils/timer.h"

void Timer::start() {
    t_start = std::chrono::high_resolution_clock::now();
}

void Timer::stop() {
    t_end = std::chrono::high_resolution_clock::now();
}

double Timer::elapsed_ms() const {
    return std::chrono::duration<double, std::milli>(t_end - t_start).count();
}

void Timer::print(const std::string& label) const {
    printf("[%s] %.3f ms\n", label.c_str(), elapsed_ms());
}
