"""Takes the README screenshots (docs/images/*.png) from the portable demo app made by tools/make_demo.py.

Each scene writes the demo's session/config, starts build/demo/TwinDeck.exe with TWINDECK_SHOT set (see
App::ApplyScreenshotScene), captures the window with PrintWindow (no focus or input needed) and closes it.

usage: python tools/make_demo.py && python tools/shots.py [scene ...]
"""
import json
import os
import subprocess
import sys
import time

from PIL import Image

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
APP = os.path.join(ROOT, "build", "demo")
EXE = os.path.join(APP, "TwinDeck.exe")
FILES = os.path.join(ROOT, "build", "demo-files")
OUT = os.path.join(ROOT, "docs", "images")
F = lambda *p: os.path.join(FILES, *p)


def tab(path, view=0, icon=96, group=0, sort=0, desc=False):
    return {"Path": path, "SortIndex": sort, "Descending": desc, "Group": group, "View": view, "IconSize": icon}


def panel(*tabs, active=0):
    return {"Panel": list(tabs), "SelectedTab": active}


def split(a, b, ratio=0.5, vertical=False):
    return {"Vertical": vertical, "SplitRatio": ratio, "Child": a, "Next": b}


BASE_CFG = {"Language": 2, "ColorScheme": 1, "FontName": "Segoe UI", "FontSize": 10.0, "SetupPrompted": True,
            "ShowRecents": True, "Backdrop": 0}

SCENES = {
    # name: (layout, inspector, config overrides, TWINDECK_SHOT)
    "hero": (split(panel(tab(F("문서")), tab(F("프로젝트")), active=0), panel(tab(F("사진"), view=3, icon=96)), 0.48),
             False, {}, "select=회의록_10월.txt"),
    "korean": (panel(tab(F("문서"))), True, {}, "select=회의록_10월.txt;filter=ㅎㅇㄹ"),
    "views": (panel(tab(F("사진"), view=3, icon=150)), False, {}, "view"),
    "light": (panel(tab(F("문서"), group=2)), True, {"ColorScheme": 2}, "select=2026 사업계획서.docx"),
    "palette": (panel(tab(FILES)), False, {}, "palette=보기"),
    "setup": (panel(tab(FILES)), False, {}, "setup"),
}


def run_scene(name):
    layout, inspector, cfg_over, shot = SCENES[name]
    cfg = dict(BASE_CFG, **cfg_over)
    json.dump(cfg, open(os.path.join(APP, "TwinDeck-Config.json"), "w", encoding="utf-8"), ensure_ascii=False)
    session = {"Version": 1, "WindowX": 60, "WindowY": 40, "WindowWidth": 1700, "WindowHeight": 1040,
               "WindowMaximized": False, "QuickAccessWidth": 210, "QuickAccessClosed": False,
               "InspectorOpen": inspector, "InspectorSplitRatio": 330, "SelectedPanel": 0, "Layout": layout}
    json.dump(session, open(os.path.join(APP, "TwinDeck-Session.json"), "w", encoding="utf-8"), ensure_ascii=False)
    env = dict(os.environ, TWINDECK_SHOT=shot)
    proc = subprocess.Popen([EXE], cwd=APP, env=env)
    try:
        time.sleep(3.5)  # folder listing, icons and thumbnails load in the background
        raw = os.path.join(ROOT, "build", f"shot_{name}.png")
        subprocess.run(["powershell", "-NoProfile", "-File", os.path.join(ROOT, "tools", "shot.ps1"), "-Out", raw, "-Exe", EXE],
                       check=True, capture_output=True)
    finally:
        proc.kill()
        proc.wait()
    img = Image.open(raw).convert("RGBA")
    img = img.crop(img.getchannel("A").getbbox())
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, f"{name}.png")
    img.convert("RGB").save(out, optimize=True)
    print("saved", out, img.size)


if __name__ == "__main__":
    for n in sys.argv[1:] or list(SCENES):
        run_scene(n)
