from pathlib import Path
from time import perf_counter

import numpy as np
import stable_retro as retro

from smb_native import SMBEnv
from run_envs import get_tas_actions, get_lag_frames

# change these paths according to your setup
# NOTE: there currently isn't any checksum to verify whether the ROM or FM2 is valid!
ROM_PATH = Path("assets/smb.nes")
TAS_FM2_PATH = Path("benchmark/warpless.fm2")
TAS_LAG_PATH = Path("benchmark/warpless_lag.txt") # for smb_native, which doesn't simulate lag frames!
OUTPUT = Path("benchmark/") # output for logging and graphs

def run_stable_retro(actions: np.ndarray) -> tuple[float, np.ndarray]:
    try:
        env = retro.make(
            game="SuperMarioBros-Nes-v0",
            state=retro.State.NONE,
            render_mode="headless",
            obs_type=retro.Observations.RAM,
            use_restricted_actions=retro.Actions.ALL
        )
    except FileNotFoundError:
        raise ValueError("'SuperMarioBros-Nes-v0' not found, try running: python -m stable_retro.import {FOLDER WHERE YOUR ROM IS, e.g: 'assets/'}")

    env.reset()

    t = perf_counter()

    # loop
    all_ram = []
    for action in actions:
        # obs is the RAM itself
        ram = env.step(action)[0][:2048] # shape is 10240 (all memory), slice to 2048 (System RAM)
        all_ram.append(ram)

    t = perf_counter() - t
    env.close()

    return t, np.array(all_ram)

def run_smb_native(actions: list[int], lag_frames: set[int]) -> tuple[float, np.ndarray]:
    env = SMBEnv()
    env.reset(preset="ColdBoot")

    t = perf_counter()

    # loop
    all_ram = []
    for i, action in enumerate(actions):
        if i in lag_frames:
            # this will copy the last frame's RAM, but not via all_ram[-1] as it
            # might cause an IndexError if lag frames contain "0"
            all_ram.append(env.ram.copy())
            continue
        
        env.set_input(action)
        env.step()

        all_ram.append(env.ram.copy())

    return perf_counter() - t, np.array(all_ram)

if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)

    ACTIONS, _, ACTIONS_NP = get_tas_actions(TAS_FM2_PATH)
    LAG_FRAMES = get_lag_frames(TAS_LAG_PATH)

    print(f"Verifying RAM matching between smb-native and stable-retro...")

    t = perf_counter()

    stable_retro_t, stable_retro_rams = run_stable_retro(actions=ACTIONS_NP)
    print(f"stable-retro done ({stable_retro_t:.2f}s)")

    smb_native_t, smb_native_rams = run_smb_native(actions=ACTIONS, lag_frames=LAG_FRAMES)
    print(f"smb-native done ({smb_native_t:.2f}s)")

    # removing first RAM from smb-native because stable-retro skips the first action, thus shape would mismatch
    smb_native_rams = smb_native_rams[1:]

    print("Shapes:")
    print("  stable-retro:", stable_retro_rams.shape)
    print("  smb-native:", smb_native_rams.shape)

    assert stable_retro_rams.shape == smb_native_rams.shape, (
        "Something went wrong, shapes don't match!\n"
        "The TAS might be corrupted or its lag frames not simulated correctly for smb-native."
    )

    framecount = stable_retro_rams.shape[0]

    def print_single_match(i: int):
        # convert negative index to positive (to print it)
        if i < 0:
            i = framecount + i

        # skip "i" if it's a lag frame
        while i in LAG_FRAMES and i < framecount:
            i += 10

        # backtrace in the rare case it goes over framecount
        if i >= framecount:
            i = framecount - 1
            while i in LAG_FRAMES and i >= 0:
                i -= 1
        
        match = (stable_retro_rams[i] == smb_native_rams[i]).mean() * 100

        # match w/o stack
        sr_no_stack = np.concatenate([stable_retro_rams[i][:256], stable_retro_rams[i][512:]])
        sn_no_stack = np.concatenate([smb_native_rams[i][:256], smb_native_rams[i][512:]])
        match_no_stack = (sr_no_stack == sn_no_stack).mean() * 100

        print(f"RAM match at frame {i + 1}: {match:.2f}% (w/o stack: {match_no_stack:.2f}%)")

    print_single_match(99)
    print_single_match(framecount // 2) # halfway
    print_single_match(-999) # 1000 frames before last
    print_single_match(-1) # last frame

    # ignore ram compares during lag frames
    # (e.g: frame 2 from coldboot is usually 73.97% match because it compares smb-native's reset()
    # and stable-retro where both reset() and step() were called, the latter 2 times, so it's not fair)
    valid_mask = np.array([(i + 1) not in LAG_FRAMES for i in range(framecount)])

    all_mask: np.ndarray = (stable_retro_rams == smb_native_rams)
    overall_match: float = all_mask[valid_mask].mean() * 100
    match_per_frame: np.ndarray = all_mask.mean(axis=1) * 100

    # parallel to above, but without stack (around 12.5% of total RAM so it's worth logging)
    all_mask_no_stack = np.delete(all_mask, slice(256, 512), axis=1)
    valid_all_mask_no_stack = all_mask_no_stack[valid_mask]

    overall_match_no_stack = valid_all_mask_no_stack.mean() * 100
    match_per_frame_no_stack = all_mask_no_stack.mean(axis=1) * 100

    valid_indices = np.where(valid_mask)[0]
    valid_match_per_frame = match_per_frame[valid_mask]
    
    idx_min = valid_indices[np.argmin(valid_match_per_frame)]
    idx_max = valid_indices[np.argmax(valid_match_per_frame)]

    print(f"Overall RAM match: {overall_match:.2f}% (w/o stack: {overall_match_no_stack:.2f}%)")
    print(f"Lowest RAM match: {match_per_frame[idx_min]:.2f}% (w/o stack: {match_per_frame_no_stack[idx_min]:.2f}%) at frame {idx_min + 1}")
    print(f"Highest RAM match: {match_per_frame[idx_max]:.2f}% (w/o stack: {match_per_frame_no_stack[idx_max]:.2f}%) at frame {idx_max + 1}")

    perfect_matches_count = valid_all_mask_no_stack.all(axis=1).sum()
    total_valid_frames = valid_mask.sum() # excluding lag frames
    
    print(f"Frames with 100% match (w/o stack): {perfect_matches_count} / {total_valid_frames} ({perfect_matches_count / total_valid_frames * 100:.2f}%)")

    print(f"\nTotal time elapsed: {perf_counter() - t:.2f}s")