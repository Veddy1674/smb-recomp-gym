"""
## Super Mario Bros. Environment - Low Level API <small>(PRIVATE)</small>
Repository: **https://github.com/Veddy1674/smb-recomp-gym**\n
This module contains the low-level API for interacting with statically recompiled Super Mario Bros. in a gym-like environment.  
It is expected to work only for THIS machine, it shouldn't be distributed, nor is legal to do so as your ROM was used to compile it.  

"""

try:
    from ._smb_native import *
except ImportError as e:
    raise ImportError(
        "Library '_smb_native' not found.\n"
        "Please follow the instructions in the README.md to install it."
    ) from e

__all__ = ["SMBEnv", "Input"]