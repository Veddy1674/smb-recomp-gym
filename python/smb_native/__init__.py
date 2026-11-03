"""
## Super Mario Bros. Environment - Low Level API <small>(PRIVATE)</small>
Repository: **https://github.com/Veddy1674/smb-recomp-gym**  
This module contains the low-level API for interacting with statically recompiled Super Mario Bros. in a gym-like environment.  
It is expected to work only for YOUR machine, it is NOT legal to distribute it as your ROM was used to compile the library.  

If you're looking for the actual Gymnasium API (>=1.2.0):  
`import smb_native.gym_env` registers 'SMB-v0' environment in Gymnasium.  
More info in the `smb_native.gym_env` module itself.
"""

try:
    """
    the .so is only installed into the site-packages when built with 'pip install',
    so the import below will cause a lint warning without the "type ignore",
    but it's fine because this file and the .so are copied into the site-packages,
    and are the one actually used when you import smb_native; no worries!
    """
    from ._smb_native import * # type: ignore
except ImportError as e:
    raise ImportError(
        "Library '_smb_native' not found.\n"
        "Please follow the instructions in the README.md to install it."
    ) from e

__all__ = ["SMBEnv", "Input"]