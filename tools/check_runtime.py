#!/usr/bin/env python3
"""Check the actual mpv filters and packaged meter without opening a device."""
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import wave


def main():
    library = Path(__file__).resolve().parents[1] / "libkilix-meter.so"
    executable = os.environ.get("KILIX_MPV") or "kilix-mpv"
    try:
        with library.open("rb") as plugin, tempfile.TemporaryFile(suffix=".wav") as source:
            with wave.open(source, "wb") as audio:
                audio.setparams((2, 2, 44100, 0, "NONE", ""))
                audio.writeframes(bytes(44100 * 4 // 4))
            source.seek(0)
            reader, writer = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM | socket.SOCK_NONBLOCK)
            with reader, writer:
                filters = ("lavfi=[aformat=channel_layouts=stereo,equalizer=f=947:t=q:w=1:g=0,"
                           "volume=1,pan=stereo|c0=c0|c1=c1,"
                           f"ladspa=file=/proc/self/fd/{plugin.fileno()}:plugin=kilix_meter]")
                result = subprocess.run(
                    [executable, "--no-config", "--load-scripts=no",
                     "--no-video", "--ao=null", "--length=0.15", f"--af={filters}",
                     f"/proc/self/fd/{source.fileno()}"],
                    env=dict(os.environ, KILIX_AMP_SPECTRUM_FD=str(writer.fileno())),
                    pass_fds=(plugin.fileno(), writer.fileno(), source.fileno()),
                    capture_output=True, timeout=10)
                if result.returncode:
                    raise RuntimeError((result.stdout + result.stderr).decode(errors="replace")[-2500:]
                                       or "kilix-mpv rejected the audio filter chain")
                if len(reader.recv(4097)) != 4096:
                    raise RuntimeError("kilix-mpv did not produce a complete spectrum frame")
    except (OSError, RuntimeError, subprocess.TimeoutExpired) as error:
        print(f"kilix-amp runtime check failed: {error}", file=sys.stderr)
        return 1
    print("kilix-amp: kilix-mpv decoding, EQ, pan and packaged spectrum tap verified")
    return 0


if __name__ == "__main__":
    sys.exit(main())
