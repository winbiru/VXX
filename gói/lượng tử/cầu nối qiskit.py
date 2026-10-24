"""Optional Qiskit adapter for V++; one JSON request/response, no shell or eval.

Install phụ thuộc qiskit.txt into an isolated Python environment. Hardware
actions use an already saved QiskitRuntimeService account; tokens are not sent
through argv. Importing this module performs no remote operations.
"""

import contextlib
import json
import sys


def integer(request, name, low, high):
    value = request.get(name)
    if type(value) is not int or not low <= value <= high:
        raise ValueError(f"{name} must be an integer in {low}..{high}")
    return value


def text(request, name):
    value = request.get(name)
    if not isinstance(value, str) or not value or "\0" in value:
        raise ValueError(f"{name} must be a nonempty string without NUL")
    return value


def circuit_from_request(request):
    from qiskit import qasm3

    circuit = qasm3.loads(text(request, "qasm"))
    if len(circuit.cregs) != 1 or circuit.cregs[0].name != "c":
        raise ValueError("expected one classical register named c")
    if not any(item.operation.name == "measure" for item in circuit.data):
        raise ValueError("circuit needs an explicit measurement")
    return circuit


def execute(request):
    if not isinstance(request, dict) or type(request.get("protocol")) is not int or request["protocol"] != 1:
        raise ValueError("unsupported protocol; expected 1")
    action = request.get("action")
    if action == "simulate":
        shots = integer(request, "shots", 1, 1_000_000)
        seed = integer(request, "seed", 0, 2_147_483_647)
        from qiskit import transpile
        from qiskit_aer import AerSimulator

        circuit = circuit_from_request(request)
        simulator = AerSimulator()
        compiled = transpile(circuit, simulator, seed_transpiler=seed)
        result = simulator.run(compiled, shots=shots, seed_simulator=seed).result()
        if not result.success:
            raise RuntimeError("Aer simulation failed")
        return {"đếm": dict(result.get_counts()), "số lần": shots, "backend": "qiskit-aer"}

    if action == "submit":
        shots = integer(request, "shots", 1, 1_000_000)
        backend_name = text(request, "backend")
        # Parse and validate before opening an authenticated service.
        circuit = circuit_from_request(request)
        from qiskit.transpiler.preset_passmanagers import generate_preset_pass_manager
        from qiskit_ibm_runtime import QiskitRuntimeService, SamplerV2

        service = QiskitRuntimeService()
        backend = service.backend(backend_name)
        compiled = generate_preset_pass_manager(backend=backend, optimization_level=1).run(circuit)
        job = SamplerV2(mode=backend).run([compiled], shots=shots)
        return {"job_id": job.job_id(), "backend": backend_name, "số lần": shots}

    if action == "result":
        job_id = text(request, "job_id")
        from qiskit_ibm_runtime import QiskitRuntimeService

        job = QiskitRuntimeService().job(job_id)
        status = job.status()
        # RuntimeJobV2 uses strings; retain compatibility with enum statuses.
        status = getattr(status, "name", str(status))
        response = {"job_id": job_id, "status": status, "ready": status == "DONE"}
        if response["ready"]:
            counts = dict(job.result()[0].data.c.get_counts())
            response.update({"đếm": counts, "số lần": sum(counts.values())})
        return response

    raise ValueError("action must be simulate, submit, or result")


def main(argv=None):
    argv = sys.argv[1:] if argv is None else argv
    try:
        if len(argv) != 1 or len(argv[0]) > 24000:
            raise ValueError("expected one JSON request of at most 24000 characters")
        request = json.loads(argv[0])
        # Keep SDK messages away from the machine-readable stdout protocol.
        with contextlib.redirect_stdout(sys.stderr):
            result = execute(request)
        response = {"protocol": 1, "ok": True, "result": result}
    except ImportError:
        response = {"protocol": 1, "ok": False,
                    "error": "missing Qiskit dependency; install phụ thuộc qiskit.txt (and phụ thuộc ibm.txt for QPU)"}
    except Exception as exc:
        response = {"protocol": 1, "ok": False, "error": f"{type(exc).__name__}: {exc}"}
    # ASCII JSON also works when a Windows child inherits a non-UTF-8 locale.
    print(json.dumps(response, ensure_ascii=True, allow_nan=False))
    return 0


if __name__ == "__main__":
    sys.exit(main())
