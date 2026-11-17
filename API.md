### Gymnasium Wrapper
<sup>(Requires Gymnasium >= 1.2.0 and numpy, if you followed the building instructions correctly, you're all set)</sup>

```python
import gymnasium as gym
from gymnasium.error import ClosedEnvironmentError
import smb_native.gym_env as smb # registers 'SMB-v0'
gym.register_envs(smb) # no-op

env = gym.make(
    "SMB-v0",
    obs_type="rgb", # 'rgb', 'grayscale', 'indexed', 'ram'
    render_mode="human", # None, 'rgb_array', 'human'
    preset="W1L1", # 'ColdBoot', 'TitleScreen', 'W1L1' ... 'W8L4'
    frame_skip=4,
    action_set="simple", # 'right_only', 'simple', 'complex', list[int]
    max_episode_steps=800, # truncation is handled by gym's TimeLimit, infinite by default
)
obs, info = env.reset()

terminated = truncated = False
try:
    while not (terminated or truncated):
        obs, reward, terminated, truncated, info = env.step(env.action_space.sample())

except ClosedEnvironmentError as e: # window closed manually
    print(e)
  
finally:
    env.close()
```
<sup>This snippet requires `pygame` installed for the `human` render_mode to function.</sup>

> [!NOTE]
> `frame_skip` and `grayscale` rendering are **native**, so don't use the relative wrappers, they would only add overhead.

**`env.close()`** is not necessary unless you're using `human` render_mode, but is good practice.

**Observation** shapes (numpy arrays):
- `RGB`: **(240, 256, 3)** uint8
- `Grayscale`: **(240, 256)**, uint8
- `Indexed`: **(240, 256)** uint8 <small>(See [`renderer.cpp`](src/core/renderer.cpp) yourself for info)</small>
- `RAM`: **(2048,)** uint8 <small>(No rendering, so it's the fastest option)</small>

**Render Mode**:
- `None`: render() returns None
- `rgb_array`: render() always returns a RGB frame, regardless of obs_type
- `human`: renders the frame in a window at 60 FPS (smooth with frame_skip > 1 too), render() returns None

**Preset Mode**:  
What state to load when .reset() is called; all presets are created once when the `smb_native` library is imported, to avoid tiny overheads for each reset call.
- `ColdBoot` starts at the very beginning of the game, first few frames will be gray/black until the title screen appears.  
- `TitleScreen` starts at the title screen, start can be pressed right away to start the game.  
`W1L1` to `W8L4` starts at the beginning of the specified level (4 levels per world, 8 worlds)

**Frame Skip**:  
Native, runs NMI multiple times per step() directly in a C++ loop

**Action Set**:
`right_only`, `simple` and `complex` are pre-made action sets you can use.  
Alternatively, use your own action set as a `list[int]`, library `smb_native` exposes 'Input' IntFlag to easily create custom action sets, for example:
```python
from smb_native import Input

my_actions = [
    Input.DOWN, # down
    Input.RIGHT | Input.B, # right + B
    Input.RIGHT | Input.A, # right + A
    Input.RIGHT | Input.B | Input.A, # right + B + A
]
```

**Custom reward:**  
Pass `reward_function=func` as argument, with `func(prev_state: SMBEnv, curr_state: SMBEnv) -> float`.  
<sub>`curr_state` is the live env, not a copy! So treat it as read-only to avoid issues.</sub>  
<sup>`SMBEnv` is the raw environment created by the low-level Python API, [so have a look at its usage for more details](#python-low-level-api).</sup>

**`info` contains:**
- `x`: Absolute X position of Mario
- `y`: Absolute/Screen Y position of Mario
- `time`: Remaining time in the level
- `lives`: Number of lives Mario has
- `coins`: Number of coins Mario has
- `score`: Current score, not highscore
- `mario_state`: Mario's current raw state (see [here](https://datacrystal.tcrf.net/wiki/Super_Mario_Bros./RAM_map) at address **0x000E**)
- `win`: True when the flagpole/axe is touched

To unwrap the environment and access the native low-level API, use `unwrap(env)` from `smb_native.gym_env`:
```python
from smb_native import SMBEnv
from smb_native.gym_env import unwrap

base: SMBGymEnv = unwrap(env) # has typing from gym_env.py
native: SMBEnv = base.native # uses python low-level API

# unwrapping is useful to read and write to RAM, which can be accessed
# from SMBGymEnv and SMBEnv, because SMBGymEnv has a @property method for it:
base.ram[0x075e] = 99
native.ram[0x075e] = 99

print(native.get_coins())
```
Or use `.unwrapped` but gym.make returns wrapped envs typed as `gym.Env`, so IDEs can't autocomplete `native` / `ram`.

> [!WARNING]
> **Vectorization is WIP.**  
> [`SMBEnvVec` was just implemented](#native-vectorization), but not the relative Gymnasium wrapper for it.  
> For now you can use Gymnasium's own `gym.make_vec`.

<!-- ### Gymnasium Vectorization

*** -->

### Python low-level API
The [Gymnasium Wrapper](#gymnasium-wrapper) is built on top of this.

```python
import numpy as np
from smb_native import SMBEnv, Input

env = SMBEnv()
env.reset(preset="W1L1") # load level 1-1

env.step(Input.RIGHT | Input.B, 10) # hold RIGHT + B for 10 frames

action = Input.B | Input.RIGHT | Input.LEFT

# invalid actions such as RIGHT + LEFT and UP + DOWN can be validated with:
action = env.validate_input(action) # result is Input.B alone

env.step(action)

frame = env.get_frame(render_mode="rgb") # allocs a new (240, 256, 3) numpy array

# or render into a preallocated buffer
buf = np.empty((240, 256, 3), dtype=np.uint8)
env.get_frame(buf, render_mode="grayscale")

print("Horizontal Speed: ", env.ram[0x0057]) # 2KB RAM, zero-copy view (not a copy!)
print("Coins: ", env.get_coins()) # utility that reads the correct address automatically

# savestates are quick copies of RAM + PPUState
save = env.copy()
env.step(Input.LEFT, 60)
env = save.copy()

if env == save: # RAM comparison
    print("'env' equals 'save' byte per byte!")

print("Hash: ", hash(env)) # hashing is shallow, confirm with == on collision!

envs = [env.copy() for _ in range(10)]
for i in range(len(envs) - 1): # step all but last
    envs[i].step(Input.A, 10)
  
if env.equals_any(envs):
    print("Found a match!")
```

**Summary:**  
To step the game logic, use **`step(action: int, times: int = 1)`**, you can use `Input` IntFlag for actions, which is casted to an int automatically.

To render a frame, use **`get_frame(render_mode: str = "rgb")`** or **`get_frame(buf: np.ndarray, render_mode: str = "rgb")`**, available options are: `rgb`, `grayscale`, `indexed`; rendering is optional after a step.

You can also render only specific sprites using **`render_sprite(first_slot: int, count: int)`** - It returns a numpy array as RGBA uint8 with the specified metasprite slots rendered (and transparent background), and two integers which represent the exact X and Y position in pixels where the sprite is supposed to be; it does not render the whole screen so it's much faster than `get_frame`, and it doesn't have a variant with buffer argument (as it doesn't have much use cases anyway), `render_sprite_mario()` automatically renders Mario's metasprites (8), frame shape is always (32, 16, 4) - **Note that returned image (first element of the tuple) can be None if the sprite is not found/visible, x and y will have a value of 0 in that case**.

**`env.ram`** is a view (not a copy) of the game's RAM (0x800 or 2048 bytes), can be read and written into.  
All the RAM-reading utilities:  
\- **get_mario_state()** (See [here](https://datacrystal.tcrf.net/wiki/Super_Mario_Bros./RAM_map) at address **0x000E**)  
\- **is_death_state()** (True if **get_mario_state()** equals 0x0B or 0x06, or **get_viewport_y()** > 1)  
\- **is_win_state()** (True when the flagpole/axe is touched)  
\- **get_terminated()** (True if **is_death_state()** or **is_win_state()**)  
\- **get_lives()**  
\- **get_coins()**  
\- **get_time()**  
\- **get_score()**  
\- **get_high_score()**  
\- **get_viewport_y()** (1 = inside, 0 = above, >1 = below, e.g. falling into a pit)  
\- **get_abs_y()** (Equivalent to Screen Y, sorta)  
\- **get_abs_x()** (Absolute X Position of Mario in the level: **(ram[0x6D] << 8) | ram[0x86]**)  
\- **get_screen_x()** (Mario's X position on the screen from the left edge)  

The environment's state is the RAM itself, it can be copied with **`copy()`** (which also copies the PPUState), and it can be compared with **`equals(env: SMBEnv)`** or **`equals_any(envs: Iterable[SMBEnv])`** (Which only compare the RAM).
<!-- TODO add copy_at(env) ??? -->

The environment's state can be hashed shallowly with `hash(env)`, so a `set` of environments can be made (for example in a search algorithm to avoid duplicates), but upon collision the two states should be compared with `.equals()` or `==` which natively compares via `std::memcmp`, to avoid false positives.  
Hashing only applies to RAM, not PPUState.

#### Native Vectorization

`SMBEnvVec` features native C++ vectorization using OpenMP, if the project [was built without it](README.md#requirements), it will function single-threadedly.  
The syntax is very close to the `SMBEnv` class, similar methods end with suffix "_all".

```python
from smb_native import SMBEnvVec, Input
import numpy as np

envs = SMBEnvVec(n_envs=64) # create 64 instances at once
envs.reset_all(preset="W1L1") # uses multi-threading

action_space = [
    Input.RIGHT,
    Input.LEFT
]

# array of n_envs actions randomly chosen from action_space
actions = np.random.choice(action_space, size=envs.n_envs)

# pre-allocated buffer (optional)
frames = np.empty((envs.n_envs, 240, 256, 3), dtype=np.uint8)

# step all 64 environments at once, each with its own action, frameskip 4
envs.step_all(actions, times=4) # uses multi-threading

# get 64 240x256 RGB uint8 frames at once
envs.get_frame_all(frames, render_mode="rgb") # uses multi-threading

envs.at(0).reset(preset="ColdBoot") # reset one single environment

# direct indexing and slicing works as well
for env in envs[1:3]:
    env.reset(preset="ColdBoot")

# access RAM and get/set values, multiple instances at once
envs.ram_all[:, 0xce] = 20 # (this changes mario absolute Y position)

# get env idx with highest abs_x
all_x = envs.get_abs_x_all()
idx = np.argmax(all_x)

print(f"Env {idx + 1}/{envs.n_envs} has abs X = {all_x[idx]}")

ram_copy = envs.get_ram_all() # copy of envs.ram_all, contiguous (n_envs, 2048) uint8

# iterate through every environment (type Iterable[SMBEnv])
for i, env in enumerate(envs, start=1):
    if i % 8 == 0:
        print(f"Env {i} has abs Y = {env.get_abs_y()}")
```

**Notes:**  
\- **`.reset_all(...)`**, **`.step_all(...)`**, **`.get_frame_all(...)`** use multi-threading, utilities that access RAM, such as **`.get_abs_x_all()`** are serial on purpose, as array accessing is usually faster this way.  
\- Every environment can be individually accessed via `direct indexing`, `slicing`, or with **`at(index: int)`**; `SMBEnvVecIterator` iterates through all the environments.  
\- **`.n_envs`** read-only property is the amount of environments the class is managing, initialized in the constructor; Equivalent to `len()`.  
\- **`.num_threads()`** returns the amount of threads used for multi-threading, by default it's the amount of virtual processors your main CPU has, if you built without OpenMP, it returns 1.

***

### C++ shared library
All the documentation here is equivalent to [Python low-level API](#python-low-level-api) (except methods use PascalCase instead of snake_case), including vectorization, so have a look there.  
Using the C++ library is usually preferred for better raw performance over the Python API or the [Gymnasium Wrapper](#gymnasium-wrapper).
```cpp
#include <smb/core/env.hpp>

int main() {
    SMBEnv::InitResetPresets(); // optional, avoids a slow first reset()

    SMBEnv env;
    env.Reset(ResetPreset::W1L1); // load level 1-1
    env.Step(Input::RIGHT | Input::B, 10); // hold RIGHT + B for 10 frames 

    uint8_t frame[FrameBytesOf(RenderMode::RGB)];
    env.RenderFrame(frame, RenderMode::RGB);
}
```

***

### Continue in [README.md](README.md) to see [Benchmark Results](README.md#benchmark-results) or [Limitations](README.md#limitations).