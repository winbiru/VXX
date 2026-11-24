#!/usr/bin/env python3
"""Verify Ed25519 X.509 CA paths and rejection of invalid server identities."""

import base64
import json
import os
import subprocess
import tempfile
import textwrap
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SUCCESS = "TLS VPP X509 PATH CONSTRAINTS OK"


def openssl(*args):
    subprocess.run(["openssl", *map(str, args)], check=True, capture_output=True, text=True)


def authority(directory, name, limit=0):
    key = directory / f"{name}.key"
    certificate = directory / f"{name}.pem"
    openssl("genpkey", "-algorithm", "ed25519", "-out", key)
    openssl("req", "-new", "-x509", "-key", key, "-days", "3",
            "-subj", f"/CN={name}", "-out", certificate,
            "-addext", f"basicConstraints=critical,CA:TRUE,pathlen:{limit}",
            "-addext", "keyUsage=critical,keyCertSign,cRLSign")
    return key, certificate


def intermediate(directory, unique, subject, parent, limit=0):
    key = directory / f"{unique}.key"
    certificate = directory / f"{unique}.pem"
    csr = directory / f"{unique}.csr"
    extensions = directory / f"{unique}.ext"
    extensions.write_text(
        f"basicConstraints=critical,CA:TRUE,pathlen:{limit}\n"
        "keyUsage=critical,keyCertSign,cRLSign\n"
    )
    openssl("genpkey", "-algorithm", "ed25519", "-out", key)
    openssl("req", "-new", "-key", key, "-subj", f"/CN={subject}", "-out", csr)
    openssl("x509", "-req", "-in", csr, "-CA", parent[1],
            "-CAkey", parent[0], "-CAcreateserial", "-days", "2",
            "-extfile", extensions, "-out", certificate)
    return key, certificate


def leaf(directory, unique, parent):
    key = directory / f"{unique}.key"
    certificate = directory / f"{unique}.pem"
    csr = directory / f"{unique}.csr"
    extensions = directory / f"{unique}.ext"
    extensions.write_text(
        "basicConstraints=critical,CA:FALSE\n"
        "keyUsage=critical,digitalSignature\n"
        "extendedKeyUsage=serverAuth\n"
        "subjectAltName=DNS:localhost\n"
    )
    openssl("genpkey", "-algorithm", "ed25519", "-out", key)
    openssl("req", "-new", "-key", key, "-subj", "/CN=localhost", "-out", csr)
    openssl("x509", "-req", "-in", csr, "-CA", parent[1],
            "-CAkey", parent[0], "-CAcreateserial", "-days", "2",
            "-extfile", extensions, "-out", certificate)
    return certificate


def pem_expression(path):
    return f'decoder.đọc der({trust_anchor_expression(path)})'


def trust_anchor_expression(path):
    return f'decoder.đọc pem({json.dumps(path.read_text())})'


def tamper_signature(directory, certificate):
    der = subprocess.run(
        ["openssl", "x509", "-in", str(certificate), "-outform", "DER"],
        check=True, capture_output=True,
    ).stdout
    # The final octet belongs to the DER-encoded Certificate.signatureValue.
    corrupted = der[:-1] + bytes([der[-1] ^ 1])
    encoded = base64.b64encode(corrupted).decode("ascii")
    output = directory / "invalid-signature.pem"
    output.write_text(
        "-----BEGIN CERTIFICATE-----\n"
        + "\n".join(textwrap.wrap(encoded, 64))
        + "\n-----END CERTIFICATE-----\n"
    )
    return output


def case(label, chain, root, accepted, expected_error="X509: issuer CA constraints rejected",
         hostname="localhost", instant="now"):
    chain_expr = ", ".join(pem_expression(path) for path in chain)
    error = json.dumps(expected_error if not accepted else "")
    return label, f"""    nếu (selected == {json.dumps(label)}) {{
        succeeded = sai;
        failure = "";
        thử {{
            decoder.xác thực chuỗi([{chain_expr}], [{trust_anchor_expression(root)}],
                                    {json.dumps(hostname)}, {instant});
            succeeded = đúng;
        }} bắt lỗi(e) {{ failure = e; }};
        nếu (succeeded != {"đúng" if accepted else "sai"} ||
            (!succeeded && failure != {error})) {{
            in "X.509 failure: {label}";
            in failure;
            trả về 1;
        }};
        in "X.509: {label}";
        trả về 0;
    }};
"""


def verify_case(program, label):
    result = subprocess.run(
        [str(program)], cwd=ROOT, capture_output=True, text=True, timeout=900,
        env=dict(os.environ, VPP_TLS_X509_CASE=label),
    )
    if result.returncode or result.stdout.strip() != f"X.509: {label}":
        raise AssertionError(f"{label} failed ({result.returncode})\n{result.stdout}{result.stderr}")
    return result.stdout.strip()


