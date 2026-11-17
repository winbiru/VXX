#!/usr/bin/env python3
"""Exercise native package commands without network or a real user project."""
import argparse
import json
import os
from pathlib import Path
import shutil
import stat
import subprocess
import tempfile


def check(tools, compiler):
    with tempfile.TemporaryDirectory(prefix="vpp-package-regression-") as work:
        root = Path(work)
        publisher, project, registry = (root / name for name in ("publisher", "project", "registry"))
        for directory in (publisher, project, registry):
            directory.mkdir()

        def run(cwd, *args, ok=True):
            result = subprocess.run([str(tools), str(compiler), *map(str, args)],
                                    cwd=cwd, capture_output=True, timeout=120)
            output = result.stdout.decode("utf-8", errors="replace")
            errors = result.stderr.decode("utf-8", errors="replace")
            assert (result.returncode == 0) == ok, (args, result.returncode, output, errors)
            return output

        def write(path, value):
            path.write_text(json.dumps(value, ensure_ascii=False) + "\n", encoding="utf-8")

        def publish(version):
            write(publisher / "vpp.json", dict(schema=1, name="lib", version=version, dependencies=[]))
            (publisher / "chính.vi").write_text("hàm công khai giá trị() { trả về 42; };\n", encoding="utf-8")
            run(publisher, "gói", "phát hành", registry)

        publish("1.2.0")
        publish("1.4.0")
        run(project, "gói", "khởi-tạo", "demo")
        run(project, "gói", "cài-đặt", f"registry+{registry}#lib@^1.2.0")
        lockfile = project / "vpp.lock"
        lock = json.loads(lockfile.read_bytes())
        entry, = lock["packages"]
        assert entry["version"] == "1.4.0", lock
        installed = project / entry["resolved"]
        shutil.rmtree(installed)
        run(project, "gói", "phục-hồi", "--offline")
        assert (installed / "chính.vi").is_file()
        run(project, "gói", "khóa")
        assert run(project, "gói", "danh-sách").strip() == "lib"
        saved = lockfile.read_bytes()
        marker = registry / "vpp-registry.json"
        write(marker, dict(schema=99))
        run(project, "gói", "đồng-bộ", ok=False)
        assert lockfile.read_bytes() == saved
        write(publisher / "vpp.json", dict(schema=1, name="lib", version="1.5.0", dependencies=[]))
        run(publisher, "gói", "phát-hành", registry, ok=False)
        assert not (registry / "lib/1.5.0").exists()
        write(marker, dict(schema=1))
        bad = json.loads(saved)
        bad["packages"][0]["source"] = "bad"
        write(lockfile, bad)
        run(project, "gói", "phục hồi", "--offline", ok=False)
        lockfile.write_bytes(saved)
        (installed / "chính.vi").write_text("tampered", encoding="utf-8")
        probe = project / "main.vi"
        probe.write_text("hàm chính() { in 42; };\n", encoding="utf-8")
        run(project, "--kiểm-khóa-nguồn", probe, ok=False)
        run(project, "gói", "phục hồi", "--offline")
        run(project, "--kiểm-khóa-nguồn", probe)
        legacy = root / "legacy"
        (legacy / "packages/lib").mkdir(parents=True)
        (legacy / "packages/lib/chính.vi").write_text("hàm công khai giá trị() { trả về 42; };\n", encoding="utf-8")
        assert "Số gói: 1" in run(legacy, "thống kê")
        run(legacy, "gói", "khởi tạo", "CON", ok=False)
        run(legacy, "gói", "khởi tạo", "legacy")
        run(legacy, "gói", "đồng bộ")
        assert "lib" in run(legacy, "gói", "danh sách")

        # A non-default branch must resolve after clone, and restoring a lock
        # must fetch the recorded commit even after the branch advances.
        repository = root / "gitlib"
        repository.mkdir()

        def git(*args):
            return subprocess.check_output(["git", "-C", str(repository), *args],
                                           stderr=subprocess.STDOUT, timeout=30).decode().strip()

        git("init", "-b", "main")
        git("config", "user.name", "VPP Regression")
        git("config", "user.email", "vpp-regression@example.invalid")
        write(repository / "vpp.json", dict(schema=1, name="gitlib", version="1.0.0", dependencies=[]))
        module = repository / "chính.vi"
        module.write_text("hàm công khai giá trị() { trả về 1; };\n", encoding="utf-8")
        git("add", ".")
        git("commit", "-m", "main")
        git("checkout", "-b", "stable")
        module.write_text("hàm công khai giá trị() { trả về 2; };\n", encoding="utf-8")
        git("commit", "-am", "stable")
        revision = git("rev-parse", "HEAD")
        expected = module.read_bytes()
        git("checkout", "main")

        def source_objects():
            objects = [p for p in (repository / ".git/objects").rglob("*") if p.is_file()]
            assert objects
            if os.name == "nt":
                for p in objects:
                    p.chmod(stat.S_IREAD)
            return {p: (p.read_bytes(), p.stat().st_mode,
                        getattr(p.stat(), "st_file_attributes", None)) for p in objects}

        def assert_clean(snapshot):
            assert not list((gitproject / ".vpp").glob("stage-*"))
            for p, (contents, mode, attributes) in snapshot.items():
                assert p.read_bytes() == contents, p
                assert p.stat().st_mode == mode, p
                assert getattr(p.stat(), "st_file_attributes", None) == attributes, p

        snapshot = source_objects()
        gitproject = root / "gitproject"
        gitproject.mkdir()
        run(gitproject, "khởi tạo", "gitproject")
        run(gitproject, "gói", "cài đặt", f"git+{repository}#stable")
        assert_clean(snapshot)
        entry, = json.loads((gitproject / "vpp.lock").read_bytes())["packages"]
        assert entry["revision"] == revision, entry
        installed = gitproject / entry["resolved"]
        assert (installed / "chính.vi").read_bytes() == expected
        git("checkout", "stable")
        module.write_text("hàm công khai giá trị() { trả về 3; };\n", encoding="utf-8")
        git("commit", "-am", "advance stable")
        snapshot = source_objects()
        shutil.rmtree(installed)
        shutil.rmtree(gitproject / ".vpp/cache")
        run(gitproject, "gói", "phục hồi", "--offline", ok=False)
        assert_clean(snapshot)
        run(gitproject, "gói", "phục hồi")
        assert_clean(snapshot)
        assert (installed / "chính.vi").read_bytes() == expected
        run(gitproject, "gói", "cập nhật")
        assert_clean(snapshot)
        assert (installed / "chính.vi").read_bytes() == module.read_bytes()

        if os.name == "nt":
            # Exercise the production AOT directory API with read-only files,
            # including a hardlink to an external file whose attributes must
            # survive removal of the link inside the temporary tree.
            from check_windows_full_bootstrap import package_env
            tree = root / "dọn cây"
            tree.mkdir()
            sentinel = root / "sentinel.txt"
            sentinel.write_bytes(b"preserved")
            os.link(sentinel, tree / "liên kết.txt")
            sentinel.chmod(stat.S_IREAD)
            readonly = tree / "chỉ đọc.txt"
            readonly.write_bytes(b"remove me")
            readonly.chmod(stat.S_IREAD)
            attributes = sentinel.stat().st_file_attributes
            assert attributes & stat.FILE_ATTRIBUTE_READONLY
            source = root / "cleanup.vi"
            source.write_text('nhập nhập xuất;\nhàm chính() { in xóa đường dẫn("dọn cây"); in xóa đường dẫn("dọn cây"); };\n', encoding="utf-8")
            binary = root / "cleanup.exe"
            clang = Path(os.environ.get("VPP_CI_CLANG") or shutil.which("clang") or "clang.exe")
            env = package_env(compiler.parent.parent, clang)
            subprocess.run([str(compiler), "build", str(source), "-o", str(binary)],
                           cwd=root, env=env, check=True, timeout=240)
            result = subprocess.run([str(binary)], cwd=root, capture_output=True, timeout=30)
            assert result.returncode == 0 and result.stdout.splitlines() == [b"3", b"0"], result
            assert not tree.exists()
            assert sentinel.read_bytes() == b"preserved"
            assert sentinel.stat().st_file_attributes == attributes
    print("NATIVE_PACKAGE_MANAGER=PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("tools", type=Path)
    parser.add_argument("compiler", type=Path)
    args = parser.parse_args()
    check(args.tools.resolve(), args.compiler.resolve())
