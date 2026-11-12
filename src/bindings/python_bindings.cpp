#include "bindings_common.hpp"

namespace nb = nanobind;
using namespace nb::literals;

// module:
NB_MODULE(_smb_native, m) {
    m.doc() =
        "Bindings for the Super Mario Bros. Environment (Low Level API).\n"
        "Do not use this library directly, use `smb_native`.";
    
    SMBEnv::InitResetPresets(); // avoids first reset() being slow

    // m.attr("SCREEN_WIDTH") = int(SCREEN_WIDTH);
    // m.attr("SCREEN_HEIGHT") = int(SCREEN_HEIGHT);

    // m.attr("PRESETS") = NamesOf(kPresets);
    // m.attr("RENDER_MODES") = NamesOf(kRenderModes);

    // IntFlag in python: Input.A | Input.B works
    nb::enum_<Input::Type>(m, "Input", nb::is_arithmetic(), nb::is_flag(),
            "NES Controller buttons as a bitmask (bit operations allowed on it)"
        )
        .value("NONE", Input::NONE)
        .value("RIGHT", Input::RIGHT)
        .value("LEFT", Input::LEFT)
        .value("DOWN", Input::DOWN)
        .value("UP", Input::UP)
        .value("START", Input::START)
        .value("SELECT", Input::SELECT)
        .value("B", Input::B)
        .value("A", Input::A);

    BindSMBEnv(m);
    BindSMBEnvVec(m);
}