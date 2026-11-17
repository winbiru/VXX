#!/usr/bin/env python3
"""Exercise the OS-thread boundary directly; guest worker ABI tests are separate."""
import ctypes as C
import os
from pathlib import Path
import platform
import struct
import subprocess
import tempfile
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
OFFSET = 8388608
ARENA_SIZE = 135271424
ENTRY = C.CFUNCTYPE(C.c_int32, C.c_void_p, C.c_void_p)


def coff_boundary(path):
    data = path.read_bytes()
    machine, count, _, symbols, nsyms, optional, _ = struct.unpack_from('<HHIIIHH', data)
    assert machine == 0x8664 and optional == 0
    sections = {}
    for i in range(count):
        name, _, _, size, _, reloc, _, nreloc, _, flags = struct.unpack_from(
            '<8sIIIIIIHHI', data, 20 + i * 40)
        types = [struct.unpack_from('<H', data, reloc + j * 10 + 8)[0] for j in range(nreloc)]
        sections[name.rstrip(b'\0').decode('ascii')] = (size, flags, types)
    undefined = set()
    strings = symbols + nsyms * 18
    i = 0
    while i < nsyms:
        off = symbols + i * 18
        name = data[off:off + 8]
        _, section, _, storage, aux = struct.unpack_from('<IhHBB', data, off + 8)
        if section == 0 and storage == 2:
            if name[:4] == b'\0' * 4:
                start = strings + struct.unpack_from('<I', name, 4)[0]
                name = data[start:data.index(b'\0', start)]
            undefined.add(name.rstrip(b'\0').decode('utf-8'))
        i += 1 + aux
    return sections, undefined


def check_windows_boundaries(clang, directory):
    # The pinned i32 PE linker permits these legacy sections and imports.
    # A new full-runtime dependency must not accidentally enter that seed.
    legacy_imports = set('''BCryptCloseAlgorithmProvider BCryptCreateHash
        BCryptDestroyHash BCryptFinishHash BCryptHashData BCryptOpenAlgorithmProvider
        CloseHandle CommandLineToArgvW CreateFileW GetCommandLineW GetEnvironmentVariableW
        GetFileInformationByHandle GetLastError GetModuleFileNameW LocalFree MoveFileExW
        MultiByteToWideChar SetLastError VirtualAlloc WideCharToMultiByte _close _cwait
        _errno _get_osfhandle _lseeki64 _read _waccess _wgetcwd _wmkdir _wmktemp_s
        _wopen _write _wspawnvp _wunlink atexit calloc free malloc realloc'''.split())
    flags = {'.text': 0x60500020, '.data': 0xc0400040,
             '.bss': 0xc0300080, '.rdata': 0x40100040}
    windows = ROOT / 'scripts/ASM/windows'

    def assemble(name, source, *defines):
        target = directory / (name + '.obj')
        subprocess.run([clang, '-target', 'x86_64-pc-windows-msvc', '-c',
                        *defines, str(windows / source), '-o', str(target)], check=True)
        return coff_boundary(target)

    sections, imports = assemble('i32', 'vpp_aot_primitives_windows_x86_64.S',
        '-DVPP_NATIVE_RUNTIME_STACK_ABI=1', '-DVPP_JIT_DISABLED=1', '-DVPP_WINDOWS_I32_BOOTSTRAP=1')
    assert set(sections) == set(flags)
    assert sections['.bss'] == (0, flags['.bss'], [])
    for name, (size, characteristics, types) in sections.items():
        assert size <= 65536 and characteristics == flags[name]
        assert all(4 <= t <= 9 for t in types), (name, types)
    assert {name for name in imports if not name.startswith('vpp_fn_')} <= legacy_imports
    assert 'vpp_fn_vm_worker_entry' not in imports
    required = {'TlsAlloc', 'TlsGetValue', 'TlsSetValue', '_beginthreadex', 'WaitForSingleObject',
                'SetFileInformationByHandle'}
    for name, defines in [('native', []), ('raw', ['-DVPP_WINDOWS_RAW_BOUNDARY=1']),
                          ('vm', ['-DVPP_STANDALONE_VM_WORKER=1'])]:
        sections, imports = assemble(name, 'vpp_windows_full_runtime_x86_64.S', *defines)
        assert sections['.bss'][0] > 0 and required <= imports, (name, imports)
        assert ('vpp_fn_vm_worker_entry' in imports) == (name == 'vm'), name
    print('PASS Windows COFF boundaries: legacy i32 seed, native/raw/VM worker imports')


