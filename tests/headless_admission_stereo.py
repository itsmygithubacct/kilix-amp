"""Actual player admission checks using a reviewed stereo-only catalog.

The caller supplies existing installed authority and receipts. Only private
copies are changed by refusal probes. No download or receipt decision occurs.
"""
import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time

from headless_live import framed


def population(root):
    result = {}
    for path in root.rglob("*"):
        if path.is_file():
            h = hashlib.sha256()
            with path.open("rb") as stream:
                for block in iter(lambda: stream.read(1024 * 1024), b""):
                    h.update(block)
            result[str(path.relative_to(root))] = h.hexdigest()
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "content-root", "receipt-state", "stereo-file", "unadmitted-mono",
                 "legacy-mono-assets", "legacy-stereo-assets", "evidence-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    out = args.evidence_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    original = {"content": population(args.content_root), "state": population(args.receipt_state)}
    rows = []
    with tempfile.TemporaryDirectory(prefix="amp-admission-") as temporary:
        root = Path(temporary)
        content, state = root / "content", root / "state"
        shutil.copytree(args.content_root, content)
        shutil.copytree(args.receipt_state, state)
        receipt_dir = state / "kilix-content/license-receipts/v1"
        receipts = list(receipt_dir.glob("*.json"))
        assert len(receipts) == 1, "this fixture requires one stereo-only receipt"
        saved_receipt = receipts[0].read_bytes()
        installed = content / "encodec-48khz-frame/op17-v1-844d8fcf"
        notice = installed / "notices/LICENSE-MIT-META.txt"
        graph = installed / "decoder_frame_op17.onnx"
        assert notice.is_file() and graph.is_file()
        environment = dict(os.environ, SDL_AUDIODRIVER="dummy", KILIX_CONTENT_ROOT=str(content),
            XDG_STATE_HOME=str(state), XDG_CONFIG_HOME=str(root / "config"), XDG_RUNTIME_DIR=str(root),
            KILIX_ENCODEC_24KHZ_DIR=str(args.legacy_mono_assets.resolve()),
            KILIX_ENCODEC_48KHZ_DIR=str(args.legacy_stereo_assets.resolve()))
        environment.pop("KILIX_ENCODEC_THREADS", None)
        environment.pop("KILIXAMP_EXIT_AFTER_MS", None)

        @contextmanager
        def player(label, live=False):
            endpoint = root / (label + ".sock")
            with (out / (label + ".log")).open("wb") as log:
                command = [str(args.binary.resolve()), "--headless", "--socket", str(endpoint)]
                if live:
                    command.append("--encodec-stdin")
                process = subprocess.Popen(command, env=environment, stdin=subprocess.PIPE if live else subprocess.DEVNULL,
                                           stdout=log, stderr=log)
                try:
                    deadline = time.monotonic() + 15
                    while not endpoint.exists():
                        assert process.poll() is None and time.monotonic() < deadline
                        time.sleep(.01)
                    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                        client.settimeout(5)
                        client.connect(str(endpoint))
                        with client.makefile("rb") as stream:
                            def request(command, **fields):
                                client.sendall(json.dumps(dict(protocol=2, cmd=command, **fields)).encode() + b"\n")
                                reply = json.loads(stream.readline(65536))
                                assert reply["protocol"] == 2 and reply["ok"] is True, reply
                                return reply
                            if live:
                                header, _ = framed(args.unadmitted_mono)
                                process.stdin.write(header)
                                process.stdin.flush()
                            yield request
                            request("quit")
                    assert process.wait(timeout=10) == 0
                    assert not endpoint.exists()
                finally:
                    if process.stdin:
                        process.stdin.close()
                    if process.poll() is None:
                        process.terminate()
                        try:
                            process.wait(timeout=5)
                        except subprocess.TimeoutExpired:
                            process.kill()
                            process.wait(timeout=5)

        def await_state(request, label, accepted):
            start = time.monotonic()
            while time.monotonic() - start < 30:
                value = request("state")
                if value["source_error_code"] or value["state"] == "playing":
                    break
                time.sleep(.01)
            else:
                raise AssertionError((label, "no terminal readiness", value))
            if accepted:
                assert value["state"] == "playing" and value["model_ready"] and value["profile"] == 2, value
                assert value["threads"] == 2 and value["source_error_code"] == 0, value
                request("pause")
            else:
                assert value["source_error_code"] == 2 and not value["model_ready"] and not value["ready"], value
            rows.append(dict(case=label, accepted=accepted, state=value, elapsed_seconds=time.monotonic()-start))
            (out / "results.json").write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")

        with player("file") as request:
            def load(label, accepted=True, path=None):
                request("clear")
                request("add", path=str((path or args.stereo_file).resolve()))
                request("play", index=0)
                await_state(request, label, accepted)

            load("actual-installed-stereo")
            # A later open must recheck durable authority within the same player.
            receipts[0].unlink()
            load("receipt-revoked", False)
            receipts[0].write_bytes(saved_receipt)
            receipts[0].chmod(0o600)
            load("receipt-restored")
            for label, path in (("notice", notice), ("graph", graph)):
                with path.open("r+b") as stream:
                    original_byte = stream.read(1)
                    stream.seek(0)
                    stream.write(bytes([original_byte[0] ^ 1]))
                load(label + "-mutated", False)
                with path.open("r+b") as stream:
                    stream.write(original_byte)
                load(label + "-restored")
            extra = installed / "unlisted.txt"
            extra.write_bytes(b"not a catalog member")
            load("extra-member", False)
            extra.unlink()
            load("full-population-restored")
            load("unadmitted-mono-file-with-valid-legacy-graphs", False, args.unadmitted_mono)
        with player("live", live=True) as request:
            await_state(request, "unadmitted-mono-live-with-valid-legacy-graphs", False)
    assert original == {"content": population(args.content_root), "state": population(args.receipt_state)}, "original authority changed"
    (out / "binding.json").write_text(json.dumps({"original_inputs": original, "cases": len(rows),
        "passed": True, "scope": "actual player admission against stereo-only catalog; no qualification"}, indent=2, sort_keys=True) + "\n")
    print(f"headless installed admission: {len(rows)} actual cases PASS; original inputs unchanged")


if __name__ == "__main__":
    main()
