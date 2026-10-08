#!/usr/bin/env python3
"""Inventory V++ bodies and their direct VM primitive calls, ignoring comments."""

import argparse
import json
from pathlib import Path
import re
import unicodedata


ROOT = Path(__file__).resolve().parents[2]
LEXEMES = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'')
DECLARATION = re.compile(r'\bhàm\s+([\w ]+?)\s*\(')
INTERFACE = re.compile(r'\bgiao\s+diện\b[^{}]*\{')
CONSTANT_RETURN = re.compile(
    r'\s*trả\s+về\s+(?:rỗng|đúng|sai|-?\d+(?:\.\d+)?|"(?:\\.|[^"\\])*")\s*;\s*')


def mask(source):
    return LEXEMES.sub(
        lambda match: ''.join('\n' if c == '\n' else ' ' for c in match[0]), source)


def close_pair(source, start, opening, closing):
    depth = 1
    for index in range(start + 1, len(source)):
        depth += (source[index] == opening) - (source[index] == closing)
        if depth == 0:
            return index
    raise ValueError(f'unclosed {opening} at offset {start}')


def interface_ranges(masked):
    ranges = []
    for declaration in INTERFACE.finditer(masked):
        start = declaration.end() - 1
        ranges.append((start, close_pair(masked, start, '{', '}')))
    return ranges


def declarations(source):
    masked = mask(source)
    interfaces = interface_ranges(masked)
    for declaration in DECLARATION.finditer(masked):
        end_parameters = close_pair(masked, declaration.end() - 1, '(', ')')
        body_start = end_parameters + 1
        while body_start < len(masked) and masked[body_start].isspace():
            body_start += 1
        line = source.count('\n', 0, declaration.start()) + 1
        if body_start == len(masked) or masked[body_start] != '{':
            in_interface = any(start < declaration.start() < end
                               for start, end in interfaces)
            yield (declaration[1].strip(), line, None, None,
                   'interface_signature' if in_interface else 'missing_body')
            continue
        body_end = close_pair(masked, body_start, '{', '}')
        yield (declaration[1].strip(), line, source[body_start + 1:body_end],
               masked[body_start + 1:body_end], 'body')


def functions(source):
    for name, line, body, masked_body, kind in declarations(source):
        if kind == 'body':
            yield name, line, body, masked_body


def category(name):
    if name.startswith(('socket_', 'dns_')):
        return 'socket'
    if name.startswith('io_'):
        return 'file-io'
    if name == 'ngau_nhien_bao_mat_bytes_vm':
        return 'crypto-primitive'
    if name.startswith(('dong_ho_', 'thoi_gian_', 'ngu_')):
        return 'clock'
    if name.startswith(('task_vm_', 'thread_vm_')):
        return 'concurrency'
    if name.startswith(('bien_dich_', 'kich_ban_')):
        return 'compiler-vm'
    if name == 'tien_trinh_chay_vm':
        return 'syscall'
    if name.startswith(('duong_dan_', 'la_tep_', 'la_thu_muc_', 'tao_thu_muc_',
                        'liet_ke_thu_muc_', 'xoa_duong_dan_', 'doc_bien_', 'ten_nen_')):
        return 'syscall'
    if name in {'bam_dinh_danh', 'bộ', 'co_khoa', 'do_dai', 'khoa_map',
                'chuoi_bytes_vm', 'chuoi_tu_bytes_vm', 'loai_cua',
                'so_thuc_bits_vm', 'so_thuc_tu_bits_vm', 'them',
                'xoa_khoa', 'xoa_tai'}:
        return 'compiler-vm'
    return 'unclassified'


