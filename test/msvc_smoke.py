#!/usr/bin/env python3
"""Exercise the built DLL's public ABI, UTF-8 paths, decoding and HTTP transport.

Runs without third-party Python packages or media downloads. Rendering and
hardware decoding require a separate test on a machine with a Vulkan driver.
"""

import argparse
import ctypes as ct
import functools
import http.server
from pathlib import Path
import struct
import tempfile
import threading
import time
import wave
import zlib


class Event(ct.Structure):
    _fields_ = [
        ("id", ct.c_int), ("error", ct.c_int),
        ("userdata", ct.c_uint64), ("data", ct.c_void_p),
    ]


class EndFile(ct.Structure):
    _fields_ = [("reason", ct.c_int), ("error", ct.c_int)]


class LogMessage(ct.Structure):
    _fields_ = [("prefix", ct.c_char_p), ("level", ct.c_char_p), ("text", ct.c_char_p)]


def bind(dll, name, result, *arguments):
    function = getattr(dll, name)
    function.restype = result
    function.argtypes = arguments
    return function


class Player:
    def __init__(self, dll):
        self.create = bind(dll, "mpv_create", ct.c_void_p)
        self.initialize = bind(dll, "mpv_initialize", ct.c_int, ct.c_void_p)
        self.option = bind(dll, "mpv_set_option_string", ct.c_int, ct.c_void_p, ct.c_char_p, ct.c_char_p)
        self.command = bind(dll, "mpv_command", ct.c_int, ct.c_void_p, ct.POINTER(ct.c_char_p))
        self.wait = bind(dll, "mpv_wait_event", ct.POINTER(Event), ct.c_void_p, ct.c_double)
        self.logs = bind(dll, "mpv_request_log_messages", ct.c_int, ct.c_void_p, ct.c_char_p)
        self.destroy = bind(dll, "mpv_terminate_destroy", None, ct.c_void_p)
        self.handle = self.create()
        assert self.handle, "mpv_create failed"
        for key, value in {
            "config": "no", "vo": "null", "ao": "null",
            "audio-display": "no", "terminal": "no", "image-display-duration": "0.1",
            "af": "lavfi=[atempo=1.25]",
        }.items():
            assert self.option(self.handle, key.encode(), value.encode()) >= 0, key
        assert self.initialize(self.handle) >= 0, "mpv_initialize failed"
        assert self.logs(self.handle, b"warn") >= 0

    def play(self, filename):
        arguments = (ct.c_char_p * 3)(b"loadfile", str(filename).encode("utf-8"), None)
        assert self.command(self.handle, arguments) >= 0, "loadfile failed"
        loaded = False
        messages = []
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            event = self.wait(self.handle, 0.1).contents
            if event.id == 2:
                message = ct.cast(event.data, ct.POINTER(LogMessage)).contents
                messages.append(message.text.decode("utf-8", errors="replace"))
            elif event.id == 8:
                loaded = True
            elif event.id == 7:
                end = ct.cast(event.data, ct.POINTER(EndFile)).contents
                assert loaded and end.reason == 0 and end.error == 0, "".join(messages)
                # Decoder/filter failures can still produce an EOF event.
                assert not any("failed" in text.lower() or "error" in text.lower() for text in messages), "".join(messages)
                print(f"PASS: {filename}")
                return
        raise AssertionError("Playback timed out: " + "".join(messages))


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *_):
        pass


def png_chunk(kind, data):
    payload = kind + data
    return struct.pack("!I", len(data)) + payload + struct.pack("!I", zlib.crc32(payload))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("library", type=Path)
    args = parser.parse_args()
    dll = ct.CDLL(str(args.library.resolve()))
    for name in (
        "create", "create_ex", "destroy", "get_native_surface", "update_size",
        "update_window_state", "notify_display_change", "lock_surface", "unlock_surface",
        "get_property", "set_property", "observe_property", "unobserve_property", "command",
    ):
        getattr(dll, "mpv_vksf_" + name)
    print("PASS: public libmpv and VKSF exports")
    with tempfile.TemporaryDirectory(prefix="mpv-msvc-") as directory:
        root = Path(directory)
        audio = root / "audio-é.wav"
        with wave.open(str(audio), "wb") as wav:
            wav.setparams((1, 2, 8000, 0, "NONE", "not compressed"))
            wav.writeframes(b"\x00\x00" * 4000)
        image = root / "image.png"
        image.write_bytes(
            b"\x89PNG\r\n\x1a\n"
            + png_chunk(b"IHDR", struct.pack("!2I5B", 2, 2, 8, 2, 0, 0, 0))
            + png_chunk(b"IDAT", zlib.compress((b"\x00" + b"\xff\x00\x00" * 2) * 2))
            + png_chunk(b"IEND", b"")
        )
        player = Player(dll)
        try:
            player.play(audio)
            player.play(image)
            for codec in ("h264", "hevc", "vp9", "av1"):
                player.play(Path(__file__).parent / "fixtures" / f"{codec}.mkv")
            handler = functools.partial(QuietHandler, directory=str(root))
            with http.server.ThreadingHTTPServer(("127.0.0.1", 0), handler) as server:
                thread = threading.Thread(target=server.serve_forever, daemon=True)
                thread.start()
                try:
                    player.play(f"http://127.0.0.1:{server.server_port}/audio-%C3%A9.wav")
                finally:
                    server.shutdown()
                    thread.join()
        finally:
            player.destroy(player.handle)


if __name__ == "__main__":
    main()
