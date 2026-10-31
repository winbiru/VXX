#!/usr/bin/env node
'use strict';

// Lightweight regression checks without a VS Code installation or npm dependencies.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const manifest = require(path.join(root, 'package.json'));
const vlangManifest = require(path.join(root, 'vpp-language.json'));
const grammar = require(path.join(root, 'syntaxes/vpp.tmLanguage.json'));

assert.equal(manifest.version, vlangManifest.version);
assert.equal(manifest.contributes.grammars[0].scopeName, grammar.scopeName);
assert.ok(grammar.patterns.some((item) => item.include === '#foreignDeclarations'));

// TextMate scopes are assigned differently in declarations and call sites:
// `hàm làm mới(mã nguồn)` -> variable.parameter.vietvm
// `mình.làm mới(mã nguồn)` -> variable.other.vietvm.
// Keep their default colors consistent in both the extension and the theme.
const defaultRules = manifest.contributes.configurationDefaults['editor.tokenColorCustomizations'].textMateRules;
const defaultColor = (scope) => defaultRules.find((rule) =>
  [rule.scope].flat().includes(scope))?.settings?.foreground;
const theme = require(path.join(root, 'themes/vpp-bright-color-theme.json'));
const themeColor = (scope) => theme.tokenColors.find((rule) =>
  [rule.scope].flat().includes(scope))?.settings?.foreground;
assert.equal(defaultColor('variable.parameter.vietvm'), defaultColor('variable.other.vietvm'));
assert.equal(themeColor('variable.parameter.vietvm'), themeColor('variable.other.vietvm'));
assert.equal(defaultColor('variable.parameter.vietvm'), themeColor('variable.parameter.vietvm'));

function capture(rule, text, index) {
  const expression = new RegExp(rule, 'u');
  const match = expression.exec(text);
  assert.ok(match, `Expected match for ${text}`);
  return match[index];
}

const typeRules = grammar.repository;
assert.equal(capture(typeRules.controlKeywords.patterns[0].match, 'khi (x < 3)', 1), 'khi');
assert.equal(new RegExp(typeRules.controlKeywords.patterns[0].match, 'u').test('khich'), false);
assert.equal(
  capture(typeRules.classDefinitions.patterns[0].match,
    'lớp Bộ Điều Khiển Ứng Dụng kế thừa Nền triển khai CóTên {', 3),
  'Bộ Điều Khiển Ứng Dụng'
);
assert.equal(
  capture(typeRules.interfaceDefinitions.patterns[0].match,
    'giao diện Giao Diện Có Tên kế thừa Giao Diện Gốc {', 3),
  'Giao Diện Có Tên'
);
assert.equal(
  capture(typeRules.imports.patterns[0].patterns[0].match,
    'gói.bộ sưu tập.chính.* như sưuTập', 1),
  'gói.bộ sưu tập.chính.*'
);
assert.equal(
  capture(typeRules.imports.patterns[0].patterns[0].match,
    'gói.bộ sưu tập.chính.* như sưuTập', 3),
  'sưuTập'
);
assert.equal(
  capture(typeRules.foreignDeclarations.patterns[0].match,
    'ngoại thư viện c_hệ_thống = "system.c";', 2),
  'c_hệ_thống'
);
assert.equal(
  capture(typeRules.foreignDeclarations.patterns[1].begin,
    'ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c_hệ_thống', 2),
  'getenv'
);
for (const rule of typeRules.foreignDeclarations.patterns[1].patterns) {
  if (rule.match) assert.doesNotThrow(() => new RegExp(rule.match, 'u'));
}

// A member must be recognized before a keyword or built-in with the same name.
const inclusions = grammar.patterns.map((pattern) => pattern.include);
for (const after of ['#controlKeywords', '#receiverKeywords', '#builtins', '#functionCalls', '#identifiers']) {
  assert.ok(inclusions.indexOf('#memberCalls') < inclusions.indexOf(after),
    `Member recognition must precede ${after}`);
}
const [methodRule, propertyRule] = typeRules.memberCalls.patterns;
assert.equal(methodRule.captures['1'].name, 'punctuation.accessor.dot.vietvm');
assert.equal(methodRule.captures['2'].name, 'entity.name.function.member.vietvm');
assert.equal(propertyRule.captures['1'].name, 'punctuation.accessor.dot.vietvm');
assert.equal(propertyRule.captures['2'].name, 'variable.other.property.vietvm');

