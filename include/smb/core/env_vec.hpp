#pragma once
#include <smb/core/env.hpp>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <vector>

struct SMBEnvVec {
    static constexpr size_t RamSize = sizeof(SMBEnv::ram);

    explicit SMBEnvVec(size_t count) : envs(count) {}

    size_t Size() const noexcept {
        return envs.size();
    }

    // access internal SMBEnv envs
    SMBEnv& At(size_t idx) {
        if (idx >= envs.size())
            throw std::out_of_range("env index out of range");

        return envs[idx];
    }

    // for const vecs (e.g. inside const methods)
    const SMBEnv& At(size_t index) const {
        if (index >= envs.size())
            throw std::out_of_range("env index out of range");

        return envs[index];
    }

    // for nanobind, as .envs is private
    auto begin() {
        return envs.begin();
    }

    auto end() {
        return envs.end();
    }

    // defined in env_vec.cpp, so the openmp flags apply even to code including this header
    static bool UsesOpenMP() noexcept;
    static int MaxThreads() noexcept; // 1 if no openmp

    void Reset(size_t index, ResetPreset preset = ResetPreset::TitleScreen) {
        At(index).Reset(preset);
    }

    // parallel
    void ResetAll(ResetPreset preset = ResetPreset::TitleScreen) noexcept;

    // parallel, 'actions' has this->Size() elements, actions held for 'times' frames
    void StepAll(const uint8_t* actions, int times = 1) noexcept;

    // parallel, 'buffer' holds this->Size() * FrameBytesOf(renderMode) bytes
    void RenderFrameAll(uint8_t* buffer, RenderMode renderMode) const noexcept;

    // "ValidateInputAll" wasn't implemented because it's mostly useless; it's barely useful for SMBEnv alone

    // utilities used for RL and such (not parallel)
    std::vector<int32_t> GetMarioStateAll() const { return CollectInts(&SMBEnv::GetMarioState); }
    std::vector<int32_t> GetLivesAll() const { return CollectInts(&SMBEnv::GetLives); }
    std::vector<int32_t> GetCoinsAll() const { return CollectInts(&SMBEnv::GetCoins); }
    std::vector<int32_t> GetTimeAll() const { return CollectInts(&SMBEnv::GetTime); }
    std::vector<int32_t> GetScoreAll() const { return CollectInts(&SMBEnv::GetScore); }
    std::vector<int32_t> GetHighScoreAll() const { return CollectInts(&SMBEnv::GetHighScore); }
    std::vector<int32_t> GetViewportYAll() const { return CollectInts(&SMBEnv::GetViewportY); }
    std::vector<int32_t> GetAbsYAll() const { return CollectInts(&SMBEnv::GetAbsY); }
    std::vector<int32_t> GetAbsXAll() const { return CollectInts(&SMBEnv::GetAbsX); }
    std::vector<int32_t> GetScreenXAll() const { return CollectInts(&SMBEnv::GetScreenX); }

    // flags are 0 or 1 (not std::vector<bool>, which is bit-packed!)
    std::vector<uint8_t> IsDeathStateAll() const { return CollectFlags(&SMBEnv::IsDeathState); }
    std::vector<uint8_t> IsWinStateAll() const { return CollectFlags(&SMBEnv::IsWinState); }
    std::vector<uint8_t> GetTerminatedAll() const { return CollectFlags(&SMBEnv::GetTerminated); }

    // copies every env's RAM into 'output' buffer, must be this->Size() * RamSize bytes
    void GetRAMAll(uint8_t* output) const noexcept {
        for (size_t i = 0; i < envs.size(); i++)
            std::memcpy(output + (i * RamSize), envs[i].ram, RamSize);
    }

    std::vector<uint8_t> GetRAMAll() const {
        std::vector<uint8_t> output(envs.size() * RamSize);
        GetRAMAll(output.data());

        return output;
    }

    // copies every env independently
    SMBEnvVec Copy() const {
        return *this;
    }

    // RAM comparison of every env against "other"'s envs (not sure when this could be used, might remove it)
    bool operator==(const SMBEnvVec& other) const noexcept {
        if (envs.size() != other.envs.size())
            return false;

        for (size_t i = 0; i < envs.size(); i++)
            if (!(envs[i] == other.envs[i]))
                return false;

        return true;
    }

private:
    std::vector<SMBEnv> envs; // AoS, multiple of 64

    // (helper for RAM-related utilities and such)
    // not parallel on purpose, since reading a few bytes per env is faster
    std::vector<int32_t> CollectInts(int (SMBEnv::*getter)() const) const {
        std::vector<int32_t> values(envs.size());

        for (size_t i = 0; i < envs.size(); i++)
            values[i] = (envs[i].*getter)();

        return values;
    }

    // (helper for RAM-related utilities and such)
    // not parallel on purpose, since reading a few bytes per env is faster
    std::vector<uint8_t> CollectFlags(bool (SMBEnv::*getter)() const) const {
        std::vector<uint8_t> values(envs.size());

        for (size_t i = 0; i < envs.size(); i++)
            values[i] = (envs[i].*getter)() ? 1 : 0; // returns uint8s with value 0 or 1, not booleans

        return values;
    }
};