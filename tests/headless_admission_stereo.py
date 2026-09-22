"""Actual player admission checks using a reviewed stereo-only receipt store.

asset/v3 layout: the caller supplies an existing installed content root, whose
48 kHz tree sits at kilix-content's `Installer(root).asset_destination(spec)`,
and an existing kilix-license receipt store (the directory
`kilix_license.receipt_store_root()` names) holding exactly one receipt, for
the 48 kHz asset only. `--content-source` is the kilix-content checkout at the
exact commit the libkilix-encodec that Amp links was built from; the asset
identity, its install location and the receipt name all come from that
source's verified packaged catalogue, never from a spelled path.

Only private copies are changed by refusal probes. No download or receipt
decision occurs; the wrong-receipt probes re-serialise the supplied receipt
with one bound field changed, in the private copy.

`--probe` (build-encodec/admission_probe.so) records what the installed
admission interface returned for every case. `--no-model-runtime` (requires
`--probe`) is for a library built without ONNX: an admitted case then must
show admission returning the exact sealed stereo population and the native
loader refusing afterwards, while every refused case must show admission
itself refusing.
"""
import argparse
from contextlib import contextmanager
import dataclasses
import hashlib
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import subprocess
import sys
import tempfile
import time

from headless_live import framed

STEREO_ASSET = "encodec-48khz-frame"
MONO_ASSET = "encodec-24khz-stateful"
KENC_OK, KENC_ERR_MODEL = 0, 2


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


