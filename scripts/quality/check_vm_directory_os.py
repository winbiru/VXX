"""OS observations that cannot be inferred from a VM's own returned values."""

import os
from pathlib import Path
import stat
import subprocess
import sys


def check_mode(path):
    mask = os.umask(0)
    os.umask(mask)
    actual = stat.S_IMODE(Path(path).stat().st_mode)
    expected = 0o777 & ~mask
    if actual != expected:
        raise AssertionError(f"mkdir mode {actual:o}, expected {expected:o}")


def check_pid(vm, image, workdir):
    env = dict(os.environ, PATH="")
    child = subprocess.Popen([vm, image], cwd=workdir, env=env,
                             stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
    try:
        output, errors = child.communicate(timeout=10)
    except subprocess.TimeoutExpired:
        child.kill()
        child.communicate()
        raise
    expected = [child.pid, os.getpid()]
    actual = [int(line) for line in output.splitlines()]
    if child.returncode or errors or actual != expected:
        raise AssertionError(f"PID/PPID {actual}, expected {expected}: {errors}")


def check_credentials(vm, image, workdir):
    env = dict(os.environ, PATH="")
    child = subprocess.run([vm, image], cwd=workdir, env=env,
                           capture_output=True, text=True, timeout=10)
    actual = [int(line) for line in child.stdout.splitlines()]
    expected = [os.getuid(), os.geteuid(), os.getgid(), os.getegid()]
    if child.returncode or child.stderr or actual != expected:
        raise AssertionError(f"UID/EUID/GID/EGID {actual}, expected {expected}: {child.stderr}")


if __name__ == "__main__":
    if len(sys.argv) == 3 and sys.argv[1] == "mode":
        check_mode(sys.argv[2])
    elif len(sys.argv) == 5 and sys.argv[1] == "pid":
        check_pid(*sys.argv[2:])
    elif len(sys.argv) == 5 and sys.argv[1] == "credentials":
        check_credentials(*sys.argv[2:])
    else:
        raise SystemExit("usage: check_vm_directory_os.py mode DIR | pid VM IMAGE WORKDIR | credentials VM IMAGE WORKDIR")