function memberName(rule, source) {
  const found = new RegExp(rule.match, 'u').exec(source);
  return found ? found[2] : null;
}
for (const [source, expected] of [
  ['mình.kiểm tra kết nối()', 'kiểm tra kết nối'],
  ['ds.danh sách()', 'danh sách'],
  ['ĐốiTượng.đọc các byte(2)', 'đọc các byte'],
  ['lớp_đặc_biệt.kiểm tra sau ()', 'kiểm tra sau'],
  ['obj.độ dài()', 'độ dài'],
  ['ds.thêm(1).danh sách()', 'thêm'],
  ['(tạo()).kiểm tra kết nối()', 'kiểm tra kết nối']
]) {
  assert.equal(memberName(methodRule, source), expected, `Incorrect method scope: ${source}`);
}
for (const [source, expected] of [
  ['mình.danh sách;', 'danh sách'],
  ['a.kiểm tra = đúng;', 'kiểm tra'],
  ['obj.danh sách[0]', 'danh sách'],
  ['obj.thuộc tính nhiều từ == 1', 'thuộc tính nhiều từ'],
  ['ds.thêm(1).danh sách;', 'danh sách'],
  ['(tạo()).kết quả', 'kết quả'],
  ['rows[0].tên đầy đủ,', 'tên đầy đủ']
]) {
  assert.equal(memberName(propertyRule, source), expected, `Incorrect property scope: ${source}`);
}
for (const source of ['mình.danh sách()', 'a.kiểm tra kết nối (1)', '1.25']) {
  assert.equal(memberName(propertyRule, source), null, `Not a property access: ${source}`);
}
assert.equal(memberName(methodRule, '1.25'), null);
assert.deepEqual(
  [...'ds.thêm(1).danh sách()'.matchAll(new RegExp(methodRule.match, 'gu'))].map((match) => match[2]),
  ['thêm', 'danh sách']
);
assert.deepEqual(
  [...'ds.tập hợp.danh sách'.matchAll(new RegExp(propertyRule.match, 'gu'))].map((match) => match[2]),
  ['tập hợp', 'danh sách']
);

class Position {
  constructor(line, character) {
    this.line = line;
    this.character = character;
  }
}
class Range {
  constructor(start, end) {
    this.start = start;
    this.end = end;
  }
}
class DocumentSymbol {
  constructor(name, detail, kind, range, selectionRange) {
    Object.assign(this, { name, detail, kind, range, selectionRange });
  }
}
class SignatureHelp {}
class SignatureInformation {
  constructor(label, documentation) {
    Object.assign(this, { label, documentation });
  }
}
class ParameterInformation {
  constructor(label) { this.label = label; }
}
class CompletionItem {
  constructor(label, kind) { Object.assign(this, { label, kind }); }
}
class SnippetString {
  constructor(value) { this.value = value; }
}

function loadExtension(platform, workspaceRoots, existsSync = fs.existsSync) {
  const vscode = {
    Position, Range, DocumentSymbol, SignatureHelp, SignatureInformation, ParameterInformation,
    CompletionItem, SnippetString,
    CompletionItemKind: { Text: 0, Keyword: 1, Function: 2, Snippet: 3, Variable: 4,
      Method: 5, Class: 6, Interface: 7, Module: 8, Folder: 9 },
    SymbolKind: { Class: 1, Interface: 2, Function: 3, Module: 4, Variable: 5 },
    workspace: {
      workspaceFolders: workspaceRoots.map((fsPath) => ({ uri: { fsPath } })),
      getConfiguration: () => ({ get: () => '' })
    }
  };
  const mockedFs = { ...fs, existsSync, statSync: () => ({ isFile: () => true }) };
  const load = (name) => name === 'vscode' ? vscode : name === 'fs' ? mockedFs : require(name);
  const entrypoint = `${fs.readFileSync(path.join(root, 'extension.js'), 'utf8')}\n` +
    'module.exports = { documentSymbols, importedSourcePaths, languageServerExecutable, renameRequestName, signatureAtPosition, vppCompletionItems };';
  const module = { exports: {} };
  new Function('require', 'module', 'process', entrypoint)(load, module, { ...process, platform });
  return module.exports;
}

