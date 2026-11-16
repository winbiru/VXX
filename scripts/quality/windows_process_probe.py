"""Independent child-process oracle for the Windows V++ process regression."""
import ctypes
from ctypes import wintypes
import os
from pathlib import Path
import subprocess
import sys
import time


def main():
    mode = sys.argv[1]
    if mode == 'io':
        assert sys.argv[2:] == ['A B', 'á🐱', '', 'quote"inside', 'space \\']
        assert os.environ['VPP_CHILD_TEST'] == 'mới'
        for _ in range(32):
            os.write(1, b'A' * 2048)
            os.write(2, b'B' * 2048)
        return 7
    if mode == 'closed':
        os.write(1, b'closed\n')
        os.close(1)
        os.close(2)
        time.sleep(30)
        return 0
    if mode == 'tree':
        return subprocess.call([sys.executable, str(Path(__file__).resolve()), 'descendant'])
    if mode == 'descendant':
        os.write(1, str(os.getpid()).encode('ascii') + b'\n')
        time.sleep(30)
        return 0
    if mode == 'check-dead':
        pid = int(sys.argv[2])
        assert pid > 0
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.OpenProcess.argtypes = (wintypes.DWORD, wintypes.BOOL, wintypes.DWORD)
        kernel.OpenProcess.restype = wintypes.HANDLE
        kernel.WaitForSingleObject.argtypes = (wintypes.HANDLE, wintypes.DWORD)
        kernel.WaitForSingleObject.restype = wintypes.DWORD
        kernel.CloseHandle.argtypes = (wintypes.HANDLE,)
        handle = kernel.OpenProcess(0x100000, False, pid)  # SYNCHRONIZE
        if not handle:
            if ctypes.get_last_error() == 87:  # ERROR_INVALID_PARAMETER: gone
                return 0
            raise ctypes.WinError(ctypes.get_last_error())
        try:
            return 0 if kernel.WaitForSingleObject(handle, 5000) == 0 else 1
        finally:
            kernel.CloseHandle(handle)
    raise ValueError(mode)


if __name__ == '__main__':
    sys.exit(main())
