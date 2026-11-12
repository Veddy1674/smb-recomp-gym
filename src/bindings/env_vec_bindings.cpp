#include "bindings_common.hpp"

// used by SMBEnvVec, in an anonymous namespace so they stay private to this file
namespace {

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

// 1D uint8 array of actions
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

// methods that return arrays of 0/1 flags are exposed as numpy bool
static nb::ndarray<nb::numpy, bool, nb::ndim<1>> WrapFlags(std::vector<uint8_t> flags) {
    auto* owned = new std::vector<uint8_t>(std::move(flags));

    nb::capsule owner(owned, [](void* p) noexcept {
        delete static_cast<std::vector<uint8_t>*>(p);
    });

    return nb::ndarray<nb::numpy, bool, nb::ndim<1>>(
        reinterpret_cast<bool*>(owned->data()), {owned->size()}, owner
    );
}

// (n_envs, 2048) uint8 array, copies every ram view (that you get via envs.GetRAMAll())
static nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>> WrapRAMCopy(std::vector<uint8_t> ram, size_t envCount) {
    auto* owned = new std::vector<uint8_t>(std::move(ram));

    nb::capsule owner(owned, [](void* p) noexcept {
        delete static_cast<std::vector<uint8_t>*>(p);
    });

    return nb::ndarray<nb::numpy, uint8_t, nb::ndim<2>>(
        owned->data(), {envCount, SMBEnvVec::RamSize}, owner
    );
}

} // namespace closure

void BindSMBEnvVec(nb::module_& m) {
    // define class
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
            "Reset every environment to a preset state: 'ColdBoot', 'TitleScreen' or 'W1L1' ... 'W8L4'. "
            "Raises ValueError if the state is invalid."
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
            nb::sig("def __eq__(self, arg: object, /) -> bool"),
            "True if the RAM of every environment matches the other vector's, in order."
        );
}