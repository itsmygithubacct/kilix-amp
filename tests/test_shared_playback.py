#!/usr/bin/env python3
"""Exercise the actual service, kilix-mpv, TUI client and SDL controller.

Uses generated audio, private sockets/configs and mpv's silent null output.
GUI checks use a private Xvfb display; they never touch the user's desktop.
"""
import importlib.util
import json
import math
import os
from pathlib import Path
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
import wave

ROOT = Path(__file__).resolve().parents[1]
AMP = Path(os.environ.get("KILIX_AMP_BINARY", ROOT / "kilix-amp"))
MUSIC = Path(os.environ.get("KILIX_MUSIC_CHECKOUT", ROOT.parents[1] / "kilix-desktops/kilix-tui-utils"))


def wait_for(predicate, timeout=5):
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        try:
            last = predicate()
            if last:
                return last
        except (OSError, ValueError):
            pass
        time.sleep(.025)
    raise AssertionError(f"condition did not become true (last result: {last!r})")


def xdotool(env, *args):
    return subprocess.check_output(["xdotool", *map(str, args)], env=env, text=True, timeout=5).strip()


def window_geometry(env, window):
    fields = dict(line.split("=") for line in xdotool(env, "getwindowgeometry", "--shell", window).splitlines())
    return tuple(int(fields[key]) for key in ("X", "Y", "WIDTH", "HEIGHT"))


