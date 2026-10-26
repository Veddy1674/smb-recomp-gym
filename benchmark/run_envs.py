from datetime import datetime
import json
from pathlib import Path
import platform
import statistics
from time import perf_counter
from typing import Callable

import numpy as np
from nes_py import NESEnv
from PIL import Image
import stable_retro as retro

from smb_native import SMBEnv

# change these paths according to your setup
# NOTE: there currently isn't any checksum to verify whether the ROM or FM2 is valid!
ROM_PATH = Path("assets/smb.nes")
TAS_FM2_PATH = Path("benchmark/warpless.fm2")
TAS_LAG_PATH = Path("benchmark/warpless_lag.txt") # for smb_native, which doesn't simulate lag frames!
OUTPUT = Path("benchmark/") # output for logging and graphs

def get_tas_actions(path: Path) -> tuple[list[int], list[int], np.ndarray]:
    with open(path, "r", encoding="utf-8") as f:
        data = f.readlines()

    # here the string "romChecksum base64:" could be checked to verify the checksum

    idx = -1 # index of the first action
    actions: list[int] = []

    # find first action
    for i, line in enumerate(data):
        line = line.strip()

        if line.startswith("|0"): # ignore "|1|..." (console reset)
            idx = i
            break

    assert idx != -1, f"Could not find the first action in the TAS file: {path}"

    for line in data[idx + 1:]:
        # parse action
        fields = line.strip().split("|")
        if len(fields) < 3:
            raise ValueError(f"Invalid line {line} in: {path}")

        mask = 0
        for i, char in enumerate(fields[2]): # get player 1 input
            if char != ".":
                mask |= 1 << i
        
        actions.append(mask)

    # inverts bits order
    inverted = [int(f"{act:08b}"[::-1], 2) for act in actions]

    # to numpy array (for stable_retro)
    bits = np.unpackbits(
        # using "inverted" as base
        np.array(inverted, dtype=np.uint8)[:, None], # shape 8 to 9
        axis=1,
        bitorder="little"
    )
    
    # stable retro has different input order:
    # ['B', None, 'SELECT', 'START', 'UP', 'DOWN', 'LEFT', 'RIGHT', 'A']
    retro_arr = bits[:, [1, 2, 3, 4, 5, 6, 7, 0]]
    retro_arr = np.insert(retro_arr, 1, 0, axis=1)
    retro_arr = retro_arr[1:] # ignore first action - NOTE: I'm not sure why, but this makes it sync

    return actions, inverted, retro_arr

# for smb-native, as it doesn't simulate lag frames
def get_lag_frames(path: Path) -> set[int]:
    with open(path, "r", encoding="utf-8") as f:
        data = f.readlines()

    lags = set()
    for line in data:
        line = line.strip()
        if line == "":
            continue
        
        try:
            frame = int(line)
        except ValueError:
            raise ValueError(f"Invalid line {line} in: {path} (int cast failed)")
        
        lags.add(frame)

    return lags

def cpu_name() -> str:
    try:
        with open("/proc/cpuinfo", encoding="utf-8") as f:
            for line in f:
                if line.startswith("model name"):
                    return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor()

def run_tas(OUTPUT: Path, envname: str, shortname: str, runs: int, warmup: int, func: Callable[..., tuple[float, np.ndarray, int]], **kwargs) -> dict:
    for _ in range(warmup):
        func(**kwargs)
    
    results = [func(**kwargs) for _ in range(runs)] # stores (time, rendered frame, frame count)
    times = [r[0] for r in results]

    median = statistics.median(times)

    rendered_frame, framecount = results[-1][1:3] # -1 is arbitrary
    fps = framecount / median
    
    result = {
        "env": shortname,
        "framecount": framecount,
        "runs": runs,
        "times": times,
        "median": median,
        "min": min(times),
        "max": max(times),
        "fps": fps,
        "cpu": cpu_name(),
        "python": platform.python_version(),
        "timestamp": datetime.now().isoformat(timespec="seconds"),
    }

    # append in results file
    with open(OUTPUT / "results.jsonl", "a", encoding="utf-8") as f:
        f.write(json.dumps(result) + "\n")

    # print results
    png = OUTPUT / f"{shortname}_end.png"
    Image.fromarray(rendered_frame).save(png)

    print(f"{envname} is done:")
    print(f"  Took: {median:.3f}s")
    print(f"  FPS: {fps:.1f}")
    print(f"  Last rendered frame was saved as: {png}")

    return result

def bench_nes_py(rom_path: Path, actions: list[int]) -> tuple[float, np.ndarray, int]:
    env = NESEnv(str(rom_path))
    env.reset()

    t = perf_counter()

    # loop
    s = None
    for action in actions:
        s = env.step(action)[0]

    t = perf_counter() - t
    assert s is not None

    s = np.array(s) # copy
    env.close()

    return t, s, len(actions)

def bench_smb_native(actions: list[int], lag_frames: set[int]) -> tuple[float, np.ndarray, int]:
    env = SMBEnv()
    env.reset(preset="ColdBoot")

    t = perf_counter()

    # loop
    s = None
    for i, action in enumerate(actions):
        if i in lag_frames:
            continue
        
        env.set_input(action)
        env.step()

        s = env.get_frame(render_mode="rgb")

    t = perf_counter() - t
    assert s is not None

    return t, s, len(actions)

def bench_stable_retro(actions: np.ndarray) -> tuple[float, np.ndarray, int]:
    try:
        env = retro.make(
            game="SuperMarioBros-Nes-v0",
            state=retro.State.NONE,
            render_mode="rgb_array",
            use_restricted_actions=retro.Actions.ALL
        )
    except FileNotFoundError:
        raise ValueError("'SuperMarioBros-Nes-v0' not found, try running: python -m stable_retro.import {FOLDER WHERE YOUR ROM IS, e.g: 'assets/'}")

    env.reset()

    t = perf_counter()

    # loop
    s = None
    for action in actions:
        s = env.step(action)[0]

    t = perf_counter() - t
    assert s is not None

    env.close()

    # +1 to account for the first action removed
    return t, s, len(actions) + 1

if __name__ == "__main__":
    OUTPUT.mkdir(parents=True, exist_ok=True)

    ACTIONS, ACTIONS_INV, ACTIONS_NP = get_tas_actions(TAS_FM2_PATH)
    LAG_FRAMES = get_lag_frames(TAS_LAG_PATH)

    RUNS = 5
    WARMUP = 1

    print(f"Running each env {RUNS} times ({WARMUP} warmup)")

    t = perf_counter()

    nes_py_res = run_tas(
        OUTPUT,
        envname="\"nes-py\"/\"gym-super-mario-bros\"",
        shortname="nes_py",
        runs=RUNS,
        warmup=WARMUP,

        func=bench_nes_py,
        rom_path=ROM_PATH,
        actions=ACTIONS_INV
    )

    smb_native_res = run_tas(
        OUTPUT,
        envname="\"smb-native\"",
        shortname="smb_native",
        runs=RUNS,
        warmup=WARMUP,

        func=bench_smb_native,
        actions=ACTIONS,
        lag_frames=LAG_FRAMES
    )

    stable_retro_res = run_tas(
        OUTPUT,
        envname="\"stable-retro\"",
        shortname="stable_retro",
        runs=RUNS,
        warmup=WARMUP,

        func=bench_stable_retro,
        actions=ACTIONS_NP
    )

    print(f"\nTotal time elapsed: {perf_counter() - t:.2f}s")