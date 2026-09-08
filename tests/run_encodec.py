"""Real native worker/audio checks on deterministic, locally generated audio."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--test-binary", type=Path, required=True)
    parser.add_argument("--codec-command", type=Path, required=True)
    parser.add_argument("--mono-assets", type=Path, required=True)
    parser.add_argument("--stereo-assets", type=Path, required=True)
    parser.add_argument("--evidence-dir", type=Path, required=True)
    selection = parser.add_mutually_exclusive_group(required=True)
    selection.add_argument("--content-root", type=Path)
    selection.add_argument("--development-only", action="store_true")
    parser.add_argument("--profile", choices=("24k", "48k", "all"), default="all")
    args = parser.parse_args()
    root = args.evidence_dir.resolve()
    root.mkdir(parents=True, exist_ok=False)
    rows = []
    for profile, channels, rate, samples, bitrates, assets in (
        ("24k", 1, 24000, 50003, (3, 6, 12), args.mono_assets),
        ("48k", 2, 48000, 100003, (3, 6, 12, 24), args.stereo_assets),
    ):
        if args.profile != "all" and profile != args.profile:
            continue
        pcm = b"".join(struct.pack("<h", (i * 113 % 32000) - 16000)
                       for i in range(samples * channels))
        wave = (b"RIFF" + struct.pack("<I", len(pcm) + 36) + b"WAVEfmt "
                + struct.pack("<IHHIIHH", 16, 1, channels, rate,
                              rate * channels * 2, channels * 2, 16)
                + b"data" + struct.pack("<I", len(pcm)) + pcm)
        wav_path = root / f"{profile}.wav"
        wav_path.write_bytes(wave)
        for bitrate in bitrates:
            label = f"{profile}-{bitrate}"
            encoded = root / f"{label}.kenc"
            commands = (
                [str(args.codec_command.resolve()), "encode", "--model-dir", str(assets.resolve()),
                 "--profile", profile, "--bitrate", str(bitrate), str(wav_path), str(encoded)],
                [str(args.test_binary.resolve()), str(encoded),
                 str(args.mono_assets.resolve()), str(args.stereo_assets.resolve()),
                 str(args.content_root.resolve()) if args.content_root else "--development-only"],
            )
            for stage, command in zip(("encode", "consumer"), commands):
                with (root / f"{label}-{stage}.stdout").open("wb") as out, \
                     (root / f"{label}-{stage}.stderr").open("wb") as err:
                    result = subprocess.run(command, stdout=out, stderr=err, timeout=120)
                if result.returncode:
                    raise SystemExit(f"{label} {stage} failed: {result.returncode}; retained at {root}")
            rows.append({"profile": profile, "bitrate_kbps": bitrate, "samples": samples,
                         "input_sha256": hashlib.sha256(wave).hexdigest(),
                         "container_sha256": hashlib.sha256(encoded.read_bytes()).hexdigest(),
                         "consumer_exit": 0})
            print(f"{label}: {'installed worker/shared audio' if args.content_root else 'development byte/path worker'} PASS", flush=True)
    report = {"scope": "functional installed native consumer" if args.content_root else "development byte/path worker only",
              "release_qualified": False, "threads": 2, "rows": rows}
    (root / "results.json").write_text(json.dumps(report, indent=2, sort_keys=True) + "\n")


if __name__ == "__main__":
    main()