class SharedPlayback(unittest.TestCase):
    def setUp(self):
        if not shutil.which(os.environ.get("KILIX_MPV", "kilix-mpv")):
            self.skipTest("kilix-mpv is required")
        self.tmp = tempfile.TemporaryDirectory(prefix="kilix-playback-test-")
        self.path = Path(self.tmp.name)
        self.socket = self.path / "amp.sock"
        self.env = dict(os.environ, KILIX_AMP_SOCKET=str(self.socket),
                        KILIX_AMP_AUDIO_OUTPUT="null", KILIX_AMP=str(AMP),
                        XDG_CONFIG_HOME=str(self.path / "config"),
                        XDG_DATA_HOME=str(self.path / "data"))
        self.env.pop("KILIXAMP_EXIT_AFTER_MS", None)
        self.processes = []
        self.daemon_pid = None
        self.log = open(self.path / "processes.log", "w+")
        self.tone = self.path / "01 tone ü.wav"
        self.other = self.path / "02 other.wav"
        for path, frequency in ((self.tone, 947), (self.other, 440)):
            with wave.open(str(path), "wb") as audio:
                audio.setparams((2, 2, 44100, 0, "NONE", ""))
                block = b"".join(struct.pack("<hh", *([int(12000 * math.sin(i * math.tau * frequency / 44100))] * 2))
                                 for i in range(44100))
                audio.writeframes(block * 8)

    def tearDown(self):
        if self.socket.exists():
            try:
                self.command("quit")
            except OSError:
                pass
        for process in reversed(self.processes):
            if process.poll() is None:
                process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        if self.daemon_pid:
            try:
                os.kill(self.daemon_pid, signal.SIGTERM)
            except ProcessLookupError:
                pass
        self.log.seek(0)
        output = self.log.read()
        if "ERROR" in output or "kilix-amp: MIDI render failed" in output:
            print(output, file=sys.stderr)
        self.log.close()
        self.tmp.cleanup()

    def spawn(self, args, env=None, **kwargs):
        process = subprocess.Popen(args, env=env or self.env,
                                   stdout=self.log, stderr=self.log, **kwargs)
        self.processes.append(process)
        return process

    def command(self, name, **fields):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
            client.settimeout(2)
            client.connect(str(self.socket))
            client.sendall((json.dumps({"cmd": name, "protocol": 2, **fields}) + "\n").encode())
            with client.makefile("r", encoding="utf-8") as stream:
                return json.loads(stream.readline())

    def start(self, *files):
        process = self.spawn([str(AMP), "--headless", "--socket", str(self.socket), *map(str, files)])
        wait_for(lambda: self.command("ping").get("ok"))
        return process

    def playing(self):
        def sample():
            status = self.command("state")
            return status if status["state"] == "playing" and status["pos"] > .15 else None
        return wait_for(sample)

    def tui(self):
        if not (MUSIC / "tools/music/main.py").is_file():
            self.skipTest("kilix-tui-utils checkout is required for cross-client test")
        sys.path.insert(0, str(MUSIC / "src"))
        spec = importlib.util.spec_from_file_location("shared_playback_music", MUSIC / "tools/music/main.py")
        module = importlib.util.module_from_spec(spec)
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        return module.Backend(str(self.socket))

    def display(self, screen="1280x720x24"):
        if not shutil.which("Xvfb") or not shutil.which("xdotool"):
            self.skipTest("Xvfb and xdotool are required for GUI checks")
        reader, writer = os.pipe()
        self.spawn(["Xvfb", "-displayfd", str(writer), "-screen", "0", screen, "-nolisten", "tcp", "-noreset"], pass_fds=(writer,))
        os.close(writer)
        try:
            self.assertTrue(select.select([reader], [], [], 5)[0])
            display = ":" + os.read(reader, 32).decode().strip()
        finally:
            os.close(reader)
        # SDL otherwise loads Mesa for window surfaces on Xvfb. A software
        # framebuffer keeps driver-global allocations out of leak checks.
        return dict(self.env, DISPLAY=display, SDL_VIDEODRIVER="x11",
                    SDL_FRAMEBUFFER_ACCELERATION="0")

    def window(self, process, env):
        def find():
            if process.poll() is not None:
                self.fail(f"Amp exited with {process.returncode}\n{Path(self.log.name).read_text()}")
            result = subprocess.run(["xdotool", "search", "--all", "--onlyvisible", "--pid", str(process.pid), "--name", "^kilix-amp$"],
                                    env=env, capture_output=True, text=True)
            return result.stdout.splitlines()[0] if result.returncode == 0 else None
        return wait_for(find)

    def test_transport_eq_and_real_spectrum(self):
        self.start(self.tone)
        initial = self.playing()
        self.assertEqual(initial["backend"], "kilix-mpv")
        self.assertAlmostEqual(initial["len"], 8, places=2)
        baseline = max(wait_for(lambda: s["spectrum"] if max((s := self.command("state"))["spectrum"]) > .2 else None))
        self.command("eq_enabled", on=True)
        self.command("eq_band", band=5, gain=-12)
        self.assertLess(max(wait_for(lambda: s["spectrum"] if max((s := self.command("state"))["spectrum"]) < baseline - .08 else None)), baseline - .08)
        self.command("volume", level=0)
        wait_for(lambda: max(self.command("state")["spectrum"]) == 0)
        self.command("volume", level=80)
        self.command("balance", level=50)
        self.command("preamp", gain=-3)
        self.command("pause")
        wait_for(lambda: self.command("state")["state"] == "paused")
        time.sleep(.15)
        position = self.command("state")["pos"]
        time.sleep(.2)
        self.assertAlmostEqual(self.command("state")["pos"], position, delta=.02)
        self.command("seek", pos=4)
        wait_for(lambda: abs(self.command("state")["pos"] - 4) < .25)
        self.command("play")
        wait_for(lambda: self.command("state")["pos"] > 4)
        self.command("stop")
        wait_for(lambda: self.command("state")["state"] == "stopped")

    @unittest.skipUnless(os.environ.get("KILIX_TEST_AUDIO_DEVICE") == "1",
                         "set KILIX_TEST_AUDIO_DEVICE=1 to test physical output with silence")
    def test_default_audio_device_selection_plays_silence(self):
        self.env.pop("KILIX_AMP_AUDIO_OUTPUT", None)
        with wave.open(str(self.tone), "wb") as audio:
            audio.setparams((2, 2, 44100, 0, "NONE", ""))
            audio.writeframes(bytes(44100 * 4 * 4))
        self.start(self.tone)
        status = self.playing()
        self.assertEqual(status["last_error"], "")
        self.assertEqual(max(status["spectrum"]), 0)

    def test_queue_snapshots_and_stale_selection(self):
        self.start(self.tone, self.other)
        self.playing()
        client = self.tui()
        self.assertTrue(client.available())
        old = client.command("state")
        self.assertTrue(self.command("reverse")["ok"])
        reply = client.command("play", index=0, queue_revision=old["queue_revision"])
        self.assertFalse(reply["ok"])
        self.assertEqual(reply["file"], str(self.tone))
        snapshot = self.command("snapshot")
        self.assertEqual(snapshot["items"], [str(self.other), str(self.tone)])
        self.assertEqual(snapshot["index"], 1)
        self.assertTrue(client.command("play", index=0, queue_revision=snapshot["queue_revision"])["ok"])
        wait_for(lambda: self.command("state")["file"] == str(self.other))
        self.command("remove", indices=[0, 0])
        self.assertEqual(client.command("playlist")["items"], [str(self.tone)])
        self.assertEqual(self.command("state")["state"], "stopped")

    def test_removing_playing_track_discards_its_source_and_duration(self):
        self.start(self.tone, self.other)
        self.playing()
        wait_for(lambda: self.command("snapshot")["tracks"][0]["duration"] == 8)
        stopped = self.command("remove", indices=[0])
        self.assertTrue(stopped["ok"])
        self.assertEqual(stopped["state"], "stopped")
        self.assertEqual(stopped["track_title"], self.other.stem)
        self.assertEqual(stopped["source_type"], "none")
        self.assertEqual(stopped["file"], "")
        self.assertIsNone(stopped["len"])
        self.assertEqual(stopped["sample_rate"], 0)
        self.assertFalse(stopped["seekable"])
        time.sleep(.2)  # Allow position notifications from the idle engine.
        self.assertEqual(self.command("snapshot")["tracks"][0]["duration"], -1)
        self.assertEqual(self.command("state", protocol=1)["file"], "")
        self.command("play")
        self.assertEqual(self.playing()["file"], str(self.other))

    def test_restarted_service_refuses_an_edit_from_the_previous_queue(self):
        first = self.start()
        self.command("add", path=str(self.tone))
        old = self.command("snapshot")
        self.command("quit")
        self.assertEqual(first.wait(timeout=5), 0)
        self.start()
        self.command("add", path=str(self.other))
        new = self.command("snapshot")
        self.assertEqual(old["queue_revision"], new["queue_revision"])
        self.assertNotEqual(old["pid"], new["pid"])
        reply = self.command("remove", indices=[0], queue_pid=old["pid"],
                             queue_revision=old["queue_revision"])
        self.assertEqual(reply.get("error_code"), "STALE_QUEUE")
        self.assertEqual(self.command("playlist")["items"], [str(self.other)])

    def test_large_playlist_reaches_both_clients_with_metadata(self):
        listing = self.path / "large.m3u"
        listing.write_text("#EXTM3U\n" + "".join(
            f"#EXTINF:8,Artist — café - Track {i}\n{self.tone}\n" for i in range(5000)))
        self.start()
        self.assertTrue(self.command("add", path=str(listing))["ok"])
        self.assertEqual(len(self.tui().command("playlist")["items"]), 5000)
        snapshot = self.command("snapshot")
        self.assertEqual(snapshot["tracks"][-1]["title"], "Track 4999")
        env = dict(self.display(), KILIXAMP_EXIT_AFTER_MS="900")
        gui = self.spawn([str(AMP)], env)
        window = self.window(gui, env)
        time.sleep(.15)
        subprocess.run(["xdotool", "key", "--window", window, "Down"], env=env, check=True, capture_output=True)
        wait_for(lambda: self.command("state")["volume"] == 75)
        self.assertEqual(gui.wait(timeout=6), 0)

    def test_wrong_protocol_cannot_mutate_and_regular_file_is_preserved(self):
        marker = self.path / "not-a-socket"
        marker.write_text("keep this file")
        denied = self.spawn([str(AMP), "--headless", "--socket", str(marker)])
        self.assertNotEqual(denied.wait(timeout=3), 0)
        self.assertEqual(marker.read_text(), "keep this file")
        self.start(self.tone)
        self.playing()
        self.assertFalse(self.command("clear", protocol=99)["ok"])
        self.assertFalse(self.command("clear", queue_revision="wrong type")["ok"])
        self.assertEqual(self.command("state")["count"], 1)

    def test_gui_controls_same_service_and_exit_keeps_playback(self):
        service = self.start(self.tone, self.other)
        self.playing()
        client = self.tui()
        client.command("volume", level=37)
        env = dict(self.display(), KILIXAMP_EXIT_AFTER_MS="2200")
        gui = self.spawn([str(AMP)], env)
        window = self.window(gui, env)
        time.sleep(.15)
        self.assertEqual(client.command("state")["volume"], 37)
        subprocess.run(["xdotool", "key", "--window", window, "b", "Up"], env=env, check=True, capture_output=True)
        wait_for(lambda: client.command("state")["index"] == 1)
        wait_for(lambda: client.command("state")["volume"] == 42)
        self.assertEqual(client.command("state")["pid"], service.pid)
        self.assertEqual(gui.wait(timeout=6), 0)
        status = self.playing()
        self.assertEqual(status["pid"], service.pid)
        self.assertEqual(status["index"], 1)

    def test_player_and_playlist_share_one_native_window(self):
        config = Path(self.env["XDG_CONFIG_HOME"]) / "kilix-amp/kilix-amp.ini"
        config.parent.mkdir(parents=True)
        config.write_text("[ui]\nmain_x=100\nmain_y=80\nplaylist_x=900\nplaylist_y=400\n")
        self.start(self.tone, self.other)
        self.playing()
        self.command("pause")
        env = self.display()

        def xdo(*args):
            return xdotool(env, *args)

        def geometry():
            return window_geometry(env, window)

        def drag(x, y, dx, dy):
            xdo("mousemove", "--window", window, x, y, "mousedown", 1, "sleep", ".08",
                "mousemove_relative", "--", dx, dy, "sleep", ".08", "mouseup", 1)

        gui = self.spawn([str(AMP)], env)
        window = self.window(gui, env)
        self.assertEqual(xdo("search", "--onlyvisible", "--pid", gui.pid).splitlines(), [window])
        self.assertEqual(geometry(), (100, 80, 275, 348))
        xdo("windowfocus", "--sync", window, "key", "Down")
        wait_for(lambda: self.command("state")["volume"] == 75)

        # Grab the lower title bar, then grow the playlist by its own grip.
        drag(72, 124, 50, 35)
        wait_for(lambda: geometry() == (150, 115, 275, 348))
        drag(270, 343, 60, 30)
        wait_for(lambda: geometry() == (150, 115, 335, 378))
        xdo("mousemove", "--window", window, 55, 154, "click", "--repeat", 2, 1)
        wait_for(lambda: self.command("state")["file"] == str(self.other))

        # The playlist close button and the player's PL button reuse the same window.
        xdo("mousemove", "--window", window, 330, 124, "click", 1)
        wait_for(lambda: geometry()[2:] == (275, 116))
        xdo("mousemove", "--window", window, 250, 63, "click", 1)
        wait_for(lambda: geometry()[2:] == (335, 378))
        xdo("windowfocus", window, "key", "alt+e")
        wait_for(lambda: geometry()[2:] == (275, 116))
        xdo("mousemove", "--window", window, 268, 7, "click", 1)
        self.assertEqual(gui.wait(timeout=5), 0)

        # Restart collapsed and restore the saved playlist dimensions at 1.5x.
        gui = self.spawn([str(AMP), "--scale", "1.5"], env)
        window = self.window(gui, env)
        self.assertEqual(geometry(), (150, 115, 413, 174))
        xdo("windowfocus", window, "key", "alt+e")
        wait_for(lambda: geometry() == (150, 115, 503, 567))
        self.assertEqual(xdo("search", "--onlyvisible", "--pid", gui.pid).splitlines(), [window])
        xdo("mousemove", "--window", window, 82, 212, "click", "--repeat", 2, 1)
        wait_for(lambda: self.command("state")["file"] == str(self.tone))

        xdo("key", "alt+e")
        wait_for(lambda: geometry()[2:] == (413, 174))
        xdo("windowmove", window, 820, 520, "key", "alt+e")
        wait_for(lambda: geometry() == (777, 153, 503, 567))

    def test_docked_player_fits_small_display_and_restores_preferred_size(self):
        config = Path(self.env["XDG_CONFIG_HOME"]) / "kilix-amp/kilix-amp.ini"
        config.parent.mkdir(parents=True)
        config.write_text("[ui]\nscale=2\npl_width=475\npl_height=380\nmain_x=800\nmain_y=600\n")
        self.start()
        small = self.display("640x480x24")
        gui = self.spawn([str(AMP)], small)
        window = self.window(gui, small)
        self.assertEqual(window_geometry(small, window), (0, 0, 640, 480))
        xdotool(small, "windowfocus", "--sync", window, "key", "alt+e")
        wait_for(lambda: window_geometry(small, window)[2:] == (550, 232))
        xdotool(small, "key", "alt+e")
        wait_for(lambda: window_geometry(small, window)[2:] == (640, 480))

        # Cycle through the larger zoom presets even when several must fit
        # the same physical screen. The last press returns to the 1x preset.
        for _ in range(4):
            xdotool(small, "key", "d")
        wait_for(lambda: window_geometry(small, window)[2:] == (475, 480))
        xdotool(small, "key", "d", "d")
        wait_for(lambda: window_geometry(small, window)[2:] == (640, 480))
        xdotool(small, "mousemove", "--window", window, 536, 14, "click", 1)
        self.assertEqual(gui.wait(timeout=5), 0)

        large = self.display("1280x1024x24")
        gui = self.spawn([str(AMP)], large)
        window = self.window(gui, large)
        self.assertEqual(window_geometry(large, window)[2:], (950, 992))

    def test_gui_removes_the_selected_duplicate_after_another_client_reorders(self):
        listing = self.path / "duplicates.m3u"
        listing.write_text("#EXTM3U\n" + "".join(
            f"#EXTINF:8,{label}\n{self.tone}\n" for label in ("First", "Second", "Third")))
        self.start()
        self.assertTrue(self.command("add", path=str(listing))["ok"])
        env = self.display()
        gui = self.spawn([str(AMP)], env)
        window = self.window(gui, env)
        xdotool(env, "windowfocus", "--sync", window, "key", "Down")
        wait_for(lambda: self.command("state")["volume"] == 75)
        xdotool(env, "mousemove", "--window", window, 50, 168, "click", 1)
        self.assertTrue(self.command("reverse")["ok"])
        self.assertTrue(self.command("add", path=str(self.other))["ok"])
        xdotool(env, "key", "Up")
        wait_for(lambda: self.command("state")["volume"] == 80)

        # Open the context menu on empty space and remove the selected row.
        xdotool(env, "mousemove", "--window", window, 50, 250, "click", 3)
        menu = xdotool(env, "search", "--sync", "--all", "--onlyvisible",
                       "--pid", gui.pid, "--name", "^menu$")
        xdotool(env, "mousemove", "--window", menu, 38, 61, "click", 1)
        wait_for(lambda: self.command("state")["count"] == 3)
        rows = self.command("snapshot")["tracks"]
        self.assertEqual([row["title"] for row in rows[:2]], ["Second", "First"])
        self.assertEqual(rows[2]["file"], str(self.other))

    def test_gui_opens_first_selected_track_after_another_client_appends(self):
        self.start(self.tone)
        self.playing()
        selection = self.path / "selection"
        os.mkfifo(selection)
        picker = self.path / "zenity"
        picker.write_text('#!/bin/sh\nexec timeout 6 cat "$PICKER_INPUT"\n')
        picker.chmod(0o700)
        env = dict(self.display(), PICKER_INPUT=str(selection),
                   PATH=str(self.path) + os.pathsep + os.environ["PATH"])
        gui = self.spawn([str(AMP)], env)
        window = self.window(gui, env)
        subprocess.run(["xdotool", "key", "--window", window, "l"], env=env,
                       check=True, capture_output=True)
        writer = wait_for(lambda: os.open(selection, os.O_WRONLY | os.O_NONBLOCK))
        with os.fdopen(writer, "w") as answer:
            self.assertTrue(self.command("add", path=str(self.tone))["ok"])
            answer.write(f"{self.other}\n{self.tone}\n")
        wait_for(lambda: self.command("state")["count"] == 4)
        status = self.playing()
        self.assertEqual(status["file"], str(self.other))
        self.assertEqual(status["index"], 2)

    def test_gui_autostarts_service_and_other_gui_attaches(self):
        env = dict(self.display(), KILIXAMP_EXIT_AFTER_MS="1600")
        first = self.spawn([str(AMP), str(self.tone)], env)
        self.window(first, env)
        status = self.playing()
        self.daemon_pid = status["pid"]
        second = self.spawn([str(AMP)], env)
        self.window(second, env)
        self.assertEqual(self.command("state")["pid"], self.daemon_pid)
        self.assertEqual(first.wait(timeout=6), 0)
        self.assertEqual(second.wait(timeout=6), 0)
        self.assertEqual(self.playing()["pid"], self.daemon_pid)

    def test_gui_creates_a_private_fallback_endpoint_on_first_launch(self):
        home = self.path / "home"
        home.mkdir(mode=0o700)
        self.socket = home / ".local/gpu_terminal/kilix/session/kilix-amp.sock"
        self.env["HOME"] = str(home)
        self.env.pop("KILIX_AMP_SOCKET", None)
        self.env.pop("XDG_RUNTIME_DIR", None)
        env = dict(self.display(), KILIXAMP_EXIT_AFTER_MS="900")
        gui = self.spawn([str(AMP)], env)
        self.window(gui, env)
        status = self.command("state")
        self.daemon_pid = status["pid"]
        self.assertEqual(self.socket.parent.stat().st_mode & 0o777, 0o700)
        self.assertEqual(gui.wait(timeout=6), 0)
        self.assertEqual(self.command("state")["pid"], self.daemon_pid)

    def test_stale_socket_concurrent_start_and_restart_after_mpv_crash(self):
        with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as stale:
            stale.bind(str(self.socket))
        denied = self.spawn([str(AMP), "--headless", "--socket", str(self.socket)])
        self.assertNotEqual(denied.wait(timeout=3), 0)
        self.assertTrue(self.socket.exists())
        # Only this test owns the dead endpoint, and explicitly removes it.
        self.socket.unlink()
        processes = [self.spawn([str(AMP), "--headless", "--socket", str(self.socket)]) for _ in range(4)]
        status = wait_for(lambda: self.command("state"))
        wait_for(lambda: sum(p.poll() is None for p in processes) == 1)
        self.assertEqual(sum(p.pid == status["pid"] for p in processes), 1)
        self.command("add", path=str(self.tone))
        self.command("play")
        self.playing()
        children = Path(f"/proc/{status['pid']}/task/{status['pid']}/children").read_text().split()
        mpv = [int(pid) for pid in children if b"kilix-mpv" in Path(f"/proc/{pid}/cmdline").read_bytes()]
        self.assertEqual(len(mpv), 1)
        os.kill(mpv[0], signal.SIGKILL)
        wait_for(lambda: "exited" in self.command("state")["last_error"])
        self.command("play")
        self.assertEqual(self.playing()["file"], str(self.tone))

        # The idle player can also exit after Stop. This must not auto-skip
        # to the next queued track or report a failure for a stopped session.
        self.command("add", path=str(self.other))
        children = Path(f"/proc/{status['pid']}/task/{status['pid']}/children")
        idle_pid, = map(int, children.read_text().split())
        self.command("stop")
        os.kill(idle_pid, signal.SIGTERM)
        wait_for(lambda: str(idle_pid) not in children.read_text().split())
        stopped = self.command("state")
        self.assertEqual(stopped["state"], "stopped")
        self.assertEqual(stopped["index"], 0)
        self.assertEqual(stopped["last_error"], "")
        self.command("play")
        self.assertEqual(self.playing()["file"], str(self.tone))

    def test_midi_and_aac_decode(self):
        midi = self.path / "note.mid"
        events = bytes.fromhex("00 c0 00 00 90 45 64 8f 00 80 45 00 00 ff 2f 00")
        midi.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(events)) + events)
        self.start(midi)
        status = self.playing()
        self.assertEqual(status["file"], str(midi))
        self.assertGreater(status["len"], 1.9)
        wait_for(lambda: max(self.command("state")["spectrum"]) > .1)
        if not shutil.which("ffmpeg"):
            self.skipTest("ffmpeg required to generate AAC fixture")
        aac = self.path / "encoded.m4a"
        subprocess.run(["ffmpeg", "-v", "error", "-i", str(self.tone), "-c:a", "aac", str(aac)], check=True)
        self.command("add", path=str(aac))
        self.command("play", index=1)
        wait_for(lambda: self.command("state")["file"] == str(aac))
        status = self.playing()
        self.assertAlmostEqual(status["len"], 8, delta=.1)

    @unittest.skipUnless(shutil.which("ffmpeg"), "ffmpeg required to generate audio containers")
    def test_mpv_audio_containers(self):
        self.start()
        for suffix, codec, muxer in (("m4b", "aac", "mp4"), ("mka", "flac", "matroska"),
                                     ("weba", "libopus", "webm")):
            with self.subTest(suffix=suffix):
                track = self.path / f"track.{suffix}"
                subprocess.run(["ffmpeg", "-v", "error", "-i", str(self.tone), "-t", "2",
                                "-c:a", codec, "-f", muxer, str(track)], check=True)
                reply = self.command("open", source_type="file", path=str(track))
                self.assertTrue(reply["ok"], reply)
                status = self.playing()
                self.assertEqual(status["file"], str(track))
                self.assertEqual(status["backend"], "kilix-mpv")
                self.assertAlmostEqual(status["len"], 2, delta=.1)

    def test_rapid_replacement_and_stop_ignore_old_mpv_events(self):
        self.start(self.tone, self.other)
        self.playing()
        for index in [1, 0, 1, 0, 1]:
            self.command("play", index=index)
        self.command("stop")
        time.sleep(.3)
        status = self.command("state")
        self.assertEqual(status["state"], "stopped")
        self.assertEqual(status["pos"], 0)
        self.command("play", index=0)
        self.assertEqual(self.playing()["file"], str(self.tone))
        self.command("sort", by="path")
        self.assertEqual(self.command("state")["index"], 0)

    def test_eof_repeat_and_unplayable_queue_terminate_correctly(self):
        short = self.path / "short.wav"
        with wave.open(str(self.tone), "rb") as source, wave.open(str(short), "wb") as out:
            out.setparams(source.getparams())
            out.writeframes(source.readframes(22050))
        self.start(short, self.other)
        wait_for(lambda: self.command("state")["index"] == 1)
        self.assertEqual(self.playing()["file"], str(self.other))
        self.command("repeat", mode=2)
        self.command("play", index=0)
        time.sleep(1.4)
        status = self.command("state")
        self.assertEqual(status["index"], 0)
        self.assertIn(status["state"], ("playing", "loading"))
        self.command("clear")
        bad = self.path / "bad.mp3"
        bad.write_text("this is not audio")
        self.command("add", path=str(bad))
        self.command("repeat", mode=1)
        self.command("play")
        wait_for(lambda: self.command("state")["last_error"])
        time.sleep(.4)
        self.assertEqual(self.command("state")["state"], "stopped")

    def test_tui_handoff_and_second_controller_leave_the_service_running(self):
        with patch.dict(os.environ, self.env):
            client = self.tui()
            self.assertTrue(client.start(), client.error)
            child = client._owned
            self.processes.append(child)
            self.assertTrue(client.capabilities["shared_playback"])
            self.assertTrue(client.command("add", path=str(self.tone))["ok"])
            self.assertTrue(client.command("play")["ok"])
            self.playing()
            client.close()
            self.assertIsNone(child.poll())
            attached = self.tui()
            self.assertTrue(attached.start(), attached.error)
            self.assertEqual(attached.command("state")["pid"], child.pid)
            attached.close()
            self.assertEqual(self.playing()["pid"], child.pid)

    @unittest.skipUnless(os.environ.get("KILIX_TEST_ENCODEC_FILE"),
                         "set KILIX_TEST_ENCODEC_FILE to an admitted local fixture")
    def test_switch_between_mpv_and_native_encodec_preserves_session(self):
        self.env["SDL_AUDIODRIVER"] = "dummy"
        service = self.start(self.tone)
        self.playing()
        self.command("volume", level=31)
        path = str(Path(os.environ["KILIX_TEST_ENCODEC_FILE"]).resolve())
        self.assertTrue(self.command("open", source_type="file", path=path)["ok"])
        native = self.playing()
        self.assertEqual(native["backend"], "native-encodec")
        self.assertEqual(native["pid"], service.pid)
        self.assertEqual(native["volume"], 31)
        self.assertTrue(native["model_ready"])
        self.command("pause")
        wait_for(lambda: self.command("state")["state"] == "paused")
        children = Path(f"/proc/{service.pid}/task/{service.pid}/children").read_text().split()
        self.assertFalse(any(b"kilix-mpv" in Path(f"/proc/{pid}/cmdline").read_bytes() for pid in children))
        self.assertTrue(self.command("open", source_type="file", path=str(self.tone))["ok"])
        general = self.playing()
        self.assertEqual(general["backend"], "kilix-mpv")
        self.assertEqual(general["volume"], 31)
        self.assertFalse(general["live"])
        self.assertEqual(general["pid"], service.pid)

    def test_cancel_midi_does_not_block_the_service(self):
        midi = self.path / "long.mid"
        # A valid ten-minute note. Rendering must be cancellable before it ends.
        duration = 480 * 1200
        encoded = [duration & 127]
        while (duration := duration >> 7):
            encoded.insert(0, 128 | (duration & 127))
        events = bytes.fromhex("00 90 45 64") + bytes(encoded) + bytes.fromhex("80 45 00 00 ff 2f 00")
        midi.write_bytes(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480) + b"MTrk" + struct.pack(">I", len(events)) + events)
        self.start(midi)
        start = time.monotonic()
        self.command("stop")
        self.assertLess(time.monotonic() - start, .5)
        self.assertEqual(self.command("state")["state"], "stopped")
        self.command("add", path=str(self.tone))
        self.command("play", index=1)
        self.assertEqual(self.playing()["file"], str(self.tone))


if __name__ == "__main__":
    unittest.main(verbosity=2)
