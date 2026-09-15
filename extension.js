const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const { spawn } = require('child_process');

const RELEASES_URL = 'https://github.com/winbiru/VXX/releases';
const LATEST_DOWNLOAD_URL = `${RELEASES_URL}/latest/download`;
const VPP_LANGUAGE_SELECTOR = { language: 'vpp' };

let languageServer = null;
let languageServerStarting = null;
let languageServerOutput = null;

function lspPosition(position) {
  return { line: position.line, character: position.character };
}

function lspRange(range) {
  return {
    start: lspPosition(range.start),
    end: lspPosition(range.end)
  };
}

function vscodeRange(range) {
  return new vscode.Range(
    new vscode.Position(range.start.line, range.start.character),
    new vscode.Position(range.end.line, range.end.character)
  );
}

function completionKind(kind) {
  switch (kind) {
    case 2: return vscode.CompletionItemKind.Method;
    case 3: return vscode.CompletionItemKind.Function;
    case 6: return vscode.CompletionItemKind.Variable;
    case 7: return vscode.CompletionItemKind.Class;
    case 8: return vscode.CompletionItemKind.Interface;
    case 9: return vscode.CompletionItemKind.Module;
    default: return vscode.CompletionItemKind.Text;
  }
}

function diagnosticSeverity(severity) {
  switch (severity) {
    case 1: return vscode.DiagnosticSeverity.Error;
    case 2: return vscode.DiagnosticSeverity.Warning;
    case 3: return vscode.DiagnosticSeverity.Information;
    case 4: return vscode.DiagnosticSeverity.Hint;
    default: return vscode.DiagnosticSeverity.Error;
  }
}

class VppLanguageServer {
  constructor(executable, output) {
    this.executable = executable;
    this.output = output;
    this.child = null;
    this.buffer = Buffer.alloc(0);
    this.nextId = 1;
    this.pending = new Map();
    this.diagnostics = vscode.languages.createDiagnosticCollection('vpp');
  }

  async start() {
    this.output.appendLine(`Khởi động: ${this.executable} --lsp`);
    this.child = spawn(this.executable, ['--lsp'], { stdio: ['pipe', 'pipe', 'pipe'] });

    await new Promise((resolve, reject) => {
      const onSpawn = () => {
        this.child.removeListener('error', onError);
        resolve();
      };
      const onError = (error) => {
        this.child.removeListener('spawn', onSpawn);
        reject(error);
      };
      this.child.once('spawn', onSpawn);
      this.child.once('error', onError);
    });

    this.child.stdout.on('data', (chunk) => this.handleData(chunk));
    this.child.stderr.on('data', (chunk) => this.output.append(chunk.toString()));
    this.child.stdin.on('error', (error) => this.output.appendLine(`stdin LSP: ${error.message}`));
    this.child.on('error', (error) => this.output.appendLine(`LSP lỗi: ${error.message}`));
    this.child.on('close', (code) => {
      this.output.appendLine(`LSP đã dừng với mã ${code}.`);
      this.rejectPending(new Error(`Máy chủ ngôn ngữ V++ đã dừng với mã ${code}.`));
      this.child = null;
    });

    await this.request('initialize', {
      processId: process.pid,
      rootUri: vscode.workspace.workspaceFolders?.[0]?.uri.toString() || null,
      capabilities: {
        textDocument: {
          completion: {},
          definition: {},
          hover: {},
          rename: {},
          formatting: {},
          publishDiagnostics: {}
        }
      }
    });
    this.notify('initialized', {});
  }

  dispose() {
    this.diagnostics.dispose();
    if (this.child) {
      this.child.kill();
      this.child = null;
    }
    this.rejectPending(new Error('Máy chủ ngôn ngữ V++ đã được đóng.'));
  }

  async stop() {
    if (!this.child) {
      this.diagnostics.clear();
      return;
    }

    try {
      await this.request('shutdown', null);
      this.notify('exit', null);
      this.child.stdin.end();
    } catch (error) {
      this.output.appendLine(`Không thể shutdown LSP sạch: ${error.message}`);
      this.child.kill();
    } finally {
      this.child = null;
      this.diagnostics.clear();
    }
  }

  rejectPending(error) {
    for (const entry of this.pending.values()) {
      clearTimeout(entry.timer);
      entry.reject(error);
    }
    this.pending.clear();
  }