function documentAt(filePath, content) {
  const lines = content.split('\n');
  return {
    uri: { fsPath: filePath },
    getText: () => content,
    positionAt(offset) {
      const before = content.slice(0, offset).split('\n');
      return new Position(before.length - 1, before.at(-1).length);
    },
    lineAt(line) {
      const start = new Position(line, 0);
      return { text: lines[line], range: new Range(start, new Position(line, lines[line].length)) };
    },
    offsetAt(position) {
      return lines.slice(0, position.line).reduce((total, line) => total + line.length + 1, 0) +
        position.character;
    }
  };
}

const ext = loadExtension(process.platform, [root]);
assert.equal(ext.renameRequestName('nhân đôi'), '__vpp_rename_probe');
assert.equal(ext.renameRequestName('cộng'), 'cộng');
assert.equal(ext.renameRequestName('cộng('), 'cộng(');
const signatureSource = 'hàm phân tích dữ liệu phức tạp(a, b) { trả về a; }\nhàm chính() {\n  in phân tích dữ liệu phức tạp(1, 2);\n}';
const signatureLine = '  in phân tích dữ liệu phức tạp(1, 2);';
const signature = ext.signatureAtPosition(documentAt(path.join(root, 'examples/signature.vi'), signatureSource),
  new Position(2, signatureLine.indexOf('2') + 1));
assert.ok(signature?.signatures?.[0]?.label.includes('phân tích dữ liệu phức tạp'));
assert.equal(signature.activeParameter, 1);
const source = [
  'công khai nhập gói.mạng.http.chính.*;',
  'ngoại thư viện c_hệ_thống = "system.c";',
  'ngoại hàm getenv(tên: c_chuỗi): c_chuỗi từ c_hệ_thống',
  '    ký hiệu "getenv" abi "c" khả năng "system.env.read";',
  'giao diện Giao Diện Có Tên kế thừa Hợp Đồng {',
  '    hàm nhận dữ liệu();',
  '}',
  'lớp Bộ Điều Khiển Ứng Dụng kế thừa Nền triển khai CóTên {',
  '    hàm công khai xử lý dữ liệu(đầu vào) {',
  '        biến kết quả = đầu vào;',
  '    };',
  '}',
  'hàm chính() {}'
].join('\n');
const symbols = ext.documentSymbols(documentAt(path.join(root, 'examples/sample.vi'), source));
for (const name of [
  'c_hệ_thống', 'getenv', 'Giao Diện Có Tên', 'nhận dữ liệu',
  'Bộ Điều Khiển Ứng Dụng', 'xử lý dữ liệu', 'kết quả', 'chính'
]) {
  assert.ok(symbols.some((symbol) => symbol.name === name), `Missing symbol: ${name}`);
}
assert.ok(!symbols.some((symbol) => symbol.name.includes('kế thừa')));
assert.equal(symbols.find((symbol) => symbol.name === 'getenv').kind, 3);

function complete(text, column, lspItems = []) {
  const doc = documentAt(path.join(root, 'examples/complete.vi'), text);
  return ext.vppCompletionItems(doc, new Position(text.split('\n').length - 1, column), lspItems);
}
function completeOnTrigger(text, triggerCharacter) {
  const doc = documentAt(path.join(root, 'examples/complete.vi'), text);
  return ext.vppCompletionItems(doc, new Position(0, text.length), [], null, triggerCharacter);
}
const findCompletion = (items, label, kind) => items.find((item) =>
  item.label === label && (kind === undefined || item.kind === kind));

const vietnameseKeyword = findCompletion(complete('trả v', 5), 'trả về', 1);
assert.equal(vietnameseKeyword?.range.start.character, 0);
assert.equal(vietnameseKeyword?.range.end.character, 5);
assert.ok(findCompletion(complete('tra v', 5), 'trả về'));
assert.equal(findCompletion(complete('tra v', 5), 'trả về').filterText, 'tra ve');
assert.ok(findCompletion(complete('nếu kh', 6), 'nếu không'));
assert.ok(findCompletion(completeOnTrigger('trả ', ' '), 'trả về'));
assert.deepEqual(completeOnTrigger('  ', ' '), []);
assert.deepEqual(completeOnTrigger('a = ', ' '), []);
assert.deepEqual(completeOnTrigger('a /', '/'), []);
assert.ok(findCompletion(complete('độ d', 4), 'độ dài'));
assert.equal(findCompletion(complete('độ d', 4), 'độ dài').insertText.value, 'độ dài($0)');
assert.equal(findCompletion(complete('độ d()', 4), 'độ dài').insertText, 'độ dài');
assert.ok(findCompletion(complete('nếu', 3), 'nếu', 3).insertText.value.includes('$0'));

