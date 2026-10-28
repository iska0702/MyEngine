#include "Timer.h"

#ifdef _WIN32
Timer::Timer()
    : m_secondsPerCount(0.0)
    , m_prevTime(0)
    , m_currTime(0)
    , m_deltaTime(-1.0f) {
    
    __int64 countsPerSec;
    QueryPerformanceFrequency((LARGE_INTEGER*)&countsPerSec);
    m_secondsPerCount = 1.0 / (double)countsPerSec;

    QueryPerformanceCounter((LARGE_INTEGER*)&m_prevTime);
}

void Timer::Tick() {
    QueryPerformanceCounter((LARGE_INTEGER*)&m_currTime);
    m_deltaTime = static_cast<float>((m_currTime - m_prevTime) * m_secondsPerCount);
    m_prevTime = m_currTime;

    if (m_deltaTime < 0.0f) {
        m_deltaTime = 0.0f;
    }
}
#else
Timer::Timer()
    : m_deltaTime(0.0f) {
    m_prevTimePoint = std::chrono::high_resolution_clock::now();
}

void Timer::Tick() {
    auto now = std::chrono::high_resolution_clock::now();
    std::chrono::duration<float> elapsed = now - m_prevTimePoint;
    m_deltaTime = elapsed.count();
    m_prevTimePoint = now;

    if (m_deltaTime < 0.0f) {
        m_deltaTime = 0.0f;
    }
}
#endif