  send(payload) {
    if (!this.child || !this.child.stdin.writable) {
      throw new Error('Máy chủ ngôn ngữ V++ chưa chạy.');
    }
    const body = JSON.stringify(payload);
    const header = `Content-Length: ${Buffer.byteLength(body, 'utf8')}\r\n\r\n`;
    this.child.stdin.write(header, 'ascii');
    this.child.stdin.write(body, 'utf8');
  }

  notify(method, params) {
    this.send({ jsonrpc: '2.0', method, params });
  }

  request(method, params) {
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new Error(`LSP hết thời gian chờ phản hồi cho ${method}.`));
      }, 10_000);
      this.pending.set(id, { resolve, reject, timer });
      try {
        this.send({ jsonrpc: '2.0', id, method, params });
      } catch (error) {
        clearTimeout(timer);
        this.pending.delete(id);
        reject(error);
      }
    });
  }

  handleData(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (true) {
      const headerEnd = this.buffer.indexOf('\r\n\r\n');
      if (headerEnd < 0) return;

      const header = this.buffer.subarray(0, headerEnd).toString('ascii');
      const match = /(?:^|\r\n)Content-Length:\s*(\d+)/i.exec(header);
      if (!match) {
        this.output.appendLine('LSP trả header không có Content-Length.');
        this.buffer = Buffer.alloc(0);
        return;
      }

      const bodyLength = Number(match[1]);
      const bodyStart = headerEnd + 4;
      const messageEnd = bodyStart + bodyLength;
      if (this.buffer.length < messageEnd) return;

      const body = this.buffer.subarray(bodyStart, messageEnd).toString('utf8');
      this.buffer = this.buffer.subarray(messageEnd);
      try {
        this.handleMessage(JSON.parse(body));
      } catch (error) {
        this.output.appendLine(`Không thể đọc phản hồi LSP: ${error.message}`);
      }
    }
  }

  handleMessage(message) {
    if (Object.prototype.hasOwnProperty.call(message, 'id')) {
      const entry = this.pending.get(message.id);
      if (!entry) return;
      clearTimeout(entry.timer);
      this.pending.delete(message.id);
      if (message.error) {
        entry.reject(new Error(message.error.message || 'LSP trả lỗi không xác định.'));
      } else {
        entry.resolve(message.result);
      }
      return;
    }

    if (message.method === 'textDocument/publishDiagnostics') {
      const uri = vscode.Uri.parse(message.params.uri);
      const diagnostics = (message.params.diagnostics || []).map((item) => {
        const diagnostic = new vscode.Diagnostic(
          vscodeRange(item.range),
          item.message,
          diagnosticSeverity(item.severity)
        );
        diagnostic.source = item.source || 'vpp';
        return diagnostic;
      });
      this.diagnostics.set(uri, diagnostics);
    }
  }
}

function languageServerExecutable(context) {
  const configured = vscode.workspace.getConfiguration('vpp').get('lsp.executable', '').trim();
  if (configured) return configured;
  const managed = vmBinary(context);
  if (fs.existsSync(managed)) return managed;
  return process.platform === 'win32' ? 'vpp.exe' : 'vpp';
}

function documentParams(document) {
  return { uri: document.uri.toString() };
}

async function startLanguageServer(context, showError = false) {
  if (!vscode.workspace.getConfiguration('vpp').get('lsp.enabled', true)) return;
  if (languageServer) return;
  if (languageServerStarting) return languageServerStarting;

  languageServerStarting = (async () => {
    const server = new VppLanguageServer(languageServerExecutable(context), languageServerOutput);
    try {
      await server.start();
      languageServer = server;
      context.subscriptions.push(server.diagnostics);

      for (const document of vscode.workspace.textDocuments) {
        if (document.languageId === 'vpp') {
          server.notify('textDocument/didOpen', {
            textDocument: {
              uri: document.uri.toString(),
              languageId: 'vpp',
              version: document.version,
              text: document.getText()
            }
          });
        }
      }
    } catch (error) {
      server.dispose();
      languageServer = null;
      languageServerOutput.appendLine(`Không thể khởi động LSP: ${error.message}`);
      if (showError) {
        vscode.window.showErrorMessage(`Không thể khởi động máy chủ ngôn ngữ V++: ${error.message}`);
      }
    } finally {
      languageServerStarting = null;
    }
  })();
  return languageServerStarting;
}

