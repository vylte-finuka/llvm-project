"use strict";
// Vyft Ltd — Extension VSCode Maratine — 2026
var __createBinding = (this && this.__createBinding) || (Object.create ? (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    var desc = Object.getOwnPropertyDescriptor(m, k);
    if (!desc || ("get" in desc ? !m.__esModule : desc.writable || desc.configurable)) {
      desc = { enumerable: true, get: function() { return m[k]; } };
    }
    Object.defineProperty(o, k2, desc);
}) : (function(o, m, k, k2) {
    if (k2 === undefined) k2 = k;
    o[k2] = m[k];
}));
var __setModuleDefault = (this && this.__setModuleDefault) || (Object.create ? (function(o, v) {
    Object.defineProperty(o, "default", { enumerable: true, value: v });
}) : function(o, v) {
    o["default"] = v;
});
var __importStar = (this && this.__importStar) || (function () {
    var ownKeys = function(o) {
        ownKeys = Object.getOwnPropertyNames || function (o) {
            var ar = [];
            for (var k in o) if (Object.prototype.hasOwnProperty.call(o, k)) ar[ar.length] = k;
            return ar;
        };
        return ownKeys(o);
    };
    return function (mod) {
        if (mod && mod.__esModule) return mod;
        var result = {};
        if (mod != null) for (var k = ownKeys(mod), i = 0; i < k.length; i++) if (k[i] !== "default") __createBinding(result, mod, k[i]);
        __setModuleDefault(result, mod);
        return result;
    };
})();
Object.defineProperty(exports, "__esModule", { value: true });
exports.activate = activate;
exports.deactivate = deactivate;
const path = __importStar(require("path"));
const fs = __importStar(require("fs"));
const vscode = __importStar(require("vscode"));
const node_1 = require("vscode-languageclient/node");
let client;
// ---------------------------------------------------------------------------
// Trouver marai.exe
// ---------------------------------------------------------------------------
function findMarai() {
    const cfg = vscode.workspace.getConfiguration('maratine');
    const custom = cfg.get('marai.path') ?? '';
    if (custom && fs.existsSync(custom))
        return custom;
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
        try {
            if (fs.existsSync(c))
                return c;
        }
        catch { }
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
    if (!ws) {
        vscode.window.showErrorMessage('Aucun dossier de travail ouvert.');
        return;
    }
    const mareps = fs.readdirSync(ws).filter(f => f.endsWith('.marep') || f.endsWith('.slul'));
    if (mareps.length === 0) {
        vscode.window.showWarningMessage('Aucun projet .marep / .slul trouve dans le dossier.');
        return;
    }
    const pick = mareps.length === 1 ? mareps[0]
        : await vscode.window.showQuickPick(mareps, { placeHolder: 'Choisir le projet a compiler' });
    if (!pick)
        return;
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
    if (!ws)
        return;
    const mareps = fs.readdirSync(ws).filter(f => f.endsWith('.marep') || f.endsWith('.slul'));
    if (mareps.length === 0) {
        vscode.window.showWarningMessage('Aucun projet .marep / .slul trouve.');
        return;
    }
    const pick = mareps.length === 1 ? mareps[0]
        : await vscode.window.showQuickPick(mareps, { placeHolder: 'Choisir le projet a auditer' });
    if (!pick)
        return;
    const terminal = vscode.window.createTerminal('Maratine Check');
    terminal.show();
    terminal.sendText(`"${marai}" check "${path.join(ws, pick)}" -O`);
}
async function cmdDumpAST() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'mara')
        return;
    const cfg = vscode.workspace.getConfiguration('maratine');
    let cc = cfg.get('compiler.path') ?? '';
    if (!cc) {
        const candidates = [
            'D:\\maratine-install\\bin\\maratine-cc.exe',
            path.join(__dirname, '..', '..', 'maratine', 'build', 'maratine-cc.exe'),
        ];
        for (const c of candidates) {
            try {
                if (fs.existsSync(c)) {
                    cc = c;
                    break;
                }
            }
            catch { }
        }
    }
    if (!cc) {
        vscode.window.showErrorMessage('maratine-cc.exe introuvable.');
        return;
    }
    const terminal = vscode.window.createTerminal('Maratine AST');
    terminal.show();
    terminal.sendText(`"${cc}" "${editor.document.uri.fsPath}" -dump-ast -emit llvm -o NUL`);
}
async function cmdDumpTokens() {
    const editor = vscode.window.activeTextEditor;
    if (!editor || editor.document.languageId !== 'mara')
        return;
    const cfg = vscode.workspace.getConfiguration('maratine');
    let cc = cfg.get('compiler.path') ?? '';
    if (!cc) {
        const candidates = [
            'D:\\maratine-install\\bin\\maratine-cc.exe',
            path.join(__dirname, '..', '..', 'maratine', 'build', 'maratine-cc.exe'),
        ];
        for (const c of candidates) {
            try {
                if (fs.existsSync(c)) {
                    cc = c;
                    break;
                }
            }
            catch { }
        }
    }
    if (!cc) {
        vscode.window.showErrorMessage('maratine-cc.exe introuvable.');
        return;
    }
    const terminal = vscode.window.createTerminal('Maratine Tokens');
    terminal.show();
    terminal.sendText(`"${cc}" "${editor.document.uri.fsPath}" -dump-tokens -emit llvm -o NUL`);
}
// ---------------------------------------------------------------------------
// Demarrage du client LSP
// ---------------------------------------------------------------------------
function startLSP(context) {
    const marai = findMarai();
    if (!marai) {
        vscode.window.showWarningMessage('Maratine LSP: marai.exe introuvable — diagnostics et completion desactives. ' +
            'Configurer maratine.marai.path dans les parametres.');
        return;
    }
    const serverOptions = {
        command: marai,
        args: ['lsp'],
        transport: node_1.TransportKind.stdio,
        options: { env: process.env },
    };
    const cfg = vscode.workspace.getConfiguration('maratine');
    const clientOptions = {
        documentSelector: [{ scheme: 'file', language: 'mara' }],
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.mara'),
        },
        traceOutputChannel: vscode.window.createOutputChannel('Maratine LSP Trace'),
        outputChannelName: 'Maratine Language Server',
        initializationOptions: {
            compilerPath: cfg.get('compiler.path') ?? '',
        },
    };
    client = new node_1.LanguageClient('maratine-lsp', 'Maratine Language Server', serverOptions, clientOptions);
    client.start();
    context.subscriptions.push(client);
    context.subscriptions.push(vscode.window.setStatusBarMessage('$(check) Maratine LSP actif', 3000));
}
// ---------------------------------------------------------------------------
// Activation / Desactivation
// ---------------------------------------------------------------------------
function activate(context) {
    // Commandes
    context.subscriptions.push(vscode.commands.registerCommand('maratine.build', cmdBuild), vscode.commands.registerCommand('maratine.check', cmdCheck), vscode.commands.registerCommand('maratine.dumpAST', cmdDumpAST), vscode.commands.registerCommand('maratine.dumpTokens', cmdDumpTokens));
    // LSP
    const cfg = vscode.workspace.getConfiguration('maratine');
    if (cfg.get('lsp.enable') !== false) {
        startLSP(context);
    }
}
async function deactivate() {
    if (client)
        await client.stop();
}
//# sourceMappingURL=extension.js.map