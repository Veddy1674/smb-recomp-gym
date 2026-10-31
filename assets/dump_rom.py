from pathlib import Path
import hashlib
import json

ROM_SIZE = 40976 - 16 # ~40KB excluding iNES header
ROM_HASH = "facee9c577a5262dbe33ac4930bb0b58c8c037f7" # SHA-1

PRG_SIZE = 0x8000 # first 32KB
CHR_SIZE = 0x2000 # other 8KB

TAB = "    " # using spaces instead of \t

def sha1(a: bytes) -> str:
    return hashlib.sha1(a).hexdigest()

def extract_rom(path: Path, prg_rom_data: bytearray, chr_rom_data: bytearray) -> bool:
    if not path.is_file():
        print(f"Error: ROM invalid or missing ({path})")
        return False

    buffer = bytearray()
    try:
        # read as binary
        buffer = path.read_bytes()[16:] # skip iNES header
    except Exception:
        print(f"Error: Unable to read ROM ({path})")
        return False

    # if size and hash match
    if len(buffer) == ROM_SIZE and sha1(buffer) == ROM_HASH:
        prg_rom_data[:] = buffer[:PRG_SIZE]
        chr_rom_data[:] = buffer[PRG_SIZE : PRG_SIZE + CHR_SIZE]
        return True
    
    print(f"Error: ROM size or hash mismatch ({path})")
    return False

def write_cpp_file(out: Path, manifest: Path, prg_rom_data: bytearray, chr_rom_data: bytearray):
    # header
    result = "#pragma once\n#include <cstdint>\n\n"

    # make a huge array with chr_rom_data
    result += (
        "alignas(64)\n"
        "inline constexpr uint8_t CHR_ROM_DATA[0x2000] = {\n"
    )

    for i in range(0, CHR_SIZE, 16):
        # excluding the last 5 unused bytes would cause rendering issues
        row = chr_rom_data[i:i+16]
        row = ", ".join(f"0x{b:02x}" for b in row) # 16 bytes

        result += f"{TAB}{row},\n"

    result += "};\n\n"

    # make a huge array with prg_rom_data
    result += (
        "alignas(64)\n"
        "#ifdef REMOVE_RESIDUAL\n"
        "// remove music/sfx related data (which is regardless unreferenced in the recompilation)\n"
        "inline constexpr uint8_t PRG_ROM_DATA[0x7239] = {\n"
        "#else\n"
        "inline constexpr uint8_t PRG_ROM_DATA[0x8000] = {\n"
        "#endif\n"
    )

    for i in range(0, PRG_SIZE, 16):
        # from index 0x7239 the music/sfx data begins, which is skipped if REMOVE_RESIDUAL
        if i == 0x7230:
            first_half = ", ".join(f"0x{b:02x}" for b in prg_rom_data[i:i+9]) # 9 bytes
            second_half = ", ".join(f"0x{b:02x}" for b in prg_rom_data[i+9:i+16]) # 7 bytes

            result += (
                f"{TAB}{first_half},\n"
                "#ifndef REMOVE_RESIDUAL // removing all of music/sfx-related data\n"
                f"{TAB}{second_half},\n"
            )
            continue
        
        row = prg_rom_data[i:i+16]
        row = ", ".join(f"0x{b:02x}" for b in row) # 16 bytes

        result += f"{TAB}{row},\n"
    
    result += (
        "#endif\n"
        "};\n\n"
    )

    # make uint8_t pointers (defined in the disassembly), points to prg_rom_data + offset
    try:
        with manifest.open("r") as f:
            manifest_data = json.load(f)
            
            for arr in manifest_data:
                arr_details = manifest_data[arr]

                offset = arr_details["offset"] # hexadecimal
                # size = int(arr_details["size"]) # decimal (unused)

                result += f"inline constexpr uint8_t const* {arr} = PRG_ROM_DATA + {offset};\n"

    except Exception as e:
        print(f"An exception occurred while trying to read '{manifest}': {e}")
        return False
    
    out.write_text(result)
    return True

if __name__ == "__main__":
    import sys

    prg_rom = bytearray()
    chr_rom = bytearray()

    BASE_DIR = Path(__file__).resolve().parent # assets/
    ROOT = BASE_DIR.parent # project root

    ROM = BASE_DIR / "smb.nes"

    INCLUDE_DATA = ROOT / "include" / "smb" / "data"
    OUT = INCLUDE_DATA / "game_data.hpp"

    # custom json file that only contains the indexes at which
    # the data arrays are found in prg_rom_data (and their size)
    MANIFEST = INCLUDE_DATA / "rom_manifest.json"

    if not extract_rom(ROM, prg_rom, chr_rom):
        sys.exit(1)
    
    if not write_cpp_file(OUT, MANIFEST, prg_rom, chr_rom):
        sys.exit(1)

    print("ROM extracted successfully!")