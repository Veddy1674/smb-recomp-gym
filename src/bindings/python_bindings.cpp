#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>
#include <smb/core/env.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace nb = nanobind;
using namespace nb::literals;

// string to enum tables:
template <typename E>
using NameTable = std::vector<std::pair<std::string_view, E>>;

static const NameTable<ResetPreset> kPresets = {
    {"ColdBoot", ResetPreset::ColdBoot},
    {"TitleScreen", ResetPreset::TitleScreen},
    {"W1L1", ResetPreset::W1L1}, {"W1L2", ResetPreset::W1L2},
    {"W1L3", ResetPreset::W1L3}, {"W1L4", ResetPreset::W1L4},
    {"W2L1", ResetPreset::W2L1}, {"W2L2", ResetPreset::W2L2},
    {"W2L3", ResetPreset::W2L3}, {"W2L4", ResetPreset::W2L4},
    {"W3L1", ResetPreset::W3L1}, {"W3L2", ResetPreset::W3L2},
    {"W3L3", ResetPreset::W3L3}, {"W3L4", ResetPreset::W3L4},
    {"W4L1", ResetPreset::W4L1}, {"W4L2", ResetPreset::W4L2},
    {"W4L3", ResetPreset::W4L3}, {"W4L4", ResetPreset::W4L4},
    {"W5L1", ResetPreset::W5L1}, {"W5L2", ResetPreset::W5L2},
    {"W5L3", ResetPreset::W5L3}, {"W5L4", ResetPreset::W5L4},
    {"W6L1", ResetPreset::W6L1}, {"W6L2", ResetPreset::W6L2},
    {"W6L3", ResetPreset::W6L3}, {"W6L4", ResetPreset::W6L4},
    {"W7L1", ResetPreset::W7L1}, {"W7L2", ResetPreset::W7L2},
    {"W7L3", ResetPreset::W7L3}, {"W7L4", ResetPreset::W7L4},
    {"W8L1", ResetPreset::W8L1}, {"W8L2", ResetPreset::W8L2},
    {"W8L3", ResetPreset::W8L3}, {"W8L4", ResetPreset::W8L4},
};

static const NameTable<RenderMode> kRenderModes = {
    {"indexed", RenderMode::Indexed},
    {"rgb", RenderMode::RGB},
    {"grayscale", RenderMode::Grayscale},
};

template <typename E>
static E ParseName(const NameTable<E>& table, std::string_view name, const char* what) {
    for (const auto& [key, value] : table)
        if (key == name)
            return value;
    
    throw nb::value_error(("invalid " + std::string(what) + ": '" + std::string(name) + "'").c_str());
}

static ResetPreset ParsePreset(std::string_view name) {
    return ParseName(kPresets, name, "preset");
}

static RenderMode ParseRenderMode(std::string_view name) {
    return ParseName(kRenderModes, name, "render_mode");
}

// Numpy Helpers:
using Frame = nb::ndarray<nb::numpy, uint8_t>;
// caller-provided outputBuffer for rendering functions
using Buffer = nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu>;

// same, but the shape comes from a runtime ndim + pointer (e.g. a std::vector)
template <typename T>
static nb::ndarray<nb::numpy, T> Wrap(T* data, size_t ndim, const size_t* shape) {
    nb::capsule owner(data, [](void* p) noexcept {
        delete[] static_cast<T*>(p);
    });

    return nb::ndarray<nb::numpy, T>(data, ndim, shape, owner);
}

// shape as a vector
static std::vector<size_t> FrameShape(RenderMode mode) {
    if (mode == RenderMode::RGB)
        return {SCREEN_HEIGHT, SCREEN_WIDTH, 3};

    return {SCREEN_HEIGHT, SCREEN_WIDTH}; // indexed / grayscale
}

static std::vector<size_t> SpriteShape(const Sprite& s) {
    return {size_t(s.height), size_t(s.width), 4}; // rgba
}

// assert for buffer arg that python gives in rendering functions
static void CheckShape(const Buffer& buf, const std::vector<size_t>& expected, const char* what) {
    bool ok = buf.ndim() == expected.size();

    for (size_t i = 0; ok && i < expected.size(); ++i)
        ok = buf.shape(i) == expected[i];

    if (!ok)
        throw nb::value_error((std::string("buffer has wrong shape for ") + what).c_str());
}

static Frame NewFrame(const SMBEnv& env, RenderMode mode) {
    const auto shape = FrameShape(mode);

    auto* buffer = new uint8_t[FrameBytesOf(mode)];
    env.RenderFrame(buffer, mode);

    return Wrap(buffer, shape.size(), shape.data());
}

static void FrameInto(const SMBEnv& env, Buffer out, RenderMode mode) {
    CheckShape(out, FrameShape(mode), "frame");
    env.RenderFrame(out.data(), mode);
}

// 'render' is a callable that fills a uint8_t* buffer (member RenderSprite / RenderSpriteMario)
template <typename Render>
static std::tuple<Frame, int, int> NewSprite(const Sprite& sprite, Render render) {
    const auto shape = SpriteShape(sprite);
    
    auto* buffer = new uint8_t[shape[0] * shape[1] * 4](); // zeroed = transparent
    render(buffer);

    return { Wrap(buffer, shape.size(), shape.data()), sprite.x, sprite.y };
}

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

    nb::class_<SMBEnv> cls(m, "SMBEnv",
        "Super Mario Bros. Environment (Low Level API).\n\n"
        "Class attributes:\n"
        "    SIZE_BYTES: sizeof the underlying C++ struct, in bytes.");

    cls.attr("SIZE_BYTES") = sizeof(SMBEnv);

    // methods:
    cls.def(nb::init<>(),
            "Create a new environment."
        )

        .def("reset",
            [](SMBEnv& env, std::string_view preset) {
                env.Reset(ParsePreset(preset));
            },
            "preset"_a = "TitleScreen",
            nb::sig("def reset(self, preset: str = 'TitleScreen') -> None"),
            "Reset to a preset state: 'ColdBoot', 'TitleScreen' or 'W1L1' ... 'W8L4'. "
            "Raises ValueError if the name is invalid."
        )

        // action is held for `times` frames
        .def("step",
            [](SMBEnv& env, uint8_t action, int times) {
                env.Step(action, times);
            },
            "action"_a, "times"_a = 1,
            nb::sig("def step(self, action: int, times: int = 1) -> None"),
            "Advance `times` frames, holding `action` (an Input bitmask) the whole time."
        )

        .def_static("validate_input", &SMBEnv::ValidateInput,
            "action"_a,
            "Remove impossible combinations (left+right, up+down) from an input bitmask."
        )

        // rendering:
        .def("get_frame",
            [](const SMBEnv& env, Buffer out, std::string_view mode) {
                FrameInto(env, out, ParseRenderMode(mode));
            },
            nb::arg("out").noconvert(), "render_mode"_a = "rgb",
            "Render into a preallocated array. Shape (H, W, 3) for rgb, (H, W) otherwise."
        )

        .def("get_frame",
            [](const SMBEnv& env, std::string_view mode) {
                return NewFrame(env, ParseRenderMode(mode));
            },
            "render_mode"_a = "rgb",
            "Render into a new array."
        )

        // returns (rgba image, x, y)
        .def("render_sprite",
            [](const SMBEnv& env, int firstSlot, int count) {
                return NewSprite(
                    env._GetSprite(firstSlot, count),
                    [&](uint8_t* buffer) {
                        env.RenderSprite(buffer, firstSlot, count);
                    }
                );
            },
            "first_slot"_a, "count"_a,
            "Render `count` OAM sprites starting at `first_slot`. "
            "Returns (rgba image of shape (h, w, 4), x, y). The image is empty if nothing is visible."
        )

        // returns (rgba image, x, y)
        .def("render_sprite_mario",
            [](const SMBEnv& env) {
                return NewSprite(
                    env._GetSpriteMario(),
                    [&](uint8_t* buffer) {
                        env.RenderSpriteMario(buffer);
                    }
                );
            },
            "Render Mario's sprite. Returns (rgba image of shape (h, w, 4), x, y)."
        )

        // zero-copy view, stays valid for the env lifetime (and reset() assigns in place)
        .def_prop_ro("ram",
            [](SMBEnv& env) {
                return nb::ndarray<nb::numpy, uint8_t, nb::shape<0x800>>(
                    env.ram,
                    {sizeof(env.ram)},
                    nb::find(env)
                );
            },
            "uint8 view of the 2KB RAM. Valid for the env lifetime."
        )

        // utilities for RL and such:
        .def("get_mario_state", &SMBEnv::GetMarioState,
            "Raw player state (0x08 = normal, 0x06 = dead, 0x0B = dying, 0x04 = flagpole/axe walk, ...)."
        )
        .def("is_death_state", &SMBEnv::IsDeathState,
            "True if Mario is dead, dying or fell in a pit."
        )
        .def("is_win_state", &SMBEnv::IsWinState,
            "True if the level was completed (flag/axe touched)."
        )
        .def("get_terminated", &SMBEnv::GetTerminated,
            "True if is_death_state() or is_win_state()."
        )
        .def("get_lives", &SMBEnv::GetLives,
            "Remaining lives."
        )
        .def("get_coins", &SMBEnv::GetCoins,
            "Coin count (0-99)."
        )
        .def("get_time", &SMBEnv::GetTime,
            "Remaining level time."
        )
        .def("get_score", &SMBEnv::GetScore,
            "Player's current score."
        )
        .def("get_high_score", &SMBEnv::GetHighScore,
            "Player's high score."
        )
        .def("get_viewport_y", &SMBEnv::GetViewportY,
            "Vertical viewport position: 1 = inside, 0 = above, >1 = below."
        )
        .def("get_abs_y", &SMBEnv::GetAbsY,
            "Mario's absolute vertical position (and on screen)."
        )
        .def("get_abs_x", &SMBEnv::GetAbsX,
            "Mario's horizontal position in the level (page * 256 + rel x)."
        )
        .def("get_screen_x", &SMBEnv::GetScreenX,
            "Mario's horizontal position relative to the screen's left edge."
        )

        // copy/equals
        .def("copy", &SMBEnv::Copy,
            "Return an independent copy of this environment."
        )
        .def("__copy__", &SMBEnv::Copy,
            "Return an independent copy of this environment."
        )
        .def("equals",
            [](const SMBEnv& a, const SMBEnv& b) {
                return a == b; // memcmp on ram
            },
            "True if the RAM of both environments is identical."
        )
        .def("__eq__",
            [](const SMBEnv& a, const SMBEnv& b) {
                return a == b; // memcmp on ram
            },
            nb::is_operator(),
            "True if the RAM of both environments is identical."
        )

        // hashing is really shallow and should be only used as a first
        // measure to compare two environments (e.g via set()), then
        // equals()/__eq__() should be used to confirm equality
        .def("__hash__",
            [](const SMBEnv& env) {
                const std::string_view ram(reinterpret_cast<const char*>(env.ram), sizeof(env.ram));
                return std::hash<std::string_view>{}(ram);
            },
            "Hash of the RAM contents."
        )

        .def("equals_any",
            [](const SMBEnv& env, nb::iterable others) {
                for (nb::handle h : others) {
                    if (env == nb::cast<const SMBEnv&>(h))
                        return true;
                }
                return false;
            },
            "True if this environment equals to any environment in `others`."
        );
}