async function restartLanguageServer(context, showMessage = false) {
  if (languageServerStarting) {
    await languageServerStarting;
  }
  if (languageServer) {
    const current = languageServer;
    languageServer = null;
    await current.stop();
    current.dispose();
  }
  await startLanguageServer(context, true);
  if (showMessage && languageServer) {
    vscode.window.showInformationMessage('Đã khởi động lại máy chủ ngôn ngữ V++.');
  }
}

function registerLanguageFeatures(context) {
  context.subscriptions.push(
    vscode.workspace.onDidOpenTextDocument((document) => {
      if (document.languageId !== 'vpp') return;
      if (!languageServer) {
        void startLanguageServer(context);
        return;
      }
      languageServer.notify('textDocument/didOpen', {
        textDocument: {
          uri: document.uri.toString(),
          languageId: 'vpp',
          version: document.version,
          text: document.getText()
        }
      });
    }),
    vscode.workspace.onDidChangeTextDocument((event) => {
      if (event.document.languageId !== 'vpp' || !languageServer) return;
      languageServer.notify('textDocument/didChange', {
        textDocument: {
          uri: event.document.uri.toString(),
          version: event.document.version
        },
        contentChanges: [{ text: event.document.getText() }]
      });
    }),
    vscode.workspace.onDidCloseTextDocument((document) => {
      if (document.languageId !== 'vpp' || !languageServer) return;
      languageServer.notify('textDocument/didClose', { textDocument: documentParams(document) });
    }),
    vscode.languages.registerCompletionItemProvider(VPP_LANGUAGE_SELECTOR, {
      async provideCompletionItems(document, position) {
        if (!languageServer) return [];
        const result = await languageServer.request('textDocument/completion', {
          textDocument: documentParams(document),
          position: lspPosition(position)
        });
        const sourceItems = Array.isArray(result) ? result : (result?.items || []);
        const items = sourceItems.map((source) => {
          const item = new vscode.CompletionItem(source.label, completionKind(source.kind));
          item.detail = source.detail;
          if (source.insertText) item.insertText = source.insertText;
          return item;
        });
        return new vscode.CompletionList(items, Boolean(result?.isIncomplete));
      }
    }, '.'),
    vscode.languages.registerDefinitionProvider(VPP_LANGUAGE_SELECTOR, {
      async provideDefinition(document, position) {
        if (!languageServer) return null;
        const result = await languageServer.request('textDocument/definition', {
          textDocument: documentParams(document),
          position: lspPosition(position)
        });
        if (!result) return null;
        if (Array.isArray(result)) {
          return result.map((location) => new vscode.Location(
            vscode.Uri.parse(location.uri),
            vscodeRange(location.range)
          ));
        }
        return new vscode.Location(vscode.Uri.parse(result.uri), vscodeRange(result.range));
      }
    }),
    vscode.languages.registerHoverProvider(VPP_LANGUAGE_SELECTOR, {
      async provideHover(document, position) {
        if (!languageServer) return null;
        const result = await languageServer.request('textDocument/hover', {
          textDocument: documentParams(document),
          position: lspPosition(position)
        });
        if (!result) return null;
        const contents = typeof result.contents === 'string'
          ? result.contents
          : result.contents?.value || '';
        return new vscode.Hover(contents, result.range ? vscodeRange(result.range) : undefined);
      }
    }),
    vscode.languages.registerRenameProvider(VPP_LANGUAGE_SELECTOR, {
      async provideRenameEdits(document, position, newName) {
        if (!languageServer) return null;
        const result = await languageServer.request('textDocument/rename', {
          textDocument: documentParams(document),
          position: lspPosition(position),
          newName
        });
        if (!result) return null;
        const workspaceEdit = new vscode.WorkspaceEdit();
        for (const [uri, edits] of Object.entries(result.changes || {})) {
          const target = vscode.Uri.parse(uri);
          for (const edit of edits) {
            workspaceEdit.replace(target, vscodeRange(edit.range), edit.newText);
          }
        }
        return workspaceEdit;
      }
    }),
    vscode.languages.registerDocumentFormattingEditProvider(VPP_LANGUAGE_SELECTOR, {
      async provideDocumentFormattingEdits(document, options) {
        if (!languageServer) return [];
        const result = await languageServer.request('textDocument/formatting', {
          textDocument: documentParams(document),
          options: {
            tabSize: options.tabSize,
            insertSpaces: options.insertSpaces
          }
        });
        return (result || []).map((edit) => new vscode.TextEdit(vscodeRange(edit.range), edit.newText));
      }
    })
  );
}

function assetForCurrentPlatform() {
  if (process.platform === 'darwin') {
    return 'vpp-macos.tar.gz';
  }
  if (process.platform === 'linux' && process.arch === 'x64') {
    return 'vpp-linux-x64.tar.gz';
  }
  if (process.platform === 'win32' && process.arch === 'x64') {
    return 'vpp-windows-x64.zip';
  }

  throw new Error(`V++ chưa có VM phát hành cho ${process.platform}/${process.arch}.`);
}

function vmDirectory(context) {
  return path.join(context.globalStorageUri.fsPath, 'vm');
}

function vmBinary(context) {
  return path.join(vmDirectory(context), process.platform === 'win32' ? 'vpp.exe' : 'vpp');
}

function configureTerminalEnvironment(context) {
  const binary = vmBinary(context);
  if (!fs.existsSync(binary)) {
    return false;
  }

  const installDir = vmDirectory(context);
  context.environmentVariableCollection.prepend('PATH', `${installDir}${path.delimiter}`);
  context.environmentVariableCollection.replace('VPP_HOME', installDir);
  return true;
}

function requestToFile(url, destination, redirectCount = 0) {
  return new Promise((resolve, reject) => {
    if (redirectCount > 10) {
      reject(new Error('GitHub chuyển hướng quá nhiều lần.'));
      return;
    }

    const client = url.startsWith('https:') ? https : http;
    const request = client.get(url, {
      headers: {
        'User-Agent': 'vpp-language-vscode-extension',
        'Accept': 'application/octet-stream'
      }
    }, (response) => {
      const status = response.statusCode || 0;
      const location = response.headers.location;

      if (status >= 300 && status < 400 && location) {
        response.resume();
        const nextUrl = new URL(location, url).toString();
        requestToFile(nextUrl, destination, redirectCount + 1).then(resolve, reject);
        return;
      }

      if (status !== 200) {
        response.resume();
        reject(new Error(`GitHub trả về HTTP ${status} cho ${url}`));
        return;
      }

      const output = fs.createWriteStream(destination);
      response.pipe(output);
      output.on('finish', () => output.close(resolve));
      output.on('error', reject);
      response.on('error', reject);
    });

    request.setTimeout(60_000, () => request.destroy(new Error('Hết thời gian tải VM từ GitHub.')));
    request.on('error', reject);
  });
}

function run(command, args) {
  return new Promise((resolve, reject) => {
    const child = spawn(command, args, { stdio: ['ignore', 'pipe', 'pipe'] });
    let stderr = '';

    child.stderr.on('data', (chunk) => {
      stderr += chunk.toString();
    });
    child.on('error', reject);
    child.on('close', (code) => {
      if (code === 0) {
        resolve();
      } else {
        reject(new Error(stderr.trim() || `${command} thoát với mã ${code}`));
      }
    });
  });
}

async function extractArchive(archivePath, destination) {
  await fs.promises.mkdir(destination, { recursive: true });

  if (process.platform === 'win32') {
    const escapedArchive = archivePath.replace(/'/g, "''");
    const escapedDestination = destination.replace(/'/g, "''");
    const script = `Expand-Archive -LiteralPath '${escapedArchive}' -DestinationPath '${escapedDestination}' -Force`;
    await run('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script]);
    return;
  }

  await run('tar', ['-xzf', archivePath, '-C', destination]);
}

async function validateVmLayout(directory) {
  const binary = path.join(directory, process.platform === 'win32' ? 'vpp.exe' : 'vpp');
  const stdlib = path.join(directory, 'gói');
  const templates = path.join(directory, 'templates');

  for (const requiredPath of [binary, stdlib, templates]) {
    if (!fs.existsSync(requiredPath)) {
      throw new Error(`Gói release thiếu thành phần bắt buộc: ${path.basename(requiredPath)}`);
    }
  }

  if (process.platform !== 'win32') {
    await fs.promises.chmod(binary, 0o755);
  }
}

async function installVm(context) {
  const asset = assetForCurrentPlatform();
  const storageDir = context.globalStorageUri.fsPath;
  const archivePath = path.join(storageDir, asset);
  const stagingDir = path.join(storageDir, 'vm-staging');
  const installDir = vmDirectory(context);
  const downloadUrl = `${LATEST_DOWNLOAD_URL}/${asset}`;

  await fs.promises.mkdir(storageDir, { recursive: true });
  await fs.promises.rm(stagingDir, { recursive: true, force: true });

  try {
    await vscode.window.withProgress({
      location: vscode.ProgressLocation.Notification,
      title: `V++: Đang cài VM từ GitHub (${asset})`,
      cancellable: false
    }, async (progress) => {
      progress.report({ message: 'Đang tải release mới nhất…' });
      await requestToFile(downloadUrl, archivePath);

      progress.report({ message: 'Đang giải nén…' });
      await extractArchive(archivePath, stagingDir);
      await validateVmLayout(stagingDir);

      progress.report({ message: 'Đang hoàn tất cài đặt…' });
      await fs.promises.rm(installDir, { recursive: true, force: true });
      await fs.promises.rename(stagingDir, installDir);
    });

    configureTerminalEnvironment(context);
    const binary = vmBinary(context);
    vscode.window.showInformationMessage(`Đã cài V++ VM: ${binary}. Terminal mới trong VS Code có thể dùng lệnh vpp.`);
    await restartLanguageServer(context);
  } finally {
    await fs.promises.rm(archivePath, { force: true }).catch(() => {});
    await fs.promises.rm(stagingDir, { recursive: true, force: true }).catch(() => {});
  }
}

async function installVmCommand(context) {
  try {
    await installVm(context);
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    const choice = await vscode.window.showErrorMessage(`Không thể cài V++ VM: ${message}`, 'Mở GitHub Releases');
    if (choice === 'Mở GitHub Releases') {
      await vscode.env.openExternal(vscode.Uri.parse(RELEASES_URL));
    }
  }
}

async function showVmInfo(context) {
  const binary = vmBinary(context);
  if (fs.existsSync(binary)) {
    vscode.window.showInformationMessage(`V++ VM đang được extension quản lý tại: ${binary}`);
    return;
  }

  const choice = await vscode.window.showInformationMessage('Chưa cài V++ VM bằng extension.', 'Cài VM');
  if (choice === 'Cài VM') {
    await installVmCommand(context);
  }
}

async function maybePromptInstall(context) {
  const config = vscode.workspace.getConfiguration('vpp');
  if (!config.get('vm.autoPromptInstall', true) || fs.existsSync(vmBinary(context))) {
    return;
  }

  const promptedKey = 'vpp.vm.installPromptShown';
  if (context.globalState.get(promptedKey, false)) {
    return;
  }

  await context.globalState.update(promptedKey, true);
  const choice = await vscode.window.showInformationMessage(
    'V++ Language có thể cài V++ VM trực tiếp từ GitHub Releases.',
    'Cài VM',
    'Mở Releases'
  );

  if (choice === 'Cài VM') {
    await installVmCommand(context);
  } else if (choice === 'Mở Releases') {
    await vscode.env.openExternal(vscode.Uri.parse(RELEASES_URL));
  }
}

function activate(context) {
  configureTerminalEnvironment(context);
  languageServerOutput = vscode.window.createOutputChannel('V++ Language Server');
  context.subscriptions.push(languageServerOutput);
  registerLanguageFeatures(context);

  context.subscriptions.push(
    vscode.commands.registerCommand('vpp.installVm', () => installVmCommand(context)),
    vscode.commands.registerCommand('vpp.showVmInfo', () => showVmInfo(context)),
    vscode.commands.registerCommand('vpp.openReleases', () => vscode.env.openExternal(vscode.Uri.parse(RELEASES_URL))),
    vscode.commands.registerCommand('vpp.restartLanguageServer', () => restartLanguageServer(context, true)),
    vscode.workspace.onDidChangeConfiguration((event) => {
      if (event.affectsConfiguration('vpp.lsp')) {
        void restartLanguageServer(context);
      }
    })
  );

  if (vscode.workspace.textDocuments.some((document) => document.languageId === 'vpp')) {
    void startLanguageServer(context);
  }
  void maybePromptInstall(context);
}

async function deactivate() {
  if (languageServer) {
    const current = languageServer;
    languageServer = null;
    await current.stop();
    current.dispose();
  }
}

module.exports = { activate, deactivate };
