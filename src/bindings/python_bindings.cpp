#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/numpy.h>
#include <smb/core/environment.hpp>
#include <smb/core/renderer.hpp>
#include <optional>
#include <cstdint>

namespace py = pybind11;

PYBIND11_MODULE(__smb_native__, m) {
    // allows bitmask operations like Input.A | Input.B in python
    py::enum_<Input::Type>(m, "Input", py::arithmetic())
        .value("NONE", Input::NONE)
        .value("RIGHT", Input::RIGHT)
        .value("LEFT", Input::LEFT)
        .value("DOWN", Input::DOWN)
        .value("UP", Input::UP)
        .value("START", Input::START)
        .value("SELECT", Input::SELECT)
        .value("B", Input::B)
        .value("A", Input::A)
        .export_values();

    // used in the wrapper
    py::enum_<RenderMode>(m, "RenderMode")
        .value("indexed", RenderMode::Indexed)
        .value("rgb", RenderMode::RGB)
        .value("grayscale", RenderMode::Grayscale)
        .export_values();
    
    // used in the wrapper
    py::enum_<ResetPreset>(m, "ResetPreset")
        .value("ColdBoot", ResetPreset::ColdBoot)
        .value("TitleScreen", ResetPreset::TitleScreen)
        .value("W1L1", ResetPreset::W1L1)
        .value("W1L2", ResetPreset::W1L2)
        .value("W1L3", ResetPreset::W1L3)
        .value("W1L4", ResetPreset::W1L4)
        .value("W2L1", ResetPreset::W2L1)
        .value("W2L2", ResetPreset::W2L2)
        .value("W2L3", ResetPreset::W2L3)
        .value("W2L4", ResetPreset::W2L4)
        .value("W3L1", ResetPreset::W3L1)
        .value("W3L2", ResetPreset::W3L2)
        .value("W3L3", ResetPreset::W3L3)
        .value("W3L4", ResetPreset::W3L4)
        .value("W4L1", ResetPreset::W4L1)
        .value("W4L2", ResetPreset::W4L2)
        .value("W4L3", ResetPreset::W4L3)
        .value("W4L4", ResetPreset::W4L4)
        .value("W5L1", ResetPreset::W5L1)
        .value("W5L2", ResetPreset::W5L2)
        .value("W5L3", ResetPreset::W5L3)
        .value("W5L4", ResetPreset::W5L4)
        .value("W6L1", ResetPreset::W6L1)
        .value("W6L2", ResetPreset::W6L2)
        .value("W6L3", ResetPreset::W6L3)
        .value("W6L4", ResetPreset::W6L4)
        .value("W7L1", ResetPreset::W7L1)
        .value("W7L2", ResetPreset::W7L2)
        .value("W7L3", ResetPreset::W7L3)
        .value("W7L4", ResetPreset::W7L4)
        .value("W8L1", ResetPreset::W8L1)
        .value("W8L2", ResetPreset::W8L2)
        .value("W8L3", ResetPreset::W8L3)
        .value("W8L4", ResetPreset::W8L4)
        .export_values();

    py::class_<SMBEnv>(m, "SMBEnv")

        .def(py::init<>()) // no args

        .def("reset", [](SMBEnv& env, ResetPreset preset) {
                env.Reset(preset);
            },
            py::arg("preset")
        )

        .def("step", [](SMBEnv& env, int times) {
                for (int i = 0; i < times; i++)
                    env.Step();
            },
            py::arg("times")
        )

        .def("set_input", [](SMBEnv& env, uint8_t action) {
                env.SetInput(action);
            },
            py::arg("action")
        )
        
        .def("get_frame", [](const SMBEnv& env, RenderMode renderMode) {
                auto frame = (renderMode == RenderMode::RGB)
                    ? py::array_t<uint8_t>({ SCREEN_HEIGHT, SCREEN_WIDTH, 3 })
                    : py::array_t<uint8_t>({ SCREEN_HEIGHT, SCREEN_WIDTH });

                env.MakeFrame(frame.mutable_data(), renderMode);

                return frame;
            },
            py::arg("render_mode")
        )

        .def("render_sprite_at", [](const SMBEnv& env, int firstSlot, int count) {
            const Sprite sprite = env.ppu.GetSpriteAt(firstSlot, count);

            // alloc frame
            auto frame = py::array_t<uint8_t>({ sprite.height, sprite.width, 4 });

            if (sprite.exists())
                env.ppu.RenderSpritesRGBA(firstSlot, count, sprite, frame.mutable_data());

            return py::make_tuple(frame, sprite.x, sprite.y);
        })
        
        .def_property_readonly("ram", [](const SMBEnv& env) {
            return py::array_t<uint8_t>(
                { sizeof(env.ram) }, { sizeof(uint8_t) }, env.ram, py::cast(&env));
        })

        .def("get_real_ram_usage", [](const SMBEnv&) {
            return sizeof(SMBEnv) + sizeof(py::detail::instance);
        })

        .def("equals", [](const SMBEnv& env, const SMBEnv& other) {
            return env == other; // memcpy
        })

        .def("copy", &SMBEnv::Copy);
}