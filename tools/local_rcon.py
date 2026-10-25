"""Commands for the sole local test server, through its private controller."""
import json
import subprocess
import sys

from local_test_environment import CONTROLLER, TEST_ROOT

# Older read-only callers locate the game tree via ROOT.
ROOT = TEST_ROOT


def command(text):
    if not isinstance(text, str) or not text.strip() or any(char in text for char in ';\r\n\x00'):
        raise ValueError('One nonempty server command is required')
    if not CONTROLLER.is_file():
        raise RuntimeError(f'The fixed local test controller is missing: {CONTROLLER}')
    result = subprocess.run(
        [sys.executable, '-B', '-X', 'utf8', str(CONTROLLER), '--command', text],
        stdin=subprocess.DEVNULL, capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=45,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'The fixed local test command failed')
    response = json.loads(result.stdout)
    if not response.get('ok'):
        raise RuntimeError(response.get('error', 'The fixed local test command failed'))
    return response.get('output', '')


if __name__ == '__main__':
    sys.stdout.reconfigure(encoding='utf-8', errors='replace')
    for requested in sys.argv[1:]:
        print(command(requested))
