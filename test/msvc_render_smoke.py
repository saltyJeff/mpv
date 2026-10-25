#!/usr/bin/env python3
"""Exercise VKSF rendering, not just decoding via vo=null.

Requires Windows, Python's Tk module, and a working Vulkan driver. Kept opt-in
so headless build machines can still run msvc_smoke.py without a GPU.
"""

import argparse
import ctypes as ct
from pathlib import Path
import tempfile
import threading
import time
import tkinter as tk

from msvc_smoke import Event, bind


class NativeWindow(ct.Union):
    _fields_ = [("pointers", ct.c_void_p * 3)]


class Window(ct.Structure):
    _fields_ = [("platform", ct.c_int), ("window", NativeWindow)]


EventCallback = ct.CFUNCTYPE(ct.c_int, ct.c_void_p, ct.POINTER(Event), ct.c_void_p)
LogCallback = ct.CFUNCTYPE(
    None, ct.c_void_p, ct.c_char_p, ct.c_char_p, ct.c_int, ct.c_char_p, ct.c_void_p,
)


def wait_for(root, condition):
    deadline = time.monotonic() + 20
    while time.monotonic() < deadline:
        root.update()
        if condition():
            return
        time.sleep(0.01)
    raise AssertionError("Timed out waiting for Vulkan rendering")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    dll = ct.CDLL(str(args.library.resolve()))
    root = tk.Tk()
    root.title("mpv VKSF rendering smoke test")
    root.geometry("640x480")
    root.update()
    get_parent = bind(ct.windll.user32, "GetParent", ct.c_void_p, ct.c_void_p)
    window = Window(2, NativeWindow((ct.c_void_p * 3)(
        None, get_parent(root.winfo_id()), None,
    )))
    restarted = threading.Event()
    messages = []

    @EventCallback
    def event_callback(_, event, userdata):
        if event.contents.id == 21:  # MPV_EVENT_PLAYBACK_RESTART
            restarted.set()
        return 0

    @LogCallback
    def log_callback(_, prefix, level, severity, text, userdata):
        messages.append((severity, text.decode("utf-8", errors="replace")))

    pair = ct.c_char_p * 2
    settings = {
        b"config": b"no", b"ao": b"null", b"hwdec": b"no",
        b"keep-open": b"yes", b"gpu-shader-cache": b"no",
    }
    options = (pair * (len(settings) + 1))(
        *(pair(key, value) for key, value in settings.items()), pair(None, None),
    )
    create = bind(
        dll, "mpv_vksf_create", ct.c_void_p, ct.POINTER(Window), ct.POINTER(pair),
        EventCallback, LogCallback, ct.c_void_p,
    )
    command = bind(dll, "mpv_vksf_command", ct.c_int, ct.c_void_p, ct.POINTER(ct.c_char_p))
    destroy = bind(dll, "mpv_vksf_destroy", None, ct.c_void_p)
    handle = None
    try:
        handle = create(ct.byref(window), options, event_callback, log_callback, None)
        assert handle, "VKSF initialization failed"
        with tempfile.TemporaryDirectory(prefix="mpv-vksf-render-") as directory:
            for codec in ("h264", "hevc", "vp9", "av1"):
                restarted.clear()
                start = len(messages)
                fixture = Path(__file__).parent / "fixtures" / f"{codec}.mkv"
                assert command(handle, (ct.c_char_p * 3)(
                    b"loadfile", str(fixture.resolve()).encode("utf-8"), None,
                )) >= 0
                wait_for(root, restarted.is_set)
                image = Path(directory) / f"{codec}.png"
                assert command(handle, (ct.c_char_p * 4)(
                    b"screenshot-to-file", str(image).encode("utf-8"), b"window", None,
                )) >= 0
                wait_for(root, image.exists)
                # The log callback runs on a separate thread. Allow queued render
                # errors to arrive before accepting a screenshot as success.
                until = time.monotonic() + 0.2
                wait_for(root, lambda: time.monotonic() >= until)
                errors = [text for severity, text in messages[start:]
                          if severity <= 20 or "Falling back to software screenshot" in text]
                assert not errors, "".join(errors)
                assert image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
                print(f"PASS: VKSF Vulkan render and GPU screenshot ({codec})")
    finally:
        if handle:
            destroy(handle)
        root.destroy()


if __name__ == "__main__":
    main()
