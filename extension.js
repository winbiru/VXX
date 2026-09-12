const vscode = require('vscode');
const fs = require('fs');
const path = require('path');
const http = require('http');
const https = require('https');
const { spawn } = require('child_process');

const RELEASES_URL = 'https://github.com/winbiru/VXX/releases';
const LATEST_DOWNLOAD_URL = `${RELEASES_URL}/latest/download`;

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

  context.subscriptions.push(
    vscode.commands.registerCommand('vpp.installVm', () => installVmCommand(context)),
    vscode.commands.registerCommand('vpp.showVmInfo', () => showVmInfo(context)),
    vscode.commands.registerCommand('vpp.openReleases', () => vscode.env.openExternal(vscode.Uri.parse(RELEASES_URL)))
  );

  void maybePromptInstall(context);
}

function deactivate() {}

module.exports = { activate, deactivate };
