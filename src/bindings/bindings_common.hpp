#pragma once
#include <nanobind/nanobind.h>
#include <nanobind/ndarray.h>
#include <smb/core/env.hpp>
#include <smb/core/env_vec.hpp>
#include <vector>
#include <string>
#include <string_view>
#include <utility>

// these are required to avoid issues in the .pyi file
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/tuple.h>

namespace nb = nanobind;
using namespace nb::literals;

// each file binds its own part, called in this order from NB_MODULE
// (SMBEnv must be bound before SMBEnvVec, as the signatures refer to it)
void BindSMBEnv(nb::module_& m);
void BindSMBEnvVec(nb::module_& m);

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

// numpy aliases:
using Frame = nb::ndarray<nb::numpy, uint8_t>;
using Buffer = nb::ndarray<uint8_t, nb::c_contig, nb::device::cpu>; // for rendering functions

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

// assert for buffer arg that python gives in rendering functions
static void CheckShape(const Buffer& buf, const std::vector<size_t>& expected, const char* what) {
    bool ok = buf.ndim() == expected.size();

    for (size_t i = 0; ok && i < expected.size(); i++)
        ok = buf.shape(i) == expected[i];

    if (!ok)
        throw nb::value_error((std::string("buffer has wrong shape for ") + what).c_str());
}