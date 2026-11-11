#include <smb/core/env_vec.hpp>

// the only file to import openmp!
#ifdef _OPENMP
    #include <omp.h>
#endif

bool SMBEnvVec::UsesOpenMP() noexcept {
#ifdef _OPENMP
    return true;
#else
    return false; // single-threaded
#endif
}

int SMBEnvVec::MaxThreads() noexcept {
#ifdef _OPENMP
    return omp_get_max_threads();
#else
    return 1;
#endif
}

void SMBEnvVec::ResetAll(ResetPreset preset) noexcept {
    // signed loop index because older openmp versions and msvc require it? // TODO re-revise
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(envs.size());

    #pragma omp parallel for schedule(static)
    for (std::ptrdiff_t i = 0; i < count; i++)
        envs[i].Reset(preset);
}

void SMBEnvVec::StepAll(const uint8_t* actions, int times) noexcept {
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(envs.size());

    // comparing the nanoseconds that each .Step() takes, long/complex/branchful
    // game logic takes almost the same time to run as predictable easy game logic
    // (meanwhile, repeating this test on millions on steps, easy game logic takes
    // ~400 less steps per millisecond; not accurate), so with a static schedule the
    // threads should take roughly the same time to finish
    #pragma omp parallel for schedule(static)
    for (std::ptrdiff_t i = 0; i < count; i++)
        envs[i].Step(actions[i], times);
}

void SMBEnvVec::RenderFrameAll(uint8_t* buffer, RenderMode renderMode) const noexcept {
    const std::ptrdiff_t count = static_cast<std::ptrdiff_t>(envs.size());
    const size_t frameBytes = FrameBytesOf(renderMode);

    #pragma omp parallel for schedule(static)
    for (std::ptrdiff_t i = 0; i < count; i++)
        envs[i].RenderFrame(buffer + (static_cast<size_t>(i) * frameBytes), renderMode);
}