def check(dylib):
    lib = C.CDLL(str(dylib))

    def bind(name, result, *args):
        fn = getattr(lib, "vpp_native_" + name)
        fn.restype = result
        fn.argtypes = args
        return fn

    ptr, i32 = C.c_void_p, C.c_int32
    out = C.POINTER(i32)
    base = bind("memory_base", ptr)
    current = bind("memory_current", ptr)
    worker_current = bind("worker_current", ptr)
    read = bind("memory_read_i32", i32, i32)
    write = bind("memory_write_i32", i32, i32, i32)
    start = bind("worker_start", ptr, ENTRY, ptr)
    state = bind("worker_state", i32, ptr)
    cancel = bind("worker_request_cancel", i32, ptr)
    cancelled = bind("worker_cancelled", i32, ptr)
    join = bind("worker_join", i32, ptr)
    result = bind("worker_result", i32, ptr, out)
    remote_read = bind("worker_read_i32", i32, ptr, i32, out)
    dispose = bind("worker_dispose", i32, ptr)

    root = base()
    assert root and write(OFFSET, 99) == 0 and not worker_current()
    count = 24
    ready = threading.Barrier(count + 1, timeout=20)
    release = threading.Event()
    errors = []
    arenas = {}
    records = []

    @ENTRY
    def entry(record, context):
        try:
            n = int(context)
            assert worker_current() == record
            arena = current()
            assert arena and arena != root and read(OFFSET) == 0
            arenas[n] = arena
            assert write(OFFSET, n) == 0
            ready.wait()
            assert release.wait(20)
            assert current() == arena and read(OFFSET) == n
            # Cancellation is cooperative and visible on the worker thread.
            if n % 2:
                deadline = time.monotonic() + 10
                while not cancelled(record):
                    assert time.monotonic() < deadline
                    time.sleep(0.001)
                return -n
            assert not cancelled(record)
            return n * 10
        except BaseException as exc:
            errors.append(repr(exc))
            return -999

    try:
        for n in range(1, count + 1):
            record = start(entry, n)
            assert record
            records.append(record)
        ready.wait()
        assert len(set(arenas.values())) == count
        assert current() == root and read(OFFSET) == 99 and not worker_current()
        value = i32()
        for n, record in enumerate(records, 1):
            assert state(record) == 0
            assert remote_read(record, OFFSET, C.byref(value)) == -1
            assert result(record, C.byref(value)) == -1
            assert dispose(record) == -1
            if n % 2:
                assert cancel(record) == 1
                assert cancel(record) == 0
        release.set()
        for n, record in enumerate(records, 1):
            assert join(record) == 0 and join(record) == 0
            assert state(record) == 1
            assert cancel(record) == 0
            assert cancelled(record) == n % 2
            assert result(record, C.byref(value)) == 0
            assert value.value == (-n if n % 2 else n * 10)
            assert remote_read(record, OFFSET, C.byref(value)) == 0
            assert value.value == n
            assert remote_read(record, -1, C.byref(value)) == -1
            assert remote_read(record, ARENA_SIZE - 3, C.byref(value)) == -1
            assert remote_read(record, OFFSET, None) == -1
        assert not errors, errors
        assert read(OFFSET) == 99
    finally:
        release.set()
        for record in records:
            cancel(record)
        for record in records:
            if join(record) == 0:
                assert dispose(record) == 0

    # Race cancellation against a returning callback. Exactly one transition
    # wins; the cancellation flag remains stable after completion publication.
    for _ in range(64):
        gate = threading.Event()

        @ENTRY
        def racing_entry(record, context):
            gate.wait(10)
            return 42

        record = start(racing_entry, None)
        assert record
        try:
            gate.set()
            won = cancel(record)
            assert won in (0, 1)
            assert join(record) == 0 and state(record) == 1
            assert cancelled(record) == won
            assert cancel(record) == 0
            assert cancelled(record) == won
        finally:
            assert dispose(record) == 0

    # A worker can start and join its own child; root arena is unchanged.
    @ENTRY
    def child(record, context):
        return 42 if read(OFFSET) == 0 and write(OFFSET, 123) == 0 else -1

    @ENTRY
    def parent(record, context):
        try:
            assert worker_current() == record
            assert write(OFFSET, 77) == 0
            nested = start(child, None)
            assert nested
            try:
                assert join(nested) == 0
                value = i32()
                assert result(nested, C.byref(value)) == 0 and value.value == 42
                assert remote_read(nested, OFFSET, C.byref(value)) == 0
                assert value.value == 123 and read(OFFSET) == 77
            finally:
                assert dispose(nested) == 0
            assert worker_current() == record
            return 7
        except BaseException as exc:
            errors.append(repr(exc))
            return -1

    record = start(parent, None)
    assert record
    try:
        assert join(record) == 0
        value = i32()
        assert result(record, C.byref(value)) == 0 and value.value == 7
        assert not errors, errors
        assert current() == root and read(OFFSET) == 99
    finally:
        assert dispose(record) == 0
    registry_start = bind("worker_registry_start", i32, ENTRY, ptr)
    registry_record = bind("worker_registry_record", ptr, i32)
    registry_self = bind("worker_registry_self", i32, ptr)
    wait_begin = bind("worker_registry_wait_begin", i32, i32, i32)
    wait_end = bind("worker_registry_wait_end", None, i32)
    shutdown = bind("worker_registry_shutdown", i32)
    open_registry = bind("worker_registry_open", i32)
    assert open_registry() == 0
    registered = {}
    registry_ready = threading.Barrier(4, timeout=10)
    finished = []

    @ENTRY
    def registered_entry(record, context):
        try:
            handle = registry_self(record)
            assert handle > 0 and registry_record(handle) == record
            registered[int(context)] = handle
            registry_ready.wait()
            deadline = time.monotonic() + 10
            while not cancelled(record):
                assert time.monotonic() < deadline
                time.sleep(0.001)
            # Once shutdown closes the root, no new worker can be published.
            assert registry_start(child, None) == -1
            finished.append(handle)
            return 0
        except BaseException as exc:
            errors.append(repr(exc))
            return -1

    try:
        handles = [registry_start(registered_entry, n) for n in range(1, 4)]
        assert handles == [1, 2, 3]
        registry_ready.wait()
        assert registered == {n: n for n in handles}
        assert not registry_record(0) and not registry_record(-1)
        assert not registry_record(4) and open_registry() == -1
        assert wait_begin(0, 1) == 0
        assert wait_begin(1, 1) == -2
        assert wait_begin(1, 4) == -1
        assert wait_begin(1, 2) == 0
        assert wait_begin(1, 3) == -2
        assert wait_begin(2, 3) == 0
        assert wait_begin(3, 1) == -2
        wait_end(2)
        assert wait_begin(3, 1) == 0
        wait_end(1)
        wait_end(3)
    finally:
        assert shutdown() == 0
    assert sorted(finished) == handles and not errors, errors
    assert not registry_record(1)
    assert registry_start(child, None) == -1
    assert open_registry() == 0
    handle = registry_start(child, None)
    assert handle == 1
    assert shutdown() == 0 and not registry_record(handle)
    # A snapshot must finish copying before spawn returns; a later mutation
    # of the caller's source must be invisible to the worker. Copy-out must
    # fail while running and support repeated readers after completion.
    start_copy = bind("worker_registry_start_copy", i32, ENTRY, i32, i32, i32)
    copy_result = bind("worker_registry_copy_result", i32, i32, i32, i32, i32)
    source, dest, output = OFFSET, OFFSET + 8192, OFFSET + 16384
    payload = bytes((i * 37 + 17) % 256 for i in range(4099))
    C.memmove(root + source, payload, len(payload))
    snapshot_release = threading.Event()

    @ENTRY
    def snapshot_entry(record, context):
        try:
            assert context == dest
            assert snapshot_release.wait(10)
            assert C.string_at(current() + dest, len(payload)) == payload
            C.memmove(current() + dest, payload[::-1], len(payload))
            return 123
        except BaseException as exc:
            errors.append(repr(exc))
            return -1

    assert open_registry() == 0
    try:
        for bad_source, bad_length, bad_dest in [(-1, 1, dest), (source, -1, dest),
                                                (ARENA_SIZE - 1, 2, dest),
                                                (source, 2, ARENA_SIZE - 1)]:
            assert start_copy(snapshot_entry, bad_source, bad_length, bad_dest) == -1
        handle = start_copy(snapshot_entry, source, len(payload), dest)
        assert handle == 1
        C.memset(root + source, 0, len(payload))
        assert copy_result(handle, dest, len(payload), output) == -1
        snapshot_release.set()
        deadline = time.monotonic() + 10
        while state(registry_record(handle)) == 0:
            assert time.monotonic() < deadline
            time.sleep(0.001)
        assert state(registry_record(handle)) == 1
        for _ in range(3):
            assert copy_result(handle, dest, len(payload), output) == 0
            assert C.string_at(root + output, len(payload)) == payload[::-1]
            C.memset(root + output, 0, len(payload))
        assert copy_result(0, dest, len(payload), output) == -1
        assert copy_result(handle, -1, 1, output) == -1
        assert copy_result(handle, dest, -1, output) == -1
        assert copy_result(handle, dest, 2, ARENA_SIZE - 1) == -1
        assert not errors, errors
    finally:
        snapshot_release.set()
        assert shutdown() == 0
    # Fast callbacks may complete before the spawning thread sees readiness.
    # Raw callbacks retain entire arenas until shutdown. Exercise all 64
    # spawns across four root lifecycles to bound Windows commit charge.
    for _ in range(4):
        assert open_registry() == 0
        try:
            for n in range(1, 17):
                assert start_copy(child, source, 0, dest) == n
            for n in range(1, 17):
                record = registry_record(n)
                assert join(record) == 0
                assert result(record, C.byref(value)) == 0 and value.value == 42
                assert remote_read(record, OFFSET, C.byref(value)) == 0
                assert value.value == 123
        finally:
            assert shutdown() == 0
    # Exercise the pointer-safe i32 service that will be used by the VM.
    service = bind("worker_service", i32, i32, i32, i32)
    set_entry = bind("worker_set_entry", i32, ptr)
    staging = 89000000
    descriptor = OFFSET + 24576
    service_release = threading.Event()

    @ENTRY
    def service_entry(record, context):
        try:
            assert context == staging and worker_current() == record
            self_handle = service(4, 0, 0)
            assert self_handle > 0
            assert service(6, self_handle, 0) == -2
            assert service(10, 0, 0) == -1
            assert service(11, 0, 0) == -1
            assert set_entry(None) == -1
            assert C.string_at(current() + staging, len(payload)) == payload
            assert service_release.wait(10)
            if service(5, 0, 0):
                return -staging
            C.memmove(current() + staging, payload[::-1], len(payload))
            return staging
        except BaseException as exc:
            errors.append(repr(exc))
            return -1

    assert service(0, 0, 0) == 0
    assert service(1, source, len(payload)) == -1
    assert service(11, 0, 0) == 0
    assert set_entry(C.cast(service_entry, ptr)) == 0
    try:
        assert service(0, 0, 0) == 1
        assert service(4, 0, 0) == service(5, 0, 0) == 0
        assert service(-1, 0, 0) == service(12, 0, 0) == -1
        assert service(2, 0, 0) == service(3, -1, 0) == -1
        assert service(1, source, 31000001) == -1
        C.memmove(root + source, payload, len(payload))
        h1 = service(1, source, len(payload))
        h2 = service(1, source, len(payload))
        assert (h1, h2) == (1, 2)
        assert set_entry(None) == -1
        C.memset(root + source, 0, len(payload))
        assert service(2, h1, 0) == service(2, h2, 0) == 0
        assert service(6, h1, 0) == service(7, 0, 0) == 0
        assert service(8, h1, 0) == -1
        assert service(3, h2, 0) == 1
        assert service(3, h2, 0) == 0
        assert service(2, h2, 0) == 0
        service_release.set()
        deadline = time.monotonic() + 10
        while service(2, h1, 0) == 0 or service(2, h2, 0) == 0:
            assert time.monotonic() < deadline
            time.sleep(0.001)
        assert service(2, h1, 0) == 1
        assert service(2, h2, 0) == 2
        assert service(3, h1, 0) == service(3, h2, 0) == 0
        assert service(8, h1, 0) == staging
        assert service(8, h2, 0) == -staging
        assert write(descriptor, staging) == 0
        assert write(descriptor + 4, len(payload)) == 0
        assert write(descriptor + 8, output) == 0
        assert service(9, h1, -1) == -1
        assert service(9, h1, ARENA_SIZE - 11) == -1
        for _ in range(3):
            assert service(9, h1, descriptor) == 0
            assert C.string_at(root + output, len(payload)) == payload[::-1]
            C.memset(root + output, 0, len(payload))
        assert not errors, errors
    finally:
        service_release.set()
        assert service(10, 0, 0) == 0
    assert service(0, 0, 0) == 0
    assert service(2, h1, 0) == -1
    # Trusted guest callbacks release their arena before publishing a result.
    # Check the actual mapping as well as the registry metadata: a changed
    # pointer alone would not detect a leaked original arena.
    import struct
    compact = bind("worker_compact_result", i32, ptr, i32)
    if platform.system() == "Windows":
        class MemoryInfo(C.Structure):
            _fields_ = [("base", ptr), ("allocation_base", ptr),
                        ("allocation_protect", C.c_uint32),
                        ("partition_id", C.c_uint16), ("size", C.c_size_t),
                        ("state", C.c_uint32), ("protect", C.c_uint32),
                        ("type", C.c_uint32)]

        query = C.WinDLL("kernel32").VirtualQuery
        query.argtypes = [ptr, C.POINTER(MemoryInfo), C.c_size_t]
        query.restype = C.c_size_t

        def mapped(address):
            info = MemoryInfo()
            assert query(address, C.byref(info), C.sizeof(info)) == C.sizeof(info)
            return info.state != 0x10000  # MEM_FREE

    else:
        libc = C.CDLL(None)
        region = libc.mach_vm_region
        u32, u64 = C.c_uint32, C.c_uint64
        region.argtypes = [u32, C.POINTER(u64), C.POINTER(u64), C.c_int,
                           C.POINTER(i32), C.POINTER(u32), C.POINTER(u32)]
        region.restype = C.c_int
        task = u32.in_dll(libc, "mach_task_self_").value
        libc.mach_port_deallocate.argtypes = [u32, u32]
        libc.mach_port_deallocate.restype = C.c_int

        def mapped(address):
            start, size = u64(address), u64()
            info, count, object_name = (i32 * 32)(), u32(32), u32()
            rc = region(task, C.byref(start), C.byref(size), 9, info,
                        C.byref(count), C.byref(object_name))
            if object_name.value:
                assert libc.mach_port_deallocate(task, object_name) == 0
            assert rc in (0, 1), rc  # KERN_SUCCESS / KERN_INVALID_ADDRESS
            return rc == 0 and start.value <= address < start.value + size.value

    packet_offset = OFFSET + 16
    packet = struct.pack("<4I", 0x57505001, 16 + len(payload), 0, 0) + payload
    original_arenas = {}

    def word(record, offset):
        return i32.from_address(record + offset).value

    def arena_pointer(record):
        return ptr.from_address(record + 24).value

    @ENTRY
    def compact_entry(record, context):
        try:
            arena = current()
            original_arenas[record] = arena
            mode = C.c_ubyte.from_address(arena + staging).value
            C.memmove(arena + packet_offset, packet, len(packet))
            offset = packet_offset
            if mode == 1:
                offset = -offset
            elif mode == 2:
                offset = -1
            elif mode == 3:
                C.memset(arena + packet_offset, 0, 4)
            elif mode == 4:
                offset = OFFSET
            elif mode == 5:
                write(packet_offset + 4, 31000001)
            # The arena is no longer accessible after successful compaction.
            return compact(record, offset)
        except BaseException as exc:
            errors.append(repr(exc))
            return -1

    assert service(11, 0, 0) == 0
    assert set_entry(C.cast(compact_entry, ptr)) == 0
    try:
        # Keep the result packets live across many sequential jobs. Retaining
        # an arena per job would consume over 16 GiB of virtual mappings.
        retained = []
        for n in range(134):
            mode = n if n < 6 else n % 3
            C.c_ubyte.from_address(root + source).value = mode
            handle = service(1, source, 1)
            assert handle == n + 1
            record = registry_record(handle)
            deadline = time.monotonic() + 10
            while service(2, handle, 0) == 0:
                assert time.monotonic() < deadline
                time.sleep(0.001)
            assert word(record, 44) == 1, "terminal OS thread was not reaped"
            assert not errors, errors
            old = original_arenas[record]
            if mode >= 3:
                assert arena_pointer(record) == old and word(record, 60) == 0
                continue
            assert arena_pointer(record) != old
            # Probe an interior page unlikely to be reused by an unrelated
            # small mapping. Query the OS mapping, rather than page residency.
            assert not mapped(old + 64 * 1024 * 1024)
            signed = -1 if mode == 2 else (-packet_offset if mode == 1 else packet_offset)
            assert service(8, handle, 0) == signed
            assert service(2, handle, 0) == (1 if mode == 0 else 3)
            if mode == 2:
                assert arena_pointer(record) is None and word(record, 60) == 0
                assert copy_result(handle, packet_offset, 1, output) == -1
                assert remote_read(record, packet_offset, C.byref(value)) == -1
                continue
            assert word(record, 60) == len(packet)
            retained.append((handle, record))
            assert remote_read(record, packet_offset, C.byref(value)) == 0
            assert value.value == 0x57505001
            assert remote_read(record, packet_offset - 4, C.byref(value)) == -1
            assert remote_read(record, packet_offset + len(packet) - 3, C.byref(value)) == -1
            assert copy_result(handle, packet_offset - 1, 1, output) == -1
            assert copy_result(handle, packet_offset, len(packet) + 1, output) == -1
        for handle, record in retained:
            for length in (16, len(packet), len(packet)):
                assert copy_result(handle, packet_offset, length, output) == 0
                assert C.string_at(root + output, length) == packet[:length]
                C.memset(root + output, 0, length)
        # Concurrent observers can reap/read the same completed record safely.
        # No guest arena is needed by these host-only status/read operations.
        C.c_ubyte.from_address(root + source).value = 0
        handle = service(1, source, 1)
        record = registry_record(handle)
        deadline = time.monotonic() + 10
        while state(record) == 0:
            assert time.monotonic() < deadline
            time.sleep(0.001)
        assert word(record, 44) == 0
        observers_ready = threading.Barrier(8, timeout=10)

        def observe():
            try:
                observers_ready.wait()
                for _ in range(100):
                    observed = i32()
                    assert service(2, handle, 0) == 1
                    assert remote_read(record, packet_offset, C.byref(observed)) == 0
                    assert observed.value == 0x57505001
            except BaseException as exc:
                errors.append(repr(exc))

        readers = [threading.Thread(target=observe) for _ in range(8)]
        for reader in readers:
            reader.start()
        for reader in readers:
            reader.join(10)
            assert not reader.is_alive()
        assert word(record, 44) == 1
        assert not errors, errors
    finally:
        assert service(10, 0, 0) == 0
    assert not registry_record(1)
    print("PASS worker compaction: 135 jobs, released arena mappings, signed packets, fallback, repeated reads and terminal reaping")
    print("PASS worker machine: 24 concurrent arenas, atomic cancel/completion, result reads, join/dispose, nested worker, root registry, wait cycles, input snapshots and repeated result copies, TLS identity and i32 worker service")


