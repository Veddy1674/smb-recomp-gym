"""
### Gymnasium API (>=1.2.0) - Wrapper for `smb_native`.

Usage:  
```python
import gymnasium as gym
from gymnasium.error import ClosedEnvironmentError
import smb_native.gym_env as smb # registers 'SMB-v0'

gym.register_envs(smb) # no-op

# native grayscale and frame_skip
env = gym.make("SMB-v0",
    obs_type="grayscale", # will return obs with shape (240, 256), natively
    render_mode="human", # will render RGB frames
    preset="W1L1", # loads world 1-1
    frame_skip=4, # native frame_skip
    action_set="simple"
)
obs, info = env.reset()

terminated = False
try:
    while not terminated:
        obs, reward, terminated, truncated, info = env.step(env.action_space.sample())
except ClosedEnvironmentError as e:
    print(e)
finally:
    env.close()
```
"""

from collections.abc import Sequence
from typing import Any, Literal, Callable, get_args
from functools import wraps

import numpy as np
import gymnasium as gym
from gymnasium import spaces
from gymnasium.error import ClosedEnvironmentError

from . import Input, SMBEnv

ObsKind = Literal["rgb", "grayscale", "indexed", "ram"]
OBS_TYPES = get_args(ObsKind)

ACTION_SETS: dict[str, list[Input]] = {
    "right_only": [
        Input.RIGHT,
        Input.RIGHT | Input.A,
        Input.RIGHT | Input.B,
        Input.RIGHT | Input.A | Input.B,
    ],
    "simple": [
        Input.NONE,
        Input.RIGHT | Input.B,
        Input.RIGHT | Input.A | Input.B,
        Input.LEFT | Input.B,
        Input.LEFT | Input.A | Input.B,
    ]
}

ACTION_SETS["complex"] = [
    *ACTION_SETS["simple"],
    Input.A,
    Input.LEFT | Input.A,
    Input.RIGHT | Input.A,
    Input.DOWN,
]

WINDOW_SIZE = (256 * 2, 240 * 2)  # for human rendering

# prev_state, curr_state, returns reward
RewardFunction = Callable[[SMBEnv, SMBEnv], float]

# decorator to raise an exception if method is called after env.close()
def _not_closed(fn):
    @wraps(fn)
    def wrapper(self, *args, **kwargs):
        if self._closed:
            raise ClosedEnvironmentError(f"{fn.__name__}() called on a closed env")
        return fn(self, *args, **kwargs)
    return wrapper

class SMBGymEnv(gym.Env[np.ndarray, np.int64]):
    # 'rgb_array' also supports all OBS_TYPES (e.g. grayscale)
    metadata = {"render_modes": ["rgb_array", "human"], "render_fps": 60}

    # defining here in case __init__ fails and Env.__del__ calls .close()
    # which would cause further exceptions such as AttributeError
    _closed = False

    # for human rendering
    window_size = WINDOW_SIZE
    _window = None
    _clock = None

    def __init__(
            self,
            obs_type: ObsKind = "rgb",
            action_set: str | Sequence[Input] = "simple",
            preset: str = "W1L1",
            frame_skip: int = 1,
            render_mode: str | None = None,
            reward_function: RewardFunction | None = None,
        ):
        super().__init__()

        if obs_type not in OBS_TYPES:
            raise ValueError(f"Invalid obs_type '{obs_type}', should be: {OBS_TYPES}")
        
        if frame_skip < 1:
            raise ValueError(f"Invalid frame_skip '{frame_skip}', should be >= 1")

        render_modes = (None, *self.metadata["render_modes"])
        if render_mode not in render_modes:
            raise ValueError(f"Invalid render_mode '{render_mode}', should be: {render_modes}")

        self.obs_type = obs_type
        self.preset = preset
        self.frame_skip = frame_skip
        self.render_mode = render_mode

        self._env = SMBEnv()
        self._env.reset(self.preset)

        self._actions = self._resolve_actions(action_set)

        self.action_space = spaces.Discrete(len(self._actions))
        self.observation_space = self._make_obs_space(self.obs_type)

        self.reward_function = reward_function or self._default_reward

    # helpers
    @property
    def native(self) -> SMBEnv:
        return self._env

    @property
    def ram(self) -> np.ndarray:
        return self._env.ram # view, not a copy!

    @staticmethod
    def _resolve_actions(action_set: str | Sequence[Input]) -> list[int]:
        if isinstance(action_set, str):
            if action_set not in ACTION_SETS:
                raise ValueError(
                    f"unknown action_set '{action_set}', options: {list(ACTION_SETS)}"
                )
            action_set = ACTION_SETS[action_set]

        if len(action_set) == 0:
            raise ValueError("action_set can't be empty")

        return [int(a) for a in action_set]

    @staticmethod
    def _make_obs_space(obs_type: ObsKind) -> spaces.Box:
        if obs_type == "ram":
            shape = (0x800,)

        elif obs_type == "rgb":
            shape = (240, 256, 3)

        else: # grayscale/indexed
            shape = (240, 256)
 
        return spaces.Box(low=0, high=255, shape=shape, dtype=np.uint8)

    # private:
    # render frame to screen, for human rendering
    def _show_frame(self) -> None:
        import pygame

        # always render in rgb! rendering in grayscale works (thanks to .convert() on surf)
        # but it's better to keep it strictly rgb if the agent sees in grayscale (then human
        # rendering would also be forced to grayscale)
        frame = self._env.get_frame("rgb")

        if frame.ndim == 2: # grayscale to rgb for pygame rendering
            frame = np.repeat(frame[:, :, None], 3, axis=2)

        # init
        if self._window is None:
            pygame.display.init() # only init display
            self._window = pygame.display.set_mode(self.window_size)
            self._clock = pygame.time.Clock()

        # rendering:
        H, W = frame.shape[:2]
        surf = pygame.image.frombuffer(frame, (W, H), "RGB").convert()

        # upscale
        pygame.transform.scale(surf, self.window_size, self._window)

        # handle X to quit
        for event in pygame.event.get():
            if event.type == pygame.QUIT:
                self.close()
                raise ClosedEnvironmentError("Window closed manually by user")

        pygame.display.flip()

        assert self._clock is not None # for pyright
        self._clock.tick(self.metadata.get("render_fps", 60))
    
    def _get_obs(self) -> np.ndarray:
        if self.obs_type == "ram":
            return self._env.ram.copy()
        
        return self._env.get_frame(self.obs_type) # allocs every call!

    # used just for testing & monitoring
    def _get_info(self) -> dict[str, Any]:
        env = self._env
        return {
            "x": env.get_abs_x(),
            "y": env.get_abs_y(),
            "time": env.get_time(),
            "lives": env.get_lives(),
            "coins": env.get_coins(),
            "score": env.get_score(),
            "mario_state": env.get_mario_state(),
            "win": env.is_win_state(),
        }

    def _default_reward(self, prev_state: SMBEnv, curr_state: SMBEnv) -> float:
        reward = 0.0

        # horizontal speed:
        # right: 0 to 24 (walk) to 40 (run)
        # left: 255 to 232 (walk) to 216 (run)
        h_speed = int(curr_state.ram[0x0057])

        is_positive = h_speed <= 127
        h_speed = h_speed if is_positive else h_speed - 256 # unsigned to signed
        h_speed /= 40.0 # normalize

        # give less penalty for negative speed to encourage exploration
        reward += h_speed if is_positive else h_speed * 0.5

        # time penalty:
        reward += -0.03

        if curr_state.is_death_state():
            return -8.0
        
        if curr_state.is_win_state():
            return 25.0

        return reward

    # gymnasium api:
    @_not_closed
    def reset(
            self,
            *,
            seed: int | None = None,
            options: dict[str, Any] | None = None,
        ) -> tuple[np.ndarray, dict[str, Any]]:
        
        super().reset(seed=seed) # the game itself is deterministic, so it applies on numpy

        preset = (options or {}).get("preset", self.preset)
        self._env.reset(preset)

        if self.render_mode == "human":
            self._show_frame()

        return self._get_obs(), self._get_info()

    @_not_closed
    def step(self, action: np.int64) -> tuple[np.ndarray, float, bool, bool, dict[str, Any]]:
        action_bits = self._actions[int(action)]

        # TODO new binding to memcpy into a preallocated env state
        prev_state = self._env.copy()

        if self.render_mode == "human":
            for _ in range(self.frame_skip): # one frame at a time so it looks smooth
                self._env.step(action_bits, 1)
                self._show_frame()
        else:
            self._env.step(action_bits, self.frame_skip)

        terminated = self._env.get_terminated() # win or death

        # self._env is passed without copy but it should not be modified, as it would affect the actual env
        reward = self.reward_function(prev_state, self._env)

        # truncated is forced False, TimeLimit wrapper handles that
        return self._get_obs(), reward, terminated, False, self._get_info()

    @_not_closed
    def render(self) -> np.ndarray | None:
        if self.render_mode == "rgb_array":
            return self._env.get_frame("rgb")
        
        # human rendering is done in step(), so it returns None
        return None

    def close(self) -> None:
        self._closed = True
        # destroy window if human rendering
        if self._window is not None:
            import pygame
            pygame.display.quit()

            self._window = None
            self._clock = None

# helper to obtain .unwrapped
def unwrap(env: gym.Env) -> SMBGymEnv:
    base = env.unwrapped
    assert isinstance(base, SMBGymEnv) # for pyright
    return base

gym.register(
    id="SMB-v0",
    entry_point=f"{__name__}:SMBGymEnv",
    # max_episode_steps=2000, # depends on lots of factors, so it should be chosen by the user
)