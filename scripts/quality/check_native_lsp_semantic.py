#!/usr/bin/env python3
"""Exercise native semantic LSP bindings and UTF-16 edits over JSON-RPC."""
import argparse
import os
from pathlib import Path
import subprocess
import tempfile

from check_native_lsp_catalog import packet, decode


def pos(source, line, needle, occurrence=0):
    text = source.splitlines()[line]
    start = -1
    for _ in range(occurrence + 1):
        start = text.index(needle, start + 1)
    return {"line": line, "character": len(text[:start].encode("utf-16-le")) // 2}


def span(source, line, needle, occurrence=0):
    start = pos(source, line, needle, occurrence)
    return {"start": start, "end": {**start, "character": start["character"] + len(needle.encode("utf-16-le")) // 2}}


def check(tools, compiler, root):
    source = """biến số = 1;
hàm riêng tư tính tổng(a) {
    biến số = a;
    số += 2;
    in "🌿"; trả về số;
};
hàm khác(b) { trả về b; };
hàm chính() {
    in tính tổng(số);
    gọi tính tổng(số);
};
"""
    with tempfile.TemporaryDirectory(prefix="vpp-lsp-semantic-") as work:
        uri = (Path(work) / "mã 🌿.vi").as_uri()
        doc = {"uri": uri}
        wire = [packet("initialize", 1), packet("textDocument/didOpen", params={
            "textDocument": {**doc, "languageId": "vpp", "version": 1, "text": source}})]

        def request(method, ident, line, needle, occurrence=0, **params):
            wire.append(packet("textDocument/" + method, ident, {
                "textDocument": doc, "position": pos(source, line, needle, occurrence), **params}))

        request("definition", 2, 4, "số")
        request("definition", 3, 8, "số")
        request("definition", 4, 8, "tính tổng")
        request("hover", 5, 8, "tính tổng")
        request("completion", 6, 4, "số")
        request("rename", 7, 4, "số", newName="giá trị mới")
        request("rename", 8, 8, "tính tổng", newName="tính kết quả")
        request("rename", 9, 4, "số", newName="a")
        request("rename", 10, 4, "số", newName="x; in 2")
        changed = source.replace("biến số = a;", "biến khác biệt = a;").replace("số += 2;", "khác biệt += 2;").replace("trả về số;", "trả về khác biệt;")
        wire.append(packet("textDocument/didChange", params={"textDocument": {**doc, "version": 2}, "contentChanges": [{"text": changed}]}))
        wire.append(packet("textDocument/definition", 11, {"textDocument": doc, "position": pos(changed, 4, "khác biệt")}))
        # A missing expression makes the current buffer syntactically invalid.
        incomplete = changed.replace("khác biệt += 2;", "khác biệt += ;")
        wire.append(packet("textDocument/didChange", params={"textDocument": {**doc, "version": 3}, "contentChanges": [{"text": incomplete}]}))
        for method, ident in [("completion", 14), ("definition", 15), ("hover", 16), ("rename", 17)]:
            extra = {"newName": "tên mới"} if method == "rename" else {}
            wire.append(packet("textDocument/" + method, ident, {"textDocument": doc,
                "position": pos(incomplete, 4, "khác biệt"), **extra}))
        wire.append(packet("textDocument/didChange", params={"textDocument": {**doc, "version": 4}, "contentChanges": [{"text": changed}]}))
        wire.append(packet("textDocument/completion", 18, {"textDocument": doc, "position": pos(changed, 4, "khác biệt")}))
        wire.append(packet("textDocument/definition", 19, {"textDocument": doc, "position": pos(changed, 4, "khác biệt")}))
        # The middle of a UTF-16 surrogate pair is not a symbol position.
        emoji = pos(changed, 4, "🌿")
        wire.append(packet("textDocument/definition", 20, {"textDocument": doc,
            "position": {**emoji, "character": emoji["character"] + 1}}))
        capture = changed.replace('in "🌿";', 'in "🌿"; in chưa biết;')
        wire.append(packet("textDocument/didChange", params={"textDocument": {**doc, "version": 5}, "contentChanges": [{"text": capture}]}))
        wire.append(packet("textDocument/definition", 21, {"textDocument": doc, "position": pos(capture, 4, "khác biệt")}))
        wire.append(packet("textDocument/definition", 22, {"textDocument": doc, "position": pos(capture, 4, "chưa biết")}))
        wire.append(packet("textDocument/rename", 23, {"textDocument": doc, "position": pos(capture, 4, "khác biệt"), "newName": "chưa biết"}))
        crlf = changed.replace("\n", "\r\n")
        wire.append(packet("textDocument/didChange", params={"textDocument": {**doc, "version": 6}, "contentChanges": [{"text": crlf}]}))
        wire.append(packet("textDocument/definition", 24, {"textDocument": doc, "position": pos(crlf, 4, "khác biệt")}))
        wire.append(packet("textDocument/didClose", params={"textDocument": doc}))
        request("definition", 12, 4, "số")
        wire.extend([packet("shutdown", 13), packet("exit")])
        result = subprocess.run([str(tools), str(compiler), "--lsp"], input=b"".join(wire), capture_output=True,
                                env=dict(os.environ, VPP_HOME=str(root)), timeout=180)
        assert result.returncode == 0, result.stderr.decode("utf-8", "replace")
        messages = decode(result.stdout)
        replies = {m["id"]: m for m in messages if "id" in m}
        caps = replies[1]["result"]["capabilities"]
        assert caps["definitionProvider"] is True and caps["hoverProvider"] is True and caps["renameProvider"] is True
        assert "completionProvider" in caps
        for ident, line, name in [(2, 2, "số"), (3, 0, "số"), (4, 1, "tính tổng")]:
            assert replies[ident].get("result") == {"uri": uri, "range": span(source, line, name)}, replies[ident]
        assert "tính tổng(a)" in replies[5]["result"]["contents"]["value"], replies[5]
        labels = {x["label"] for x in replies[6]["result"]["items"]}
        assert {"số", "a", "tính tổng", "chính"} <= labels and "b" not in labels, replies[6]
        for ident, name, replacement, lines in [(7, "số", "giá trị mới", [2, 3, 4]),
                                                (8, "tính tổng", "tính kết quả", [1, 8, 9])]:
            edits = replies[ident]["result"]["changes"][uri]
            assert all(e["newText"] == replacement for e in edits), edits
            assert sorted((e["range"]["start"]["line"], e["range"]["start"]["character"]) for e in edits) == sorted(
                (line, pos(source, line, name)["character"]) for line in lines), edits
            assert all(e["range"] == span(source, e["range"]["start"]["line"], name) for e in edits), edits
        assert replies[9]["error"]["code"] == -32602, replies[9]
        assert replies[10]["error"]["code"] == -32602, replies[10]
        assert replies[11]["result"] == {"uri": uri, "range": span(changed, 2, "khác biệt")}, replies[11]
        fallback = replies[14]["result"]
        assert fallback["isIncomplete"] is True, replies[14]
        assert {"khác biệt", "a", "tính tổng"} <= {item["label"] for item in fallback["items"]}, replies[14]
        for ident in (15, 16, 17, 20):
            assert replies[ident].get("result") is None, replies[ident]
        assert replies[18]["result"]["isIncomplete"] is False, replies[18]
        assert replies[19]["result"] == replies[11]["result"], replies[19]
        assert replies[21]["result"] == replies[11]["result"], replies[21]
        assert replies[22]["result"] is None, replies[22]
        assert replies[23]["error"]["code"] == -32602, replies[23]
        assert replies[24]["result"] == replies[11]["result"], replies[24]
        assert replies[12]["result"] is None
        assert replies[13]["result"] is None
    print("Native semantic LSP: completion, definition, hover, rename, shadowing, UTF-16, CRLF, incomplete buffers, binding preservation and invalidation PASS")

def check_imports(tools, compiler, root):
    with tempfile.TemporaryDirectory(prefix="vpp-lsp-imports-") as work:
        work = Path(work)
        library = work / "phụ trợ.vi"
        bridge = work / "cầu nối.vi"
        lib_source = "hàm riêng tư bí mật() { trả về 0; };\nhàm tính điểm(a, b = 1) { trả về a + b; };\n"
        library.write_text(lib_source, encoding="utf-8")
        bridge.write_text("công khai nhập phụ trợ.tính điểm;\n", encoding="utf-8")
        source = "nhập cầu nối.tính điểm như điểm;\nnhập phụ trợ.* như p;\nhàm chính() { in điểm(1); in p.tính điểm(2); };\n"
        uri = (work / "chính.vi").as_uri()
        doc = {"uri": uri}
        wire = [packet("initialize", 1), packet("textDocument/didOpen", params={
            "textDocument": {**doc, "languageId": "vpp", "version": 1, "text": source}})]
        def request(method, ident, needle, occurrence=0, **params):
            wire.append(packet("textDocument/" + method, ident, {
                "textDocument": doc, "position": pos(source, 2, needle, occurrence), **params}))
        request("definition", 2, "điểm")
        request("definition", 3, "p.tính điểm")
        request("hover", 4, "điểm")
        request("completion", 5, "điểm")
        request("rename", 6, "điểm", newName="điểm mới")
        lib_doc = {"uri": library.as_uri()}
        updated = "\n" + lib_source
        wire.append(packet("textDocument/didOpen", params={"textDocument": {
            **lib_doc, "languageId": "vpp", "version": 1, "text": updated}}))
        request("definition", 7, "điểm")
        wire.append(packet("textDocument/didChange", params={"textDocument": {**lib_doc, "version": 2}, "contentChanges": [{"text": ""}]}))
        request("definition", 10, "điểm")
        request("completion", 11, "điểm")
        wire.append(packet("textDocument/didClose", params={"textDocument": lib_doc}))
        request("definition", 8, "điểm")
        wire.append(packet("workspace/didChangeWatchedFiles", params={"changes": [{"uri": library.as_uri(), "type": 2}]}))
        request("definition", 12, "điểm")
        wire.extend([packet("shutdown", 9), packet("exit")])
        result = subprocess.run([str(tools), str(compiler), "--lsp"], input=b"".join(wire), capture_output=True,
                                env=dict(os.environ, VPP_HOME=str(root)), timeout=180)
        assert result.returncode == 0, result.stderr.decode("utf-8", "replace")
        replies = {m["id"]: m for m in decode(result.stdout) if "id" in m}
        location = {"uri": library.as_uri(), "range": span(lib_source, 1, "tính điểm")}
        for ident in (2, 3, 8):
            assert replies[ident].get("result") == location, replies[ident]
        assert "tính điểm(a, b = …)" in replies[4]["result"]["contents"]["value"], replies[4]
        labels = {x["label"] for x in replies[5]["result"]["items"]}
        assert {"điểm", "p.tính điểm"} <= labels and "p.bí mật" not in labels, replies[5]
        assert replies[6]["error"]["code"] == -32602, replies[6]
        assert replies[7]["result"] == {"uri": library.as_uri(), "range": span(updated, 2, "tính điểm")}, replies[7]
        assert replies[10]["result"] is None, replies[10]
        assert not {"điểm", "p.tính điểm"} & {x["label"] for x in replies[11]["result"]["items"]}, replies[11]
        assert replies[12]["result"] == location, replies[12]
    print("Native semantic LSP: imports, aliases, reexports, visibility, empty open buffers and watched-file notifications PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    for name in ("tools", "compiler", "root"):
        parser.add_argument(name, type=Path)
    args = parser.parse_args()
    check(args.tools.resolve(), args.compiler.resolve(), args.root.resolve())
    check_imports(args.tools.resolve(), args.compiler.resolve(), args.root.resolve())