def authority(source):
    """The v3 content and licence authorities from the supplied source."""
    sys.path.insert(0, str((source / "src").resolve()))
    import kilix_content
    import kilix_license
    import kilix_license.receipts  # noqa: F401 (parse_receipt_bytes)
    return kilix_content, kilix_license


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("binary", "content-source", "content-root", "receipt-store", "stereo-file", "unadmitted-mono",
                 "legacy-mono-assets", "legacy-stereo-assets", "evidence-dir"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--probe", type=Path)
    parser.add_argument("--no-model-runtime", action="store_true")
    args = parser.parse_args()
    if args.no_model_runtime and args.probe is None:
        parser.error("--no-model-runtime needs --probe: without it admitted and refused cases look the same")
    content_api, licence_api = authority(args.content_source)
    catalog = content_api.verified_packaged_catalog()
    stereo, mono = catalog.require_asset(STEREO_ASSET), catalog.require_asset(MONO_ASSET)
    assert len(stereo.licenses) == 1 and len(mono.licenses) == 1, "asset/v3 binds exactly one licence row"
    out = args.evidence_dir.resolve()
    out.mkdir(parents=True, exist_ok=False)
    original = {"content": population(args.content_root), "receipts": population(args.receipt_store)}
    rows = []
    with tempfile.TemporaryDirectory(prefix="amp-admission-") as temporary:
        root = Path(temporary)
        content, receipts_root = root / "content", root / "receipts"
        shutil.copytree(args.content_root, content)
        shutil.copytree(args.receipt_store, receipts_root)
        assert stat.S_IMODE(receipts_root.stat().st_mode) & 0o077 == 0, "receipt store copy is not private"
        receipts = sorted(p for p in receipts_root.glob("*.json") if not p.name.startswith("."))
        assert len(receipts) == 1, "this fixture requires one stereo-only receipt"
        store = licence_api.ReceiptStore(receipts_root)
        receipt_path = store.path_for(stereo.licenses[0].record_digest, stereo.manifest_digest)
        assert receipts[0] == receipt_path, "the one receipt is not the 48 kHz asset's, for this manifest"
        assert store.lookup(mono.licenses[0].record_digest, mono.manifest_digest) is None
        saved_receipt = receipt_path.read_bytes()
        receipt = licence_api.receipts.parse_receipt_bytes(saved_receipt)
        installer = content_api.Installer(str(content))
        installed = Path(installer.asset_destination(stereo))
        assert installed.parent == content / "assets", installed
        members = {item.path for item in stereo.files}
        notices = sorted(name for name in members if name.startswith("notices/"))
        assert len(notices) == 1, notices
        notice, graph = installed / notices[0], installed / "decoder_frame_op17.onnx"
        assert "decoder_frame_op17.onnx" in members and notice.is_file() and graph.is_file()
        assert Path(installer.asset_destination(mono)).is_dir(), "the unadmitted mono tree must be installed"
        probe_log = root / "probe.jsonl"
        environment = dict(os.environ, SDL_AUDIODRIVER="dummy", KILIX_CONTENT_ROOT=str(content),
            KILIX_LICENSE_RECEIPTS=str(receipts_root), GPU_TERMINAL_HOME=str(root / "stack"),
            HOME=str(root / "home"), XDG_STATE_HOME=str(root / "state"),
            XDG_CONFIG_HOME=str(root / "config"), XDG_RUNTIME_DIR=str(root),
            KILIX_ENCODEC_24KHZ_DIR=str(args.legacy_mono_assets.resolve()),
            KILIX_ENCODEC_48KHZ_DIR=str(args.legacy_stereo_assets.resolve()))
        environment.pop("KILIX_ENCODEC_THREADS", None)
        environment.pop("KILIXAMP_EXIT_AFTER_MS", None)
        if args.probe:
            environment["LD_PRELOAD"] = str(args.probe.resolve())
            environment["KA_ADMISSION_PROBE_LOG"] = str(probe_log)

        def probe_records():
            if not probe_log.exists():
                return []
            return [json.loads(line) for line in probe_log.read_text().splitlines()]

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

        def await_state(request, label, accepted, profile=2, seen=0):
            start = time.monotonic()
            while time.monotonic() - start < 30:
                value = request("state")
                if value["source_error_code"] or value["state"] == "playing":
                    break
                time.sleep(.01)
            else:
                raise AssertionError((label, "no terminal readiness", value))
            calls = probe_records()[seen:]
            admission = [call for call in calls if call["call"] == "kenc_installed_assets_open"]
            if args.probe:
                assert len(admission) == 1 and admission[0]["profile"] == profile, (label, calls)
            if accepted and not args.no_model_runtime:
                assert value["state"] == "playing" and value["model_ready"] and value["profile"] == 2, value
                assert value["threads"] == 2 and value["source_error_code"] == 0, value
                request("pause")
            else:
                # A refusal, or (without a model runtime) an admitted model the native loader cannot run.
                assert value["source_error_code"] == 2 and not value["model_ready"] and not value["ready"], value
            if args.probe and accepted:
                assert admission[0]["result"] == KENC_OK, (label, admission)
                sealed = {item["name"]: item["bytes"] for item in admission[0]["files"]}
                graphs = {item.path: item.bytes for item in stereo.files if not item.path.startswith("notices/")}
                assert sealed == graphs and admission[0]["count"] == len(graphs), (label, sealed, graphs)
                assert all(item["seals"] > 0 for item in admission[0]["files"]), (label, admission)
                if args.no_model_runtime:
                    loads = [call for call in calls if call["call"] == "kenc_file_source_create_fds"]
                    assert len(loads) == 1 and loads[0]["stereo"] == len(graphs), (label, calls)
                    paths = {"kenc_file_source_create", "kenc_stereo_create", "kenc_model_load"}
                    assert not [call for call in calls if call["call"] in paths], (label, calls)
                    assert loads[0]["result"] == KENC_ERR_MODEL, (label, loads)
            elif args.probe:
                assert admission[0]["result"] == KENC_ERR_MODEL and admission[0]["count"] == 0, (label, admission)
                assert not [call for call in calls if call["call"] != "kenc_installed_assets_open"], (label, calls)
            rows.append(dict(case=label, accepted=accepted, state=value, admission=admission,
                             elapsed_seconds=time.monotonic()-start))
            (out / "results.json").write_text(json.dumps(rows, indent=2, sort_keys=True) + "\n")

        with player("file") as request:
            def load(label, accepted=True, path=None, profile=2):
                seen = len(probe_records())
                request("clear")
                request("add", path=str((path or args.stereo_file).resolve()))
                request("play", index=0)
                await_state(request, label, accepted, profile, seen)

            def install_receipt(data):
                receipt_path.write_bytes(data)
                receipt_path.chmod(0o600)

            load("actual-installed-stereo")
            # A later open must recheck durable authority within the same player.
            receipt_path.unlink()
            load("receipt-revoked", False)
            install_receipt(saved_receipt)
            load("receipt-restored")
            # Wrong receipts, filed under the name this binding looks up.
            for label, field in (("receipt-other-manifest", "manifest_digest"),
                                 ("receipt-other-licence-text", "licence_text_digest")):
                other = hashlib.sha256(("amp wrong receipt " + field).encode()).hexdigest()
                install_receipt(dataclasses.replace(receipt, **{field: other}).to_bytes())
                load(label, False)
            install_receipt(saved_receipt)
            load("receipt-restored-after-wrong")
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
            load("unadmitted-mono-file-with-valid-legacy-graphs", False, args.unadmitted_mono, 1)
        with player("live", live=True) as request:
            await_state(request, "unadmitted-mono-live-with-valid-legacy-graphs", False, 1, len(probe_records()))
        if args.probe:
            (out / "probe.jsonl").write_bytes(probe_log.read_bytes())
    assert original == {"content": population(args.content_root), "receipts": population(args.receipt_store)}, \
        "original authority changed"
    (out / "binding.json").write_text(json.dumps({"original_inputs": original, "cases": len(rows),
        "passed": True, "model_runtime": not args.no_model_runtime, "probe": bool(args.probe),
        "content_catalog_manifest": stereo.manifest_digest,
        "scope": "actual player admission against a stereo-only receipt store; no qualification"},
        indent=2, sort_keys=True) + "\n")
    print(f"headless installed admission: {len(rows)} actual cases PASS; original inputs unchanged")


if __name__ == "__main__":
    main()
