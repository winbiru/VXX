#!/usr/bin/env python3
"""Select production i32 V++ functions for a raw JIT boundary probe.

The bootstrap adds runtime helpers implicitly. The JIT accepts an explicit
translation unit instead: retain the original function text and its transitive
dependencies, without pulling the compiler's unrelated process/linker driver.
"""
import argparse
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
VM_PARTS = ("bộ nhớ", "giá trị", "máy hệ thống", "thư mục máy", "tiến trình máy",
            "ổ cắm máy", "phân giải máy", "host ABI", "truyền worker", "worker", "nạp chương trình", "runtime",
            "thực thi", "số thực tự chủ")
LEXICAL = re.compile(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.S)
DECLARATION = re.compile(r'(?m)^hàm\s+(?:(?:công khai|riêng tư)\s+)?([^(){};\n]+)\(([^()]*)\)\s*\{')


def mask(text):
    # Preserve offsets and lines so slicing always returns the production text.
    return LEXICAL.sub(lambda m: re.sub(r'[^\n]', ' ', m[0]), text)


def functions(text, origin):
    code = mask(text)
    for match in DECLARATION.finditer(code):
        depth, end = 1, match.end()
        while depth and end < len(code):
            depth += (code[end] == '{') - (code[end] == '}')
            end += 1
        if depth:
            raise ValueError(f"unterminated function in {origin}: {match[1]}")
        name = match[1].strip()
        yield name, (text[match.start():end] + ';\n', code[match.end():end], match[2])


def prepare(source, destination, *, vm_core=False):
    source, destination = Path(source), Path(destination)
    paths = [ROOT / 'gói/biên dịch/bản địa/môi trường thực thi.vi',
             ROOT / 'gói/biên dịch/bản địa/trình liên kết elf.vi']
    if vm_core:
        paths += [ROOT / 'gói/máy ảo' / (part + '.vi') for part in VM_PARTS]
    paths.append(source)
    definitions = {}
    for path in paths:
        for name, definition in functions(path.read_text(encoding='utf-8'), path):
            if name in definitions:
                raise ValueError(f"duplicate raw function {name}: {path}")
            definitions[name] = definition
    if 'chính' not in definitions:
        raise ValueError(f"raw JIT probe has no entrypoint: {source}")
    calls = {name: re.compile(r'(?<!\w)' + re.escape(name).replace(r'\ ', r'[ \t]+') + r'\s*\(')
             for name in definitions}
    selected, pending = set(), ['chính']
    while pending:
        name = pending.pop()
        if name in selected:
            continue
        selected.add(name)
        _, body, _ = definitions[name]
        pending.extend(other for other, call in calls.items()
                       if other not in selected and call.search(body))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text('// Production V++ functions, selected for the raw JIT probe.\n' +
                           '\n'.join(definition[0] for name, definition in definitions.items()
                                     if name in selected), encoding='utf-8')
    return destination


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vm-core', action='store_true')
    parser.add_argument('source', type=Path)
    parser.add_argument('destination', type=Path)
    args = parser.parse_args()
    prepare(args.source, args.destination, vm_core=args.vm_core)


if __name__ == '__main__':
    main()
