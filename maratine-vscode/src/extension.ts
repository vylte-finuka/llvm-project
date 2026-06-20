// Vyft Ltd — Extension VSCode Maratine — 2026

import * as path from 'path';
import * as fs   from 'fs';
import * as vscode from 'vscode';
import {
  LanguageClient,
  LanguageClientOptions,
  ServerOptions,
  TransportKind,
  ExecutableOptions,
} from 'vscode-languageclient/node';

let client: LanguageClient | undefined;

// ---------------------------------------------------------------------------
// Trouver marai.exe
// ---------------------------------------------------------------------------

function findMarai(): string | undefined {
  const cfg = vscode.workspace.getConfiguration('maratine');
  const custom: string = cfg.get('marai.path') ?? '';
  if (custom && fs.existsSync(custom)) return custom;

  const candidates = [
    // Installation standard
    'D:\\maratine-install\\bin\\marai.exe',
    // Dossier build local
    path.join(__dirname, '..', '..', 'maratine', 'build', 'tools', 'marai', 'marai.exe'),
    // PATH
    'marai',
    'marai.exe',
  ];
  for (const c of candidates) {
    try { if (fs.existsSync(c)) return c; } catch {}
  }
  return undefined;
}

// ---------------------------------------------------------------------------
// Commandes enregistrées
// ---------------------------------------------------------------------------

async function cmdBuild() {
  const marai = findMarai();
  if (!marai) {
    vscode.window.showErrorMessage('marai.exe introuvable. Configurer maratine.marai.path.');
    return;
  }
  const ws = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!ws) { vscode.window.showErrorMessage('Aucun dossier de travail ouvert.'); return; }

  const mareps = fs.readdirSync(ws).filter(f => f.endsWith('.marep') || f.endsWith('.slul'));
  if (mareps.length === 0) {
    vscode.window.showWarningMessage('Aucun projet .marep / .slul trouve dans le dossier.');
    return;
  }

  const pick = mareps.length === 1 ? mareps[0]
    : await vscode.window.showQuickPick(mareps, { placeHolder: 'Choisir le projet a compiler' });
  if (!pick) return;

  const terminal = vscode.window.createTerminal('Maratine Build');
  terminal.show();
  terminal.sendText(`"${marai}" build "${path.join(ws, pick)}" -O`);
}

async function cmdCheck() {
  const marai = findMarai();
  if (!marai) {
    vscode.window.showErrorMessage('marai.exe introuvable.');
    return;
  }
  const ws = vscode.workspace.workspaceFolders?.[0]?.uri.fsPath;
  if (!ws) return;

  const mareps = fs.readdirSync(ws).filter(f => f.endsWith('.marep') || f.endsWith('.slul'));
  if (mareps.length === 0) {
    vscode.window.showWarningMessage('Aucun projet .marep / .slul trouve.');
    return;
  }

  const pick = mareps.length === 1 ? mareps[0]
    : await vscode.window.showQuickPick(mareps, { placeHolder: 'Choisir le projet a auditer' });
  if (!pick) return;

  const terminal = vscode.window.createTerminal('Maratine Check');
  terminal.show();
  terminal.sendText(`"${marai}" check "${path.join(ws, pick)}" -O`);
}

async function cmdDumpAST() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== 'mara') return;

  const cfg = vscode.workspace.getConfiguration('maratine');
  let cc: string = cfg.get('compiler.path') ?? '';
  if (!cc) {
    const candidates = [
      'D:\\maratine-install\\bin\\maratine-cc.exe',
      path.join(__dirname, '..', '..', 'maratine', 'build', 'maratine-cc.exe'),
    ];
    for (const c of candidates) { try { if (fs.existsSync(c)) { cc = c; break; } } catch {} }
  }
  if (!cc) { vscode.window.showErrorMessage('maratine-cc.exe introuvable.'); return; }

  const terminal = vscode.window.createTerminal('Maratine AST');
  terminal.show();
  terminal.sendText(`"${cc}" "${editor.document.uri.fsPath}" -dump-ast -emit llvm -o NUL`);
}

async function cmdDumpTokens() {
  const editor = vscode.window.activeTextEditor;
  if (!editor || editor.document.languageId !== 'mara') return;

  const cfg = vscode.workspace.getConfiguration('maratine');
  let cc: string = cfg.get('compiler.path') ?? '';
  if (!cc) {
    const candidates = [
      'D:\\maratine-install\\bin\\maratine-cc.exe',
      path.join(__dirname, '..', '..', 'maratine', 'build', 'maratine-cc.exe'),
    ];
    for (const c of candidates) { try { if (fs.existsSync(c)) { cc = c; break; } } catch {} }
  }
  if (!cc) { vscode.window.showErrorMessage('maratine-cc.exe introuvable.'); return; }

  const terminal = vscode.window.createTerminal('Maratine Tokens');
  terminal.show();
  terminal.sendText(`"${cc}" "${editor.document.uri.fsPath}" -dump-tokens -emit llvm -o NUL`);
}

// ---------------------------------------------------------------------------
// Demarrage du client LSP
// ---------------------------------------------------------------------------

function startLSP(context: vscode.ExtensionContext) {
  const marai = findMarai();
  if (!marai) {
    vscode.window.showWarningMessage(
      'Maratine LSP: marai.exe introuvable — diagnostics et completion desactives. ' +
      'Configurer maratine.marai.path dans les parametres.'
    );
    return;
  }

  const serverOptions: ServerOptions = {
    command: marai,
    args:    ['lsp'],
    transport: TransportKind.stdio,
    options: { env: process.env } as ExecutableOptions,
  };

  const cfg = vscode.workspace.getConfiguration('maratine');
  const clientOptions: LanguageClientOptions = {
    documentSelector: [{ scheme: 'file', language: 'mara' }],
    synchronize: {
      fileEvents: vscode.workspace.createFileSystemWatcher('**/*.mara'),
    },
    // Envoyer le texte complet lors de didSave pour alimenter le cache LSP
    middleware: {
      didSave: (document, next) => {
        return next(document);
      },
    },
    traceOutputChannel: vscode.window.createOutputChannel('Maratine LSP Trace'),
    outputChannelName: 'Maratine Language Server',
    initializationOptions: {
      compilerPath: cfg.get<string>('compiler.path') ?? '',
    },
  };

  client = new LanguageClient(
    'maratine-lsp',
    'Maratine Language Server',
    serverOptions,
    clientOptions,
  );

  client.start();
  context.subscriptions.push(client);
  context.subscriptions.push(
    vscode.window.setStatusBarMessage('$(check) Maratine LSP actif', 3000)
  );
}

// ---------------------------------------------------------------------------
// Activation / Desactivation
// ---------------------------------------------------------------------------

export function activate(context: vscode.ExtensionContext) {
  // Commandes
  context.subscriptions.push(
    vscode.commands.registerCommand('maratine.build',      cmdBuild),
    vscode.commands.registerCommand('maratine.check',      cmdCheck),
    vscode.commands.registerCommand('maratine.dumpAST',    cmdDumpAST),
    vscode.commands.registerCommand('maratine.dumpTokens', cmdDumpTokens),
  );

  // LSP
  const cfg = vscode.workspace.getConfiguration('maratine');
  if (cfg.get<boolean>('lsp.enable') !== false) {
    startLSP(context);
  }
}

export async function deactivate() {
  if (client) await client.stop();
}
