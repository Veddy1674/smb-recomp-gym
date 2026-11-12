#include "bindings_common.hpp"
#include <tuple>
#include <optional>

// used by SMBEnv, in an anonymous namespace so they stay private to this file
namespace {

static std::vector<size_t> SpriteShape(const Sprite& s) {
    return {size_t(s.height), size_t(s.width), 4}; // rgba
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
static std::tuple<std::optional<Frame>, int, int> NewSprite(const Sprite& sprite, Render render) {
    if (!sprite.exists())
        return { std::nullopt, sprite.x, sprite.y };

    const auto shape = SpriteShape(sprite);
    
    auto* buffer = new uint8_t[shape[0] * shape[1] * 4](); // zeroed = transparent
    render(buffer);

    return {
        Wrap(buffer, shape.size(), shape.data()), sprite.x, sprite.y
    };
}

} // namespace closure

void BindSMBEnv(nb::module_& m) {
    // define class
    nb::class_<SMBEnv> cls(m, "SMBEnv",
        "Super Mario Bros. Environment (Low Level API).\n\n"
        "Class attributes:\n"
        "    SIZE_BYTES: sizeof the underlying C++ struct, in bytes.");

    cls.attr("SIZE_BYTES") = sizeof(SMBEnv);

    // methods:
    cls
        .def(nb::init<>(),
            "Create a new environment."
        )

        .def("reset",
            [](SMBEnv& env, std::string_view preset) {
                env.Reset(ParsePreset(preset));
            },
            "preset"_a = "TitleScreen",
            nb::sig("def reset(self, preset: str = 'TitleScreen') -> None"),
            "Reset to a preset state: 'ColdBoot', 'TitleScreen' or 'W1L1' ... 'W8L4'. "
            "Raises ValueError if the state is invalid."
        )

        // action is held for `times` frames
        .def("step",
            [](SMBEnv& env, uint8_t action, int times) {
                env.Step(action, times);
            },
            "action"_a, "times"_a = 1,
            nb::sig("def step(self, action: int, times: int = 1) -> None"),
            "Advance `times` frames, holding `action` (uint8 Input bitmask) the whole time."
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
            "Returns (rgba image (h, w, 4) or None if not visible, x, y). x and y are 0 when the image is None."
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
            "Returns rendered Mario sprite as (rgba image (h, w, 4) or None if not visible, x, y). x and y are 0 when the image is None."
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
            nb::sig("def __eq__(self, arg: object, /) -> bool"),
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
                for (nb::handle item : others)
                    if (env == nb::cast<const SMBEnv&>(item))
                        return true;
                
                return false;
            },
            "others"_a,
            "True if this environment equals to any environment in `others`."
        );
}