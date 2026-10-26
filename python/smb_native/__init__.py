# wrapper for __smb_native__.so

try:
    from . import __smb_native__ as smb # type: ignore
except ImportError:
    raise ImportError("Library '__smb_native__' not found.\nPlease follow the instructions in the README.md to install it.")

from typing import Literal, Optional
from enum import IntFlag
import numpy as np

class Input(IntFlag):
    NONE = 0
    RIGHT = 1
    LEFT = 2
    DOWN = 4
    UP = 8
    START = 16
    SELECT = 32
    B = 64
    A = 128

class SMBEnv:
    def __init__(self):
        # init presets
        self.env = smb.SMBEnv()
        self.ram: np.ndarray = self.env.ram

    def reset(self, preset: Literal["ColdBoot", "TitleScreen", "W1L1"] = "TitleScreen"):
        # assert hasattr(smb.ResetPreset, preset), f"Invalid preset: '{preset}'"
        
        # reset (sets current state to preset state)
        # TODO FIX BECAUSE HASATTR PASSES FOR "name" AND "value"! check against smb.ResetPreset.__members__ or such
        self.env.reset(getattr(smb.ResetPreset, preset))

    def step(self, times: int = 1):
        self.env.step(times)

    def set_input(self, action: int):
        self.env.set_input(action)

    def get_frame(self, render_mode: Literal["indexed", "rgb", "grayscale"] = "rgb",) -> np.ndarray:
        return self.env.get_frame(getattr(smb.RenderMode, render_mode))

    def render_sprite_at(self, first_slot: int, count: int) -> np.ndarray:
        return self.env.render_sprite_at(first_slot, count)

    # utils:
    def render_mario(self) -> np.ndarray:
        return self.render_sprite_at(self.ram[0x06e4] >> 2, 8)

    # supposed to be called before set_input() to disallow directional invalid actions like left + right
    # this serves to simulate a real NES controller that physically disallows this
    def validate_input(self, action: int) -> int:
        if action & Input.LEFT and action & Input.RIGHT:
            action &= ~(Input.LEFT | Input.RIGHT) # remove both (no left nor right)

        if action & Input.UP and action & Input.DOWN:
            action &= ~(Input.UP | Input.DOWN) # remove both (no up nor down)

        return action

    def get_status(self) -> int:
        return self.ram[0x000E] # e.g: 0x0B = "Dying", 0x08 = "Normal", 0x04 = "Autowalk from flagpole"

    def get_terminated(self) -> tuple[bool, bool]:
        status = self.get_status()
        return (
            status == 0x0B or self.ram[0x00B5] > 0x01, # dying state or below viewport
            status == 0x04 # victory (flagpole touched)
        )

    def get_abs_x(self) -> int:
        return int(self.ram[0x006D]) << 8 | int(self.ram[0x0086])

    def get_abs_y(self) -> int: # same as screen y
        return int(self.ram[0x00CE]) + 1

    def get_screen_x(self) -> int:
        return int(self.ram[0x03AD]) # relative to screen's left edge

    def get_high_score(self) -> int:
        hs = 0
        
        for i in range(6):
            # exponent 6 (million) to 1 (10)
            hs += int(self.ram[0x07D7 + i]) * (10 ** (6 - i))
            
        return hs

    def get_real_ram_usage(self) -> int:
        return self.env.get_real_ram_usage() # returns a constant (56 bytes + sizeof(SMBEnv) in C++)

    def copy(self) -> "SMBEnv":
        # clone wrapper
        wrapper = SMBEnv.__new__(SMBEnv)
        
        wrapper.env = self.env.copy() 
        wrapper.ram = wrapper.env.ram
        
        return wrapper

    def get_hash(self) -> bytes:
        return bytes(self.ram.data)

    def __hash__(self) -> int:
        return hash(self.get_hash()) # by itself this does NOT guarantee uniqueness, use equals() when two hashes match

    # note that .equals natively use memcmp which is faster than comparing hashes
    def equals(self, other: "SMBEnv") -> bool:
        return self.env.equals(other.env)

    def equals_any(self, envs: list["SMBEnv"]) -> bool:
        for env in envs: # TODO iterate via cpp
            if self.equals(env):
                return True

        return False

    def __eq__(self, other: object) -> bool:
        if not isinstance(other, SMBEnv):
            return False
        
        return self.equals(other)