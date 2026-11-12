#include "bindings_common.hpp"

namespace nb = nanobind;

// module:
NB_MODULE(_smb_native, m) {
    m.doc() =
        "Bindings for the Super Mario Bros. Environment (Low Level API).\n"
        "Do not use this library directly, use `smb_native`.";
    
    SMBEnv::InitResetPresets(); // avoids first reset() being slow

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
        .value("A", Input::A)

        .def("__repr__",
            [](nb::handle self) {
                // NOTE (obvious but I didn't know) without <nanobind/stl/string.h>:
                // cast and format fail with RuntimeError: std::bad_cast
                // so it must be included in bindings_common.hpp
                
                // e.g. name is "A", value is "128"
                std::string name = nb::cast<std::string>(self.attr("name"));
                int value = nb::cast<int>(self.attr("value"));

                return "<Input." + name + ": " + std::to_string(value) + ">";
            },
            nb::sig("def __repr__(self) -> str")
        )
        
        .def("__str__",
            [](nb::handle self) {
                std::string name = nb::cast<std::string>(self.attr("name"));
                return "Input." + name;
            },
            nb::sig("def __str__(self) -> str")
        );

    BindSMBEnv(m);
    BindSMBEnvVec(m);
}