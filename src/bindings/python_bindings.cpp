#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/optional.h>
#include <nanobind/stl/tuple.h>
#include <smb/core/env_vec.hpp>
#include <smb/core/env.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <optional>

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

    for (size_t i = 0; ok && i < expected.size(); i++)
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

// batched frames: (n_envs, H, W, 3) for rgb, (n_envs, H, W) otherwise
static std::vector<size_t> BatchFrameShape(size_t envCount, RenderMode mode) {
    std::vector<size_t> shape = FrameShape(mode);
    shape.insert(shape.begin(), envCount);

    return shape;
}

// batched "NewFrame"
static Frame NewFrameAll(const SMBEnvVec& vec, RenderMode mode) {
    const auto shape = BatchFrameShape(vec.Size(), mode);

    auto* buffer = new uint8_t[vec.Size() * FrameBytesOf(mode)];

    {
        nb::gil_scoped_release release;
        vec.RenderFrameAll(buffer, mode);
    }

    // the capsule needs the GIL, so wrap is done after the release scope
    return Wrap(buffer, shape.size(), shape.data());
}

// batched "FrameInto"
static void FrameAllInto(const SMBEnvVec& vec, Buffer out, RenderMode mode) {
    CheckShape(out, BatchFrameShape(vec.Size(), mode), "batched frames");

    nb::gil_scoped_release release;
    vec.RenderFrameAll(out.data(), mode);
}

// 1D uint8 array of actions for SMBEnvVec
using Actions = nb::ndarray<const uint8_t, nb::ndim<1>, nb::c_contig, nb::device::cpu>;

// 1D numpy array that takes ownership of a std::vector (moved to the heap, freed by the capsule)
template <typename T>
static nb::ndarray<nb::numpy, T, nb::ndim<1>> WrapVector(std::vector<T> values) {
    auto* owned = new std::vector<T>(std::move(values));

    nb::capsule owner(owned, [](void* p) noexcept {
        delete static_cast<std::vector<T>*>(p);
    });

    return nb::ndarray<nb::numpy, T, nb::ndim<1>>(owned->data(), {owned->size()}, owner);
}

// methods that return arrays of 0/1 flags (in SMBEnvVec) are exposed as numpy bool
static nb::ndarray<nb::numpy, bool, nb::ndim<1>> WrapFlags(std::vector<uint8_t> flags) {
    auto* owned = new std::vector<uint8_t>(std::move(flags));

    nb::capsule owner(owned, [](void* p) noexcept {
        delete static_cast<std::vector<uint8_t>*>(p);
    });

    return nb::ndarray<nb::numpy, bool, nb::ndim<1>>(
        reinterpret_cast<bool*>(owned->data()), {owned->size()}, owner
    );
}

