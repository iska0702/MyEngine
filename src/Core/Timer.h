#pragma once

#ifdef _WIN32
#include <windows.h>
#else
#include <chrono>
#include <cstdint>
#endif

class Timer {
public:
    Timer();
    
    void Tick();
    float GetDeltaTime() const { return m_deltaTime; }

private:
#ifdef _WIN32
    double m_secondsPerCount;
    __int64 m_prevTime;
    __int64 m_currTime;
#else
    std::chrono::high_resolution_clock::time_point m_prevTimePoint;
#endif
    float m_deltaTime;
};
