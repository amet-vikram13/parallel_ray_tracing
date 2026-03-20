#pragma once

#include <chrono>
#include <string>
#include <cstdio>

class Timer {
public:
    void start();
    void stop();
    double elapsed_ms() const;
    void print(const std::string& label) const;

private:
    std::chrono::high_resolution_clock::time_point t_start, t_end;
};
