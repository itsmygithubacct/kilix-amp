"""Actual protocol-2 backend: live stdin, Unix reconnect and refusal controls."""
import argparse
import json
import os
from pathlib import Path
import socket
import struct
import subprocess
import tempfile
import threading
import time


def framed(path):
    data = path.read_bytes()
    assert data[:8] == b'KENC\x01\r\n\x1a' and data[8] == 1
    header = bytearray(data[:64])
    header[11] = 1
    header[24:40] = bytes(16)
    header[44:48] = bytes(4)
    header[48:56] = struct.pack('<Q', 64)
    offset = struct.unpack_from('<Q', data, 48)[0]
    packets = []
    for _ in range(50):
        length = struct.unpack_from('<I', data, offset)[0]
        assert 0 < length <= 160
        packets.append(data[offset:offset + 4 + length])
        offset += 4 + length
    return bytes(header), packets


def run(binary, path, evidence, unix=False):
    checks = 0
    header, packets = framed(path)
    with tempfile.TemporaryDirectory(prefix='ahl-') as temporary, evidence.open('wb') as log:
        root = Path(temporary)
        control = root / 'control.sock'
        source = root / 'source.sock'
        stopping = threading.Event()
        accepted = []
        producer_errors = []
        server = None
        thread = None
        if unix:
            server = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            server.bind(str(source))
            source.chmod(0o600)
            server.listen(1)
            server.settimeout(.1)

            def produce():
                try:
                    while not stopping.is_set():
                        try:
                            channel, _ = server.accept()
                        except socket.timeout:
                            continue
                        with channel:
                            channel.settimeout(5)
                            accepted.append(time.monotonic())
                            if len(accepted) == 1:
                                channel.sendall(header + b''.join(packets[:2]))
                            else:
                                channel.sendall(header + b''.join(packets) + bytes(4))
                except (BrokenPipeError, ConnectionResetError):
                    pass
                except Exception as error:
                    producer_errors.append(repr(error))

            thread = threading.Thread(target=produce)
            thread.start()
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(root / 'config'),
                   XDG_RUNTIME_DIR=str(root))
        env.pop('KILIXAMP_EXIT_AFTER_MS', None)
        env.pop('KILIX_ENCODEC_THREADS', None)
        argv = [str(binary.resolve()), '--headless', '--socket', str(control)]
        if not unix:
            argv.append('--encodec-stdin')
        process = subprocess.Popen(argv, stdin=subprocess.PIPE, stdout=log, stderr=log, env=env)
        try:
            if not unix:
                process.stdin.write(header + b''.join(packets) + bytes(4))
            process.stdin.close()
            deadline = time.monotonic() + 15
            while not control.exists() and time.monotonic() < deadline:
                assert process.poll() is None
                time.sleep(.01)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as channel:
                channel.settimeout(5)
                channel.connect(str(control))
                stream = channel.makefile('rb')

                def request(value, ok=True):
                    nonlocal checks
                    channel.sendall(json.dumps(value).encode() + b'\n')
                    reply = json.loads(stream.readline())
                    assert reply['ok'] is ok, reply
                    if ok:
                        assert reply['protocol'] == value.get('protocol', 1), reply
                    checks += 1
                    return reply

                def command(name, ok=True, **fields):
                    return request(dict(protocol=2, cmd=name, **fields), ok)

                def state():
                    return command('state')

                assert command('ping')['live_sources'] is True
                checks += 1
                for protocol in [0, 3, True, '2', 2.0]:
                    request(dict(protocol=protocol, cmd='quit'), False)
                    assert process.poll() is None
                    checks += 1
                if unix:
                    queued = command('open', source_type='encodec-unix', path=str(source))
                    assert queued['state'] == 'loading' and not queued['ready'] and queued['len'] is None
                    checks += 1
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        status = state()
                        if status['source_error_code']:
                            break
                        time.sleep(.01)
                    assert status['source_error_code'] == 8 and status['reconnect_required'], status
                    assert status['source_error_message'] and not status['ended']
                    assert status['len'] is None and status['live']
                    time.sleep(.2)
                    assert len(accepted) == 1, accepted
                    assert state()['source_error_code'] == 8
                    checks += 5
                    command('open', source_type='encodec-unix', path=str(source))
                deadline = time.monotonic() + 30
                played = False
                while time.monotonic() < deadline:
                    status = state()
                    assert status['live'] and status['len'] is None and not status['seekable'], status
                    assert not status['source_error_code'], status
                    checks += 2
                    if status['state'] == 'playing':
                        played = True
                        break
                    time.sleep(.01)
                assert played, status
                assert status['sample_rate'] == 24000 and status['channels'] == 1 and status['profile'] == 1
                assert status['threads'] == 2 and status['model_ready']
                assert status['source_type'] == ('encodec-unix' if unix else 'encodec-stdin')
                checks += 4
                assert command('seek', False, pos=1)['error_code'] == 'NOT_SEEKABLE'
                command('open', False, source_type='encodec-unix', path='/' + 'x' * 4096)
                command('open', False, source_type='encodec-stdin', path='stdin')
                command('play', False, index=1.5)
                command('shuffle', False, on=1)
                request(dict(protocol=1, cmd='state'), False)
                command('pause')
                paused = state()['pos']
                time.sleep(.06)
                assert state()['pos'] == paused
                command('play')
                checks += 2
                deadline = time.monotonic() + 10
                while time.monotonic() < deadline:
                    status = state()
                    if status['state'] == 'stopped':
                        break
                    time.sleep(.01)
                assert status['ended'] and not status['source_error_code'], status
                assert status['pos'] == 2 and status['len'] is None, status
                assert status['wire_valid'] and status['wire_pts_ms'] == '1960' and status['wire_epoch'] == '1', status
                checks += 3
                if unix:
                    assert len(accepted) == 2
                    checks += 1
                command('clear')
                assert state()['source_type'] == 'none'
                command('quit')
                checks += 1
                stream.close()
            assert process.wait(timeout=5) == 0
            assert not control.exists()
            checks += 2
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
            stopping.set()
            if thread:
                thread.join(timeout=6)
                assert not thread.is_alive()
            if server:
                server.close()
            assert not producer_errors, producer_errors
    return checks


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('file', type=Path)
    parser.add_argument('--evidence-dir', required=True, type=Path)
    args = parser.parse_args()
    args.evidence_dir.mkdir(mode=0o700, parents=True, exist_ok=False)
    rows = {}
    for unix in [False, True]:
        name = 'unix' if unix else 'stdin'
        rows[name] = run(args.binary, args.file, args.evidence_dir / f'{name}.log', unix)
        print(name, rows[name], 'successful reply/assertion checks', flush=True)
    (args.evidence_dir / 'results.json').write_text(json.dumps(dict(
        scope='functional live control only', release_qualified=False, checks=rows), indent=2) + '\n')


if __name__ == '__main__':
    main()