def main():
    with tempfile.TemporaryDirectory(prefix="vpp_tls_paths_") as temporary:
        directory = Path(temporary)
        root_zero = authority(directory, "RootZero", limit=0)
        direct_leaf = leaf(directory, "DirectLeaf", root_zero)
        intermediate_one = intermediate(directory, "IntermediateOne", "IntermediateOne", root_zero)
        ordinary_leaf = leaf(directory, "OrdinaryLeaf", intermediate_one)
        self_issued = intermediate(directory, "SelfIssued", "RootZero", root_zero)
        self_issued_leaf = leaf(directory, "SelfIssuedLeaf", self_issued)
        root_one = authority(directory, "RootOne", limit=1)
        normal_one = intermediate(directory, "NormalOne", "NormalOne", root_one)
        normal_leaf = leaf(directory, "NormalLeaf", normal_one)
        invalid_signature = tamper_signature(directory, normal_leaf)
        level_one = intermediate(directory, "LevelOne", "LevelOne", root_one, limit=1)
        level_two = intermediate(directory, "LevelTwo", "LevelTwo", level_one)
        deep_leaf = leaf(directory, "DeepLeaf", level_two)

        cases = [
            case("directly trusted root accepts leaf", [direct_leaf], root_zero[1], True),
            case("peer-supplied trusted root accepts leaf",
                 [direct_leaf, root_zero[1]], root_zero[1], True),
            case("one intermediate rejected by root pathLen 0",
                 [ordinary_leaf, intermediate_one[1]], root_zero[1], False),
            case("one intermediate accepted by root pathLen 1",
                 [normal_leaf, normal_one[1]], root_one[1], True),
            case("self-issued intermediate allowed with root pathLen 0",
                 [self_issued_leaf, self_issued[1]], root_zero[1], True),
            case("two intermediates rejected by root pathLen 1",
                 [deep_leaf, level_two[1], level_one[1]], root_one[1], False),
            case("wrong trust anchor rejected",
                 [normal_leaf, normal_one[1]], root_zero[1], False,
                 expected_error="X509: no trusted issuer"),
            case("wrong DNS hostname rejected",
                 [normal_leaf, normal_one[1]], root_one[1], False,
                 expected_error="X509: server identity rejected", hostname="incorrect.test"),
            case("not-yet-valid leaf rejected",
                 [direct_leaf], root_zero[1], False,
                 expected_error="X509: server identity rejected",
                 instant="[2000, 1, 1, 0, 0, 0]"),
            case("expired leaf rejected",
                 [normal_leaf, normal_one[1]], root_one[1], False,
                 expected_error="X509: server identity rejected",
                 instant="[9999, 1, 1, 0, 0, 0]"),
            case("mutated Ed25519 signature rejected",
                 [invalid_signature, normal_one[1]], root_one[1], False,
                 expected_error="X509: issuer signature rejected"),
        ]
        source = (
            "nhập gói.bảo mật.chứng chỉ TLS.Chứng Chỉ TLS VPP;\n"
            "nhập gói.thời gian.giờ quốc tế.thời gian đọc giờ quốc tế ffi;\n"
            "nhập gói.hệ thống.môi trường.đọc biến môi trường;\n"
            "hàm chính() {\n"
            "    decoder = Chứng Chỉ TLS VPP();\n"
            "    now = thời gian đọc giờ quốc tế ffi();\n"
            '    selected = đọc biến môi trường("VPP_TLS_X509_CASE", "");\n'
            + "".join(branch for _, branch in cases)
            + '    ném "Unknown X.509 test case";\n};\n'
        )
        with tempfile.NamedTemporaryFile(
            prefix=".vm_tls_paths_", suffix=".vi", dir=ROOT / "scripts/test/fixtures",
            mode="w", encoding="utf-8", delete=False,
        ) as output:
            output.write(source)
            source_path = Path(output.name)
        program = directory / ("verify.exe" if os.name == "nt" else "verify")
        try:
            subprocess.run(
                [os.environ.get("VPP_TLS_TEST_BINARY", str(ROOT / "VPP")),
                 "build", str(source_path.relative_to(ROOT)), "-o", str(program)],
                cwd=ROOT, check=True, timeout=300,
            )
        finally:
            source_path.unlink()
        # Each certificate path has its own VM arena. The Ed25519 negative
        # cases can then run alongside longer chains without accumulating state.
        failures = []
        with ThreadPoolExecutor(max_workers=4) as pool:
            for result in as_completed([pool.submit(verify_case, program, label) for label, _ in cases]):
                try:
                    print(result.result(), flush=True)
                except (AssertionError, subprocess.TimeoutExpired) as error:
                    failures.append(str(error))
        if failures:
            raise AssertionError("\n".join(failures))
        print(SUCCESS)


if __name__ == "__main__":
    main()