const namedFunctions = complete('hàm phân tích dữ liệu(a) { trả về a; }\nphân t', 6);
assert.equal(findCompletion(namedFunctions, 'phân tích dữ liệu').range.start.character, 0);
const merged = complete('phân t', 6, [
  { label: 'phân tích dữ liệu', kind: 3, detail: 'Hàm ngữ nghĩa' },
  { label: 'phân tích tệp', kind: 3 }
]);
assert.equal(merged.filter((item) => item.label === 'phân tích dữ liệu').length, 1);
assert.ok(findCompletion(merged, 'phân tích tệp'));
assert.ok(findCompletion(complete('đốiTượng.', 'đốiTượng.'.length,
  [{ label: 'xử lý', kind: 2 }]), 'xử lý'));
assert.ok(!findCompletion(complete('đốiTượng.', 'đốiTượng.'.length), 'nếu'));
assert.ok(!findCompletion(complete('nhập gói.', 'nhập gói.'.length), 'hàm'));
const moduleDirectory = findCompletion(complete('nhập gói.mạ', 'nhập gói.mạ'.length), 'gói.mạng.');
assert.equal(moduleDirectory?.kind, 9);
assert.equal(moduleDirectory?.range.start.character, 'nhập '.length);
assert.equal(moduleDirectory?.insertText, 'gói.mạng.');
assert.ok(findCompletion(complete('nhập gói.mạng.chí', 'nhập gói.mạng.chí'.length), 'gói.mạng.chính.*'));
assert.ok(findCompletion(complete('nhập gói.lõi.chuyển kiểu.', 'nhập gói.lõi.chuyển kiểu.'.length),
  'gói.lõi.chuyển kiểu.*'));
assert.ok(findCompletion(complete('nhập gói.lõi.chuyển kiểu.thành', 'nhập gói.lõi.chuyển kiểu.thành'.length),
  'gói.lõi.chuyển kiểu.thành chuỗi', 2));
assert.ok(!findCompletion(complete('nhập gói.lõi.chuyển kiểu.', 'nhập gói.lõi.chuyển kiểu.'.length),
  'gói.lõi.chuyển kiểu.chuyển kiểu lỗi số nguyên'));
assert.deepEqual(complete('// trả v', '// trả v'.length), []);
assert.deepEqual(complete('in "trả v', 9), []);
assert.deepEqual(complete('/* chú thích\ntrả v', 5), []);

const imports = ext.importedSourcePaths(documentAt(path.join(root, 'examples/sample.vi'),
  'công khai nhập gói.mạng.http.chính.*;\nnhập gói.bộ sưu tập.chính.* như sưuTập;'));
assert.ok(imports.includes(path.join(root, 'gói/mạng/http/chính.vi')));
assert.ok(imports.includes(path.join(root, 'gói/bộ sưu tập/chính.vi')));

const fixtureRoot = path.join(root, '__mock_windows_workspace__');
const expectedWindows = path.join(fixtureRoot, 'build/bin/Release/vpp-cli.exe');
const winExt = loadExtension('win32', [fixtureRoot], (candidate) =>
  candidate === path.join(fixtureRoot, 'vpp-libraries.json') || candidate === expectedWindows);
assert.equal(winExt.languageServerExecutable({ globalStorageUri: { fsPath: '/tmp/vpp-extension' } }), expectedWindows);
const expectedUnix = path.join(fixtureRoot, 'build/bin/vpp');
const unixExt = loadExtension('darwin', [fixtureRoot], (candidate) =>
  candidate === path.join(fixtureRoot, 'vpp-libraries.json') || candidate === expectedUnix);
assert.equal(unixExt.languageServerExecutable({ globalStorageUri: { fsPath: '/tmp/vpp-extension' } }), expectedUnix);

console.log('V++ extension regressions: PASS');