if __name__ == "__main__":
    system, arch = platform.system(), platform.machine().lower()
    if system == "Linux" and arch == "x86_64":
        clang = os.environ.get("VPP_CLANG", "clang")
        with tempfile.TemporaryDirectory(prefix="vpp-worker-linux-") as tmp:
            executable = Path(tmp) / "worker-test"
            for mode in ([], ["-DVPP_STANDALONE_VM_WORKER=1"]):
                subprocess.run([clang, *mode, "-nostdlib", "-static", "-Wl,-e,_start",
                                "-Wl,--no-undefined", "-Wl,-z,noexecstack",
                                "-I", str(ROOT / "scripts/ASM/common/posix"),
                                "-I", str(ROOT / "scripts/quality/fixtures"),
                                str(ROOT / "scripts/quality/fixtures/worker_machine_linux_x86_64.S"),
                                str(ROOT / "scripts/ASM/linux/vpp_linux_syscalls_x86_64.S"),
                                "-o", str(executable)], check=True)
                result = subprocess.run([str(executable)], timeout=90)
                if result.returncode:
                    raise SystemExit(f"Linux worker test failed: phase={result.returncode}, mode={mode}")
        print("PASS Linux freestanding workers: 24 arenas, private errno, cancellation, 135 compact snapshots, repeated copies, nested workers, self-wait detection and shutdown/join")
        raise SystemExit(0)
    windows = system == "Windows" and arch in ("amd64", "x86_64")
    macos = system == "Darwin" and arch in ("arm64", "x86_64")
    if not (macos or windows):
        raise SystemExit("Worker machine test requires Linux x86_64, macOS ARM64/x86_64 or Windows x64")
    with tempfile.TemporaryDirectory(prefix="vpp-worker-machine-", ignore_cleanup_errors=windows) as tmp:
        dylib = Path(tmp) / ("worker.dll" if windows else "worker.dylib")
        clang = os.environ.get("VPP_CLANG", os.environ.get("VPP_CI_CLANG", "clang"))
        check_windows_boundaries(clang, Path(tmp))
        if windows:
            args = ["-shared", "-Wl,--export-all-symbols", "-I", str(ROOT / "scripts/ASM/windows"),
                    str(ROOT / "scripts/quality/fixtures/worker_machine_windows_x86_64.S")]
        elif arch == "x86_64":
            args = ["-arch", "x86_64", "-dynamiclib", "-I", str(ROOT / "scripts/ASM/common/posix"),
                    str(ROOT / "scripts/quality/fixtures/worker_machine_posix_x86_64.S")]
        else:
            args = ["-dynamiclib", "-I", str(ROOT / "scripts/ASM/macos"),
                    str(ROOT / "scripts/quality/fixtures/worker_machine_macos_arm64.S")]
        subprocess.run([clang, *args, "-o", str(dylib)], check=True)
        check(dylib)