// (n_envs, 2048) uint8 array for SMBEnvVec, copies every ram view (that you get via envs.GetRAMAll())
static nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>> WrapRAMCopy(std::vector<uint8_t> ram, size_t envCount) {
    auto* owned = new std::vector<uint8_t>(std::move(ram));

    nb::capsule owner(owned, [](void* p) noexcept {
        delete static_cast<std::vector<uint8_t>*>(p);
    });

    return nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>>(
        owned->data(), {envCount, SMBEnvVec::RamSize}, owner
    );
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
            "Raises ValueError if the name is invalid."
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
    
    nb::class_<SMBEnvVec> vecCls(m, "SMBEnvVec",
        "Batch of independent Super Mario Bros. Environments (Low Level API).\n"
        "Stepped and rendered in parallel when built with OpenMP (see HAS_OPENMP).");

    // methods:
    vecCls
        // lambda init instead of nb::init<>, so errors can be thrown (and warns printed)
        .def("__init__",
            [](SMBEnvVec* self, int envCount) {
                if (envCount <= 0)
                    throw nb::value_error("n_envs must be positive");

                if (!SMBEnvVec::UsesOpenMP()) {
                    nb::module_::import_("warnings").attr("warn")(
                        "smb_native was built without OpenMP, SMBEnvVec will run single-threaded. "
                        "Install OpenMP and rebuild for native multi-threading.",
                        nb::handle(PyExc_RuntimeWarning)
                    );
                }

                new (self)SMBEnvVec(static_cast<size_t>(envCount));
            },
            "n_envs"_a,
            "Create `n_envs` environments in a batch, natively managed in C++."
        )

        .def_prop_ro("n_envs", &SMBEnvVec::Size,
            "Number of environments, initialized in the constructor."
        )
        .def_static("max_threads", &SMBEnvVec::MaxThreads,
            "Max threads used for stepping and rendering (1 without OpenMP, see OMP_NUM_THREADS)."
        )
        .def("__len__", &SMBEnvVec::Size,
            "Number of environments."
        )

        // reference into the vector (never resized)
        .def("at",
            [](SMBEnvVec& vec, size_t index) -> SMBEnv& {
                return vec.At(index);
            },
            "index"_a,
            nb::rv_policy::reference_internal,
            "Access a single environment (view, not a copy). Raises IndexError if out of range."
        )

        // makes 'for env in envs:' work
        .def("__getitem__",
            [](SMBEnvVec& vec, int64_t index) -> SMBEnv& {
                if (index < 0)
                    index += static_cast<int64_t>(vec.Size()); // python-like negative index

                if (index < 0)
                    throw std::out_of_range("env index out of range");

                return vec.At(static_cast<size_t>(index));
            },
            "index"_a,
            nb::rv_policy::reference_internal,
            "Same as at(), supports negative indices and iteration."
        )

        .def("reset_all",
            [](SMBEnvVec& vec, std::string_view preset) {
                const ResetPreset parsed = ParsePreset(preset); // throws before releasing the GIL

                nb::gil_scoped_release release;
                vec.ResetAll(parsed);
            },
            "preset"_a = "TitleScreen",
            nb::sig("def reset_all(self, preset: str = 'TitleScreen') -> None"),
            "Reset every environment to a preset state."
        )

        .def("step_all",
            [](SMBEnvVec& vec, Actions actions, int times) {
                if (actions.shape(0) != vec.Size())
                    throw nb::value_error("actions must have exactly one element per environment");

                nb::gil_scoped_release release; // release gil!
                vec.StepAll(actions.data(), times);
            },
            "actions"_a, "times"_a = 1,
            "Advance every env `times` frames; env i holds actions[i] (uint8 Input bitmask) the whole time."
        )

        // rendering:

        // with buffer
        .def("get_frame_all",
            [](const SMBEnvVec& vec, Buffer out, std::string_view mode) {
                FrameAllInto(vec, out, ParseRenderMode(mode));
            },
            nb::arg("out").noconvert(),
            "render_mode"_a = "rgb",
            "Render all envs into a preallocated array. Shape (n_envs, H, W, 3) for rgb, (n_envs, H, W) otherwise."
        )

        // without buffer (new frame)
        .def("get_frame_all",
            [](const SMBEnvVec& vec, std::string_view mode) {
                return NewFrameAll(vec, ParseRenderMode(mode));
            },
            "render_mode"_a = "rgb",
            "Render all envs into a new array."
        )

        // zero-copy view, rows are sizeof(SMBEnv) bytes apart
        .def_prop_ro("ram_all",
            [](SMBEnvVec& vec) {
                const size_t shape[2] = { vec.Size(), SMBEnvVec::RamSize };
                const int64_t strides[2] = { static_cast<int64_t>(sizeof(SMBEnv)), 1 };

                return nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>>(
                    vec.At(0).ram, // address where non-contigous ram begins
                    2,
                    shape,
                    nb::find(vec),
                    strides
                );
            },
            "uint8 view of every env's RAM, shape (`n_envs`, 2048). Not contiguous, writable."
        )

        .def("get_ram_all",
            [](const SMBEnvVec& vec) {
                return WrapRAMCopy(vec.GetRAMAll(), vec.Size());
            },
            "Copy of every env's RAM as a new contiguous array, shape (n_envs, 2048) uint8."
        )

        // utilities for RL and such:
        .def("get_mario_state_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetMarioStateAll()); },
            "Raw player state of every env (0x08 = normal, 0x06 = dead, 0x0B = dying, 0x04 = flagpole/axe walk, ...)."
        )
        .def("is_death_state_all",
            [](const SMBEnvVec& vec) { return WrapFlags(vec.IsDeathStateAll()); },
            "numpy bool array, True where Mario is dead, dying or fell in a pit."
        )
        .def("is_win_state_all",
            [](const SMBEnvVec& vec) { return WrapFlags(vec.IsWinStateAll()); },
            "numpy bool array, True where the level was completed (flag/axe touched)."
        )
        .def("get_terminated_all",
            [](const SMBEnvVec& vec) { return WrapFlags(vec.GetTerminatedAll()); },
            "numpy bool array, True where is_death_state() or is_win_state()."
        )
        .def("get_lives_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetLivesAll()); },
            "Remaining lives of every env. (numpy array int32)"
        )
        .def("get_coins_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetCoinsAll()); },
            "Coin count (0-99) of every env. (numpy array int32)"
        )
        .def("get_time_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetTimeAll()); },
            "Remaining level time of every env. (numpy array int32)"
        )
        .def("get_score_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetScoreAll()); },
            "Player's current score of every env. (numpy array int32)"
        )
        .def("get_high_score_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetHighScoreAll()); },
            "Player's high score of every env. (numpy array int32)"
        )
        .def("get_viewport_y_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetViewportYAll()); },
            "Vertical viewport position of every env: 1 = inside, 0 = above, >1 = below. (numpy array int32)"
        )
        .def("get_abs_y_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetAbsYAll()); },
            "Mario's absolute vertical position of every env (and on screen). (numpy array int32)"
        )
        .def("get_abs_x_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetAbsXAll()); },
            "Mario's horizontal position in the level of every env (page * 256 + rel x). (numpy array int32)"
        )
        .def("get_screen_x_all",
            [](const SMBEnvVec& vec) { return WrapVector(vec.GetScreenXAll()); },
            "Mario's horizontal position relative to the screen's left edge, of every env. (numpy array int32)"
        )

        // copy/equals
        .def("copy", &SMBEnvVec::Copy,
            "Return an independent copy of this vector and all its environments."
        )
        .def("__copy__", &SMBEnvVec::Copy,
            "Return an independent copy of this vector and all its environments."
        )
        .def("equals",
            [](const SMBEnvVec& a, const SMBEnvVec& b) {
                return a == b; // memcmp on ram, env by env
            },
            "True if the RAM of every environment matches the other vector's, in order."
        )
        .def("__eq__",
            [](const SMBEnvVec& a, const SMBEnvVec& b) {
                return a == b;
            },
            nb::is_operator(),
            "True if the RAM of every environment matches the other vector's, in order."
        );
}