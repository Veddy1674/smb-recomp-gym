#include <cstring>
#include <smb/core/environment.hpp>
#include <iostream>
#include <locale>
#include <iomanip>
#include "time.hpp"

int main() {
    using namespace Input;
    
    // NOTE: This is a quite bad benchmark and will be replaced with a proper one

    std::cout << "Running a benchmark...\n";

    SMBEnv env;
    std::vector<uint8_t> buffer(256 * 240 * 3); // dummy buffer
    BenchmarkResult result;

    std::cout.imbue(std::locale(""));
    std::cout << std::fixed;

    const double SECS_HEADLESS = 3.0;
    const double SECS_RENDERING = 3.0;

    // Headless:
    env.Reset(ResetPreset::W1L1);
    env.SetInput(Input::RIGHT);

    result = MeasureIterations(SECS_HEADLESS, [&] {
        env.Step();
    });

    std::cout << "Headless: " << result.iterations << " steps in " << std::setprecision(2) << result.took_seconds << "s"
        << " - " << std::setprecision(5) << result.ms_per_iteration << "ms/step or " << std::setprecision(2) << result.iterations_per_second << " FPS\n";

    // Rendering:
    env.Reset(ResetPreset::W1L1);
    env.SetInput(Input::RIGHT);

    result = MeasureIterations(SECS_RENDERING, [&] {
        env.Step();
        env.MakeFrame(buffer.data(), RenderMode::RGB);
    });

    std::cout << "Rendering: " << result.iterations << " steps in " << std::setprecision(2) << result.took_seconds << "s"
        << " - " << std::setprecision(5) << result.ms_per_iteration << "ms/step or " << std::setprecision(2) << result.iterations_per_second << " FPS\n";
    
    std::cout << std::endl;

    return 0;
}