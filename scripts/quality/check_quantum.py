"""Quantum library checks. Qiskit tests skip when optional SDKs are absent.

Run with the Python environment containing phụ thuộc qiskit.txt. Hardware
tests replace the service and sampler; they never create a remote job.
"""

import importlib.util
import io
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stdout
from unittest.mock import Mock, patch

sys.dont_write_bytecode = True
ROOT = Path(__file__).resolve().parents[2]
BRIDGE = ROOT / "gói/lượng tử/cầu nối qiskit.py"
spec = importlib.util.spec_from_file_location("vpp_quantum_bridge", BRIDGE)
bridge = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bridge)
HAS_QISKIT = all(importlib.util.find_spec(name) is not None
                 for name in ("qiskit", "qiskit_aer", "qiskit_qasm3_import"))
CLI = Path(os.environ.get("VPP_EXEC", ROOT / "build/bin/vpp-cli")).resolve()


def request(action="simulate", **fields):
    return {"protocol": 1, "action": action, **fields}


def vpp(source, environment=None):
    # Keep sources under the project so bundled module discovery also works
    # with a CLI that has not been installed globally.
    (ROOT / ".tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="quantum-check-", dir=ROOT / ".tmp") as directory:
        path = Path(directory) / "main.vi"
        path.write_text(source, encoding="utf-8")
        completed = subprocess.run([str(CLI), "chạy", str(path)], cwd=ROOT,
                                   env=environment, capture_output=True, text=True, timeout=120)
        if completed.returncode:
            raise AssertionError(completed.stderr + completed.stdout)
        return [json.loads(line) for line in completed.stdout.splitlines() if line.strip()]


class ProtocolTests(unittest.TestCase):
    def test_invalid_requests_and_missing_dependency(self):
        for payload in ([], {}, {"protocol": True}, request("unknown")):
            with self.subTest(payload=payload), self.assertRaises(ValueError):
                bridge.execute(payload)
        for shots in (0, 1.5, True, 1_000_001):
            with self.subTest(shots=shots), self.assertRaises(ValueError):
                bridge.execute(request(shots=shots, seed=1, qasm="unused"))
        output = io.StringIO()
        with redirect_stdout(output), patch.object(bridge, "execute", side_effect=ImportError()):
            self.assertEqual(bridge.main([json.dumps(request())]), 0)
        response = json.loads(output.getvalue())
        self.assertFalse(response["ok"])
        self.assertIn("phụ thuộc qiskit.txt", response["error"])

    def test_stdout_is_reserved_for_json(self):
        def noisy(_):
            print("SDK diagnostic")
            return {"đếm": {"0": 1}}
        output, diagnostic = io.StringIO(), io.StringIO()
        with redirect_stdout(output), patch("sys.stderr", diagnostic), patch.object(bridge, "execute", noisy):
            bridge.main([json.dumps(request())])
        self.assertTrue(json.loads(output.getvalue())["ok"])
        self.assertEqual(diagnostic.getvalue().strip(), "SDK diagnostic")

    def test_cli_reports_invalid_json_and_oversize(self):
        for argv in ([], ["{"], ["x" * 24001]):
            output = io.StringIO()
            with redirect_stdout(output):
                bridge.main(argv)
            self.assertFalse(json.loads(output.getvalue())["ok"])


@unittest.skipUnless(CLI.is_file(), "V++ CLI has not been built")
class BridgeBoundaryTests(unittest.TestCase):
    def test_submit_poll_protocol_without_qpu(self):
        with tempfile.TemporaryDirectory(prefix="vpp-quantum-jobs-") as directory:
            fake = Path(directory) / "fake_service.py"
            fake.write_text('''import json, sys
request = json.loads(sys.argv[1])
assert request["protocol"] == 1
if request["action"] == "submit":
    assert request["backend"] == "test backend & literal"
    assert request["shots"] == 32
    assert request["qasm"].startswith("OPENQASM 3.0;")
    result = {"job_id": "test-job", "backend": request["backend"], "số lần": 32}
else:
    assert request["action"] == "result" and request["job_id"] == "test-job"
    result = {"job_id": "test-job", "status": "DONE", "ready": True, "đếm": {"0": 32}, "số lần": 32}
print(json.dumps({"protocol": 1, "ok": True, "result": result}))
''', encoding="utf-8")
            source = '''nhập lượng tử; nhập gói/lượng tử/qiskit; nhập json;
            hàm chính() {
                sdk = Cầu Nối Qiskit(PYTHON, BRIDGE);
                m = Mạch Lượng Tử(1); m.đo tất cả();
                job = sdk.gửi(m, "test backend & literal", 32);
                in json tạo(job);
                in json tạo(sdk.kết quả(job["job_id"]));
            };'''.replace("PYTHON", json.dumps(sys.executable)).replace(
                "BRIDGE", json.dumps(str(fake), ensure_ascii=False))
            submitted, result = vpp(source)
        self.assertEqual(submitted["job_id"], "test-job")
        self.assertEqual(result["status"], "DONE")
        self.assertEqual(result["đếm"], {"0": 32})

    def test_errors_cross_the_process_boundary(self):
        with tempfile.TemporaryDirectory(prefix="vpp-quantum-bridge-") as directory:
            fake = Path(directory) / "bridge with spaces.py"
            fake.write_text('''import json, sys
seed = json.loads(sys.argv[1])["seed"]
if seed == 1:
    print(json.dumps({"protocol": 1, "ok": False, "error": "SDK sentinel error"}))
elif seed == 2:
    print("invalid JSON")
elif seed == 3:
    print(json.dumps({"protocol": 2, "ok": True, "result": {}}))
elif seed == 4:
    print("child sentinel error", file=sys.stderr)
    sys.exit(9)
elif seed == 5:
    print(json.dumps({"protocol": 1, "ok": True, "result": None}))
''', encoding="utf-8")
            source = '''nhập lượng tử; nhập gói/lượng tử/qiskit; nhập json;
            hàm chính() {
                sdk = Cầu Nối Qiskit(PYTHON, BRIDGE);
                m = Mạch Lượng Tử(1); m.đo tất cả();
                lặp(seed = 1; seed <= 5; seed++) {
                    thử { sdk.mô phỏng(m, 1, seed); in json tạo("unexpected success"); }
                    bắt lỗi(e) { in json tạo(e); };
                };
            };'''.replace("PYTHON", json.dumps(sys.executable)).replace(
                "BRIDGE", json.dumps(str(fake), ensure_ascii=False))
            errors = vpp(source)
        self.assertEqual(len(errors), 5)
        self.assertIn("SDK sentinel error", errors[0])
        self.assertIn("qiskit: phản hồi JSON không hợp lệ", errors[1])
        self.assertIn("sai phiên bản giao thức", errors[2])
        self.assertIn("child sentinel error", errors[3])
        self.assertIn("qiskit: phản hồi thiếu kết quả hợp lệ", errors[4])


@unittest.skipUnless(HAS_QISKIT, "optional Qiskit/Aer environment is not installed")
class QiskitTests(unittest.TestCase):
    def test_random_unitary_circuits_match_qiskit(self):
        from qiskit import QuantumCircuit, qasm3
        from qiskit.quantum_info import Statevector
        import numpy as np

        rng = random.Random(20261022)
        circuits, source = [], ["nhập lượng tử; nhập json; hàm chính() {"]
        for n in (1, 2, 3, 4):
            for _ in range(3):
                qc = QuantumCircuit(n)
                source.append(f"m = Mạch Lượng Tử({n});")
                for _ in range(20):
                    gate = rng.choice(["h", "x", "y", "z", "s", "t"] + (["cx"] if n > 1 else []))
                    q = rng.randrange(n)
                    if gate == "cx":
                        target = rng.choice([i for i in range(n) if i != q])
                        qc.cx(q, target)
                        source.append(f"m.cx({q}, {target});")
                    else:
                        getattr(qc, gate)(q)
                        source.append(f"m.{gate}({q});")
                source.append('máy = Bộ Mô Phỏng Lượng Tử(); in json tạo(máy.trạng thái(m));')
                source.append('m.đo tất cả(); in json tạo(xuất openqasm(m));')
                circuits.append(qc)
        source.append("};")
        rows = vpp("\n".join(source))
        self.assertEqual(len(rows), 2 * len(circuits))
        for i, qc in enumerate(circuits):
            with self.subTest(circuit=i):
                state, qasm = rows[2 * i:2 * i + 2]
                actual = np.array(state["thực"]) + 1j * np.array(state["ảo"])
                np.testing.assert_allclose(actual, Statevector.from_instruction(qc).data, atol=1e-9)
                np.testing.assert_allclose(state["xác suất"], abs(actual) ** 2, atol=1e-9)
                imported = qasm3.loads(qasm)
                self.assertEqual(imported.num_qubits, qc.num_qubits)
                self.assertEqual(imported.count_ops()["measure"], qc.num_qubits)
                # Remove terminal measurements to compare the exported circuit's semantics.
                exported_state = Statevector.from_instruction(imported.remove_final_measurements(inplace=False))
                np.testing.assert_allclose(actual, exported_state.data, atol=1e-9)

    def test_vpp_to_aer_roundtrip(self):
        source = '''nhập lượng tử; nhập gói/lượng tử/qiskit; nhập json;
        hàm chính() {
            sdk = Cầu Nối Qiskit(PYTHON, BRIDGE);
            m = Mạch Lượng Tử(2); m.h(0).cx(0, 1).đo tất cả();
            in json tạo(sdk.mô phỏng(m, 512, 42));
        };'''.replace("PYTHON", json.dumps(sys.executable)).replace("BRIDGE", json.dumps(str(BRIDGE), ensure_ascii=False))
        environment = os.environ.copy()
        environment["PYTHONIOENCODING"] = "ascii"
        rows = vpp(source, environment=environment)
        self.assertEqual(rows[0]["backend"], "qiskit-aer")
        counts = rows[0]["đếm"]
        self.assertEqual(set(counts), {"00", "11"})
        self.assertEqual(sum(counts.values()), 512)

    def test_dynamic_circuits_and_bit_mapping_match_aer(self):
        bodies = [
            'm = Mạch Lượng Tử(1, 2); m.h(0).đo(0, 0).x(0).đo(0, 1);',
            'm = Mạch Lượng Tử(2); m.h(0).cx(0, 1).reset(0).đo tất cả();',
            'm = Mạch Lượng Tử(2); m.h(0).h(1).đo(0, 0).h(1).đo(1, 1);',
            'm = Mạch Lượng Tử(2, 3); m.x(0).đo(0, 2).đo(1, 0);',
        ]
        source = "nhập lượng tử; nhập json; hàm chính() { máy = Bộ Mô Phỏng Lượng Tử();\n"
        for body in bodies:
            source += body + '\nin json tạo(máy.chạy(m, 256, 42)); in json tạo(xuất openqasm(m));\n'
        rows = vpp(source + "};")
        for i in range(len(bodies)):
            local, qasm = rows[2 * i:2 * i + 2]
            aer = bridge.execute(request(qasm=qasm, shots=256, seed=42))
            self.assertEqual(set(local["đếm"]), set(aer["đếm"]))
            self.assertEqual(sum(aer["đếm"].values()), 256)
            for key, count in local["đếm"].items():
                self.assertLess(abs(count - aer["đếm"][key]), 64)

    def test_submit_and_poll_without_remote_jobs(self):
        backend, compiled, job = object(), object(), Mock()
        job.job_id.return_value = "job-test"
        service, sampler = Mock(), Mock()
        service.backend.return_value = backend
        service.job.return_value = job
        sampler.run.return_value = job
        runtime = types.ModuleType("qiskit_ibm_runtime")
        runtime.QiskitRuntimeService = Mock(return_value=service)
        runtime.SamplerV2 = Mock(return_value=sampler)
        manager = Mock()
        manager.run.return_value = compiled
        qasm = 'OPENQASM 3.0; include "stdgates.inc"; qubit[1] q; bit[1] c; c[0]=measure q[0];'
        with patch.dict(sys.modules, {"qiskit_ibm_runtime": runtime}), patch(
                "qiskit.transpiler.preset_passmanagers.generate_preset_pass_manager", return_value=manager):
            result = bridge.execute(request("submit", qasm=qasm, backend="test-backend", shots=32))
            self.assertEqual(result["job_id"], "job-test")
            runtime.SamplerV2.assert_called_once_with(mode=backend)
            sampler.run.assert_called_once_with([compiled], shots=32)
            job.status.return_value = "RUNNING"
            result = bridge.execute(request("result", job_id="job-test"))
            self.assertFalse(result["ready"])
            job.result.assert_not_called()
            job.status.return_value = "DONE"
            job.result.return_value = [types.SimpleNamespace(data=types.SimpleNamespace(
                c=types.SimpleNamespace(get_counts=lambda: {"0": 32}))) ]
            result = bridge.execute(request("result", job_id="job-test"))
            self.assertEqual(result["đếm"], {"0": 32})
            for status in ("ERROR", "CANCELLED"):
                job.status.return_value = status
                self.assertFalse(bridge.execute(request("result", job_id="job-test"))["ready"])

    def test_bridge_validates_before_starting_python(self):
        source = '''nhập gói/lượng tử/qiskit; nhập lượng tử; nhập json;
        hàm chính() {
            m = Mạch Lượng Tử(1); m.đo tất cả();
            sdk = Cầu Nối Qiskit("/nonexistent/python", "/nonexistent/bridge.py");
            lỗi = sai;
            thử { sdk.mô phỏng(m, 0); } bắt lỗi(e) { lỗi = đúng; };
            in json tạo(lỗi);
        };'''
        self.assertEqual(vpp(source), [1])


if __name__ == "__main__":
    unittest.main(verbosity=2)