def audit(root):
    # These calls lower to opcodes; they are not resolved through a name-based
    # native dispatcher. `intrinsic.h` is the single registry used by codegen
    # and the VM, so the audit must read that registry instead of scraping old
    # string comparisons from codegen.cpp.
    registry = (root / 'src/include/vpp/bytecode/intrinsic.h').read_text(encoding='utf-8')
    sources = {path: path.read_text(encoding='utf-8')
               for path in sorted((root / 'gói').rglob('*.vi'))}
    declared = {name for source in sources.values() for name, _, _, _ in functions(source)}
    # Vietnamese aliases such as "độ dài"/"thêm" have actual V++ bodies.
    # Their ASCII counterparts are the unambiguous calls into VM primitives.
    intrinsics = sorted(
        set(re.findall(r'\{\s*"([^"]+)"\s*,\s*OP_VM_[A-Z0-9_]+\s*,\s*\d+(?:\s*,\s*(?:true|false))?\s*\}', registry))
        - declared)
    calls = {name: re.compile(r'(?<!\w)' + re.escape(name) + r'\s*\(')
             for name in intrinsics}
    modules, implementations, native_callers = [], [], []
    placeholders, empty_bodies = [], []
    missing_bodies, interface_signatures, constant_bodies, errors = [], [], [], []
    counts = {'library_bodies': 0, 'direct_native_wrappers': 0,
              'bodies_using_primitives': 0, 'error_helpers': 0,
              'constant_bodies': 0, 'empty_bodies': 0,
              'unavailable_native_hooks': 0}
    for path, source in sources.items():
        module_calls = set()
        function_count = 0
        for name, line, body, _, kind in declarations(source):
            item = {'file': path.relative_to(root).as_posix(),
                    'line': line, 'function': name}
            if kind == 'missing_body':
                missing_bodies.append(item)
            elif kind == 'interface_signature':
                interface_signatures.append(item)
        for name, line, body, masked_body in functions(source):
            function_count += 1
            native = sorted(n for n, pattern in calls.items() if pattern.search(masked_body))
            module_calls.update(native)
            kind = 'library_bodies'
            if not masked_body.strip():
                empty_bodies.append({'file': path.relative_to(root).as_posix(),
                                     'line': line, 'function': name})
                kind = 'empty_bodies'
            elif CONSTANT_RETURN.fullmatch(body):
                kind = 'constant_bodies'
                constant_bodies.append({'file': path.relative_to(root).as_posix(),
                                        'line': line, 'function': name,
                                        'body': body.strip()})
            elif native:
                kind = 'bodies_using_primitives'
                return_call = re.fullmatch(r'\s*trả về\s+([\w ]+)\s*\([^;]*\)\s*;\s*', masked_body)
                if return_call and return_call[1].strip() in native:
                    kind = 'direct_native_wrappers'
                native_callers.append({'file': path.relative_to(root).as_posix(),
                                       'line': line, 'function': name, 'kind': kind,
                                       'calls': native})
            elif re.fullmatch(r'\s*ném\s+[^;]*;\s*', masked_body):
                kind = 'error_helpers'
            unavailable = False
            for token in LEXEMES.finditer(body):
                if token[0].startswith(('"', "'")):
                    plain = ''.join(c for c in unicodedata.normalize('NFD', token[0].lower())
                                    if not unicodedata.combining(c))
                    if 'native hook' in plain and 'khong kha dung' in plain:
                        unavailable = True
                        placeholders.append({'file': path.relative_to(root).as_posix(),
                                             'line': line, 'function': name, 'message': token[0]})
            if unavailable:
                kind = 'unavailable_native_hooks'
            counts[kind] += 1
            implementations.append({
                'file': path.relative_to(root).as_posix(),
                'line': line,
                'function': name,
                'kind': kind,
                'calls': native,
            })
            for called in re.findall(r'\b(vm_\w+|\w+_vm(?:_\w+)?)\s*\(', masked_body):
                if called not in calls:
                    errors.append(f'{path.relative_to(root)}:{line}: unknown VM call {called}')
        modules.append({'file': path.relative_to(root).as_posix(), 'functions': function_count,
                        'direct_primitive_calls': sorted(module_calls)})
    boundaries = {}
    for intrinsic in intrinsics:
        boundary = category(intrinsic)
        summary = boundaries.setdefault(boundary, {'primitives': 0, 'callers': 0})
        summary['primitives'] += 1
    for caller in native_callers:
        for boundary in {category(name) for name in caller['calls']}:
            boundaries[boundary]['callers'] += 1
    unclassified = [name for name in intrinsics if category(name) == 'unclassified']
    errors.extend(f'unclassified VM primitive {name}' for name in unclassified)
    # Every registered primitive must be present through bytecode and VM dispatch.
    entries = re.findall(r'\{\s*"([^"]+)"\s*,\s*(OP_VM_[A-Z0-9_]+)\s*,\s*(\d+)', registry)
    instruction = (root / 'src/include/vm/instruction.h').read_text(encoding='utf-8')
    opcode_source = (root / 'src/bytecode/opcode.cpp').read_text(encoding='utf-8')
    runtime = (root / 'src/runtime/vm.cpp').read_text(encoding='utf-8')
    seen_names, seen_opcodes = set(), set()
    for name, opcode, arity in entries:
        if name in seen_names or opcode in seen_opcodes:
            errors.append(f'duplicate primitive registry entry: {name} / {opcode}')
        seen_names.add(name)
        seen_opcodes.add(opcode)
        if not re.search(r'\b' + opcode + r'\s*=', instruction):
            errors.append(f'{name}: missing instruction {opcode}')
        if opcode_source.count('case ' + opcode + ':') != 2:
            errors.append(f'{name}: missing opcode name/verifier registration')
        if 'case ' + opcode + ':' not in runtime:
            errors.append(f'{name}: missing VM dispatch {opcode}')

    incomplete = missing_bodies + empty_bodies + placeholders
    return {'schema': 2, 'files': len(modules), 'functions': sum(counts.values()),
            'classification': counts, 'unavailable_native_hooks': placeholders,
            'missing_bodies': missing_bodies, 'empty_bodies': empty_bodies,
            'interface_signatures': interface_signatures,
            'constant_bodies': constant_bodies,
            'incomplete_implementations': incomplete,
            'boundary_summary': boundaries,
            'intrinsics': [{'name': n, 'boundary': category(n)} for n in intrinsics],
            'implementations': implementations,
            'native_callers': native_callers, 'modules': modules, 'errors': errors}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, help='write the full JSON inventory')
    args = parser.parse_args()
    report = audit(ROOT)
    if args.output:
        args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(f"{report['files']} files, {report['functions']} V++ bodies; "
          f"{len(report['incomplete_implementations'])} incomplete implementations")
    print(json.dumps(report['classification'], ensure_ascii=False, sort_keys=True))
    print(json.dumps(report['boundary_summary'], ensure_ascii=False, sort_keys=True))
    for error in report['errors']:
        print(error)
    return bool(report['incomplete_implementations'] or report['errors'])


if __name__ == '__main__':
    raise SystemExit(main())
