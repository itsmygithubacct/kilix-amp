"""Exercise the actual headless player through its private control socket."""
import argparse
import json
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('binary', type=Path)
    parser.add_argument('files', type=Path, nargs='+')
    parser.add_argument('--log', type=Path, required=True)
    args = parser.parse_args()
    checks = 0
    with tempfile.TemporaryDirectory(prefix='amp-headless-') as name, args.log.open('wb') as log:
        root = Path(name)
        endpoint = root / 'amp.sock'
        env = dict(os.environ, SDL_AUDIODRIVER='dummy', XDG_CONFIG_HOME=str(root / 'config'),
                   XDG_RUNTIME_DIR=str(root))
        env.pop('KILIXAMP_EXIT_AFTER_MS', None)
        process = subprocess.Popen([str(args.binary.resolve()), '--headless', '--socket', str(endpoint)],
                                   stdout=log, stderr=log, env=env)
        try:
            deadline = time.monotonic() + 15
            while not endpoint.exists() and time.monotonic() < deadline:
                assert process.poll() is None, 'backend died before listening'
                time.sleep(.01)
            with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as client:
                client.settimeout(5)
                client.connect(str(endpoint))
                stream = client.makefile('rb')

                def commands(*values):
                    nonlocal checks
                    client.sendall(b''.join(json.dumps(dict(protocol=1, **value)).encode() + b'\n'
                                            for value in values))
                    replies = [json.loads(stream.readline()) for _ in values]
                    assert all(reply['protocol'] == 1 and reply['ok'] for reply in replies), replies
                    checks += len(replies)
                    return replies

                def state():
                    return commands({'cmd': 'state'})[0]

                for path in args.files:
                    commands({'cmd': 'clear'}, {'cmd': 'add', 'path': str(path.resolve())})
                    replies = commands({'cmd': 'play', 'index': 0}, {'cmd': 'pause'})
                    assert replies[-1]['state'] == 'paused', replies
                    time.sleep(.05)
                    assert state()['state'] == 'paused'
                    checks += 2
                    commands({'cmd': 'play'})
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        status = state()
                        if status['state'] == 'playing':
                            break
                        assert status['state'] in {'loading', 'buffering'}, status
                        time.sleep(.01)
                    else:
                        raise AssertionError('never reached actual playing state')
                    assert status['len'] > 2, status
                    assert status['file'] == str(path.resolve())
                    assert commands({'cmd': 'pause'})[0]['state'] == 'paused'
                    commands({'cmd': 'seek', 'pos': 1.02})
                    deadline = time.monotonic() + 30
                    while time.monotonic() < deadline:
                        status = state()
                        assert status['state'] == 'paused', status
                        if .98 <= status['pos'] <= 1.0:
                            break
                        time.sleep(.01)
                    else:
                        raise AssertionError(f'seek never acknowledged: {status}')
                    prior = status['pos']
                    client.sendall(b'{"protocol":2,"cmd":"seek","pos":-1}\n')
                    refused = json.loads(stream.readline())
                    assert refused['protocol'] == 2 and refused['ok'] is False
                    assert refused['error_code'] == 'INVALID_REQUEST' and refused['error_recoverable'] is True
                    assert state()['pos'] == prior
                    checks += 3
                    assert commands({'cmd': 'toggle'})[0]['state'] in {'buffering', 'playing'}
                    assert commands({'cmd': 'stop'})[0]['state'] == 'stopped'
                    commands({'cmd': 'play'}, {'cmd': 'stop'})
                    time.sleep(.05)
                    assert state()['state'] == 'stopped'
                    checks += 7
                commands({'cmd': 'quit'})
                stream.close()
            assert process.wait(timeout=5) == 0
            assert not endpoint.exists()
            checks += 2
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    print(f'headless EnCodec: {checks} successful reply/assertion checks; two playback sources; functional scope only')


if __name__ == '__main__':
    main()
