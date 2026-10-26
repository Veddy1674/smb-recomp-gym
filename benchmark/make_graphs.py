import json
from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np

# change this path according to your setup
OUTPUT = Path("benchmark/graphs/")
RESULTS = Path("benchmark/results.jsonl")

LABELS = {
    "nes_py": "nes-py\ngym-super-mario-bros",
    "stable_retro": "stable-retro",
    "smb_native": "smb-native",
}

# TODO revise and avoid globals, output as arg
def __make_single_env_fps_graph(results: dict):
    rows = sorted(results.values(), key=lambda r: r["fps"])
    base = results["nes_py"]["fps"]

    envs = []
    for r in rows:
        val = r["env"]
        env = LABELS.get(val, val)
        assert env is not None
        
        envs.append(env)

    fps = [r["fps"] for r in rows]
    colors = ["#f4892c" if r["env"] == "smb_native" else "#ceba61" for r in rows]

    fig, ax = plt.subplots(figsize=(9, 2.2 + 0.7 * len(rows)))
    bars = ax.barh(envs, fps, color=colors, height=0.55)
    
    ax.set_xlabel("Frames per second\nA frame is step() + rendering")

    ax.bar_label(bars, labels=[f"{v:,.0f} FPS ({v / base:.1f}x)" for v in fps], padding=4)
    ax.grid(axis="x", alpha=0.25)
    ax.set_axisbelow(True)

    for side in ("top", "right"):
        ax.spines[side].set_visible(False)

    r0 = rows[0]
    fig.text(
        x = -.05, y = -.1,
        s = f"{r0['cpu']} | 1 Instance | RGB Rendering",
        fontsize=8, color="#ADADAD",
    )

    fig.savefig(OUTPUT / "single_env_fps.svg", bbox_inches="tight")

if __name__ == "__main__":
    results = {}
    for line in RESULTS.read_text(encoding="utf-8").splitlines():
        r = json.loads(line)
        results[r["env"]] = r    
    
    __make_single_env_fps_graph(results)