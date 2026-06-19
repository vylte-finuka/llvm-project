// Vyft Ltd — marai lsp — Serveur LSP Maratine — 2026
//
// Serveur LSP minimal (JSON-RPC over stdio) :
//   - textDocument/publishDiagnostics  (via maratine-cc)
//   - textDocument/completion          (mots-cles, types, patterns FFI)
//   - textDocument/hover               (description des types Mara)
//   - textDocument/definition          (non implemente — stub)

#include "MaraiLSP.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/raw_ostream.h"
#include <cstdio>
#include <iostream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

using namespace llvm;
using namespace marai;

// ---------------------------------------------------------------------------
// JSON-RPC framing
// ---------------------------------------------------------------------------

static std::string readMessage() {
  std::string header;
  int contentLength = -1;

  while (true) {
    std::string line;
    if (!std::getline(std::cin, line)) return {};
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) break;
    if (line.rfind("Content-Length:", 0) == 0)
      contentLength = std::stoi(line.substr(15));
  }

  if (contentLength <= 0) return {};

  std::string body(contentLength, '\0');
  std::cin.read(&body[0], contentLength);
  return body;
}

// ---------------------------------------------------------------------------
// String helpers
// ---------------------------------------------------------------------------

static std::vector<std::string> splitLines(const std::string &S) {
  std::vector<std::string> Lines;
  std::istringstream SS(S);
  std::string L;
  while (std::getline(SS, L)) Lines.push_back(L);
  return Lines;
}

static bool strContains(const std::string &S, const std::string &Sub) {
  return S.find(Sub) != std::string::npos;
}

static std::string trimLeft(const std::string &S) {
  size_t P = S.find_first_not_of(" \t\r\n");
  return (P == std::string::npos) ? "" : S.substr(P);
}

static void sendMessage(const json::Value &V) {
  std::string Body;
  raw_string_ostream OS(Body);
  OS << V;
  OS.flush();
  std::cout << "Content-Length: " << Body.size() << "\r\n\r\n" << Body;
  std::cout.flush();
}

static json::Value makeResponse(const json::Value &Id, json::Value Result) {
  return json::Object{
    {"jsonrpc", "2.0"},
    {"id",     Id},
    {"result", std::move(Result)},
  };
}

static json::Value makeNotification(StringRef Method, json::Value Params) {
  return json::Object{
    {"jsonrpc", "2.0"},
    {"method",  Method},
    {"params",  std::move(Params)},
  };
}

// ---------------------------------------------------------------------------
// Localiser maratine-cc
// ---------------------------------------------------------------------------

static std::string resolveCompiler(const std::string &MaraiExe,
                                   const std::string &cfgPath) {
  if (!cfgPath.empty() && sys::fs::exists(cfgPath)) return cfgPath;

  SmallString<256> CC(MaraiExe);
  sys::path::remove_filename(CC);
  sys::path::append(CC, "..", "..", "maratine-cc.exe");
  sys::fs::make_absolute(CC);
  if (sys::fs::exists(CC)) return std::string(CC);

  SmallString<256> Same(MaraiExe);
  sys::path::remove_filename(Same);
  sys::path::append(Same, "maratine-cc.exe");
  if (sys::fs::exists(Same)) return std::string(Same);

  if (auto F = sys::findProgramByName("maratine-cc")) return *F;
  return {};
}

// ---------------------------------------------------------------------------
// Compiler et extraire les diagnostics
// ---------------------------------------------------------------------------

struct Diagnostic {
  int    line = 0, col = 0;
  std::string message;
  int    severity = 1; // 1=error, 2=warning
};

static std::vector<Diagnostic> compileDiagnostics(
    const std::string &Compiler, const std::string &FilePath) {

  std::vector<Diagnostic> Diags;
  if (Compiler.empty() || FilePath.empty()) return Diags;

  // Ecrire le contenu dans un fichier temp si necessaire (LSP envoie le buffer)
  SmallString<256> OutNull;
  sys::fs::createUniqueFile("marai-lsp-out-%%%%", OutNull);

  std::vector<StringRef> Args;
  Args.push_back(Compiler);
  Args.push_back(FilePath);
  Args.push_back("-emit");
  Args.push_back("llvm");
  Args.push_back("-o");
  Args.push_back(OutNull.str());

  // Capturer stderr
  SmallString<256> StderrFile;
  sys::fs::createUniqueFile("marai-lsp-err-%%%%", StderrFile);
  std::vector<std::optional<StringRef>> Redirects = {
    std::nullopt,                   // stdin  = inherit
    std::nullopt,                   // stdout = inherit
    StringRef(StderrFile.str()),    // stderr = capture
  };

  std::string ErrMsg;
  sys::ExecuteAndWait(Compiler, Args, std::nullopt, Redirects, 30, 0, &ErrMsg);

  sys::fs::remove(OutNull);

  // Lire la sortie erreur
  if (auto Buf = MemoryBuffer::getFile(StderrFile)) {
    std::string Output = Buf.get()->getBuffer().str();
    sys::fs::remove(StderrFile);

    // Parser les lignes d'erreur — formats :
    //   [line:col] message
    //   Parse error: [line:col] message
    //   error [line:col]: message
    std::istringstream SS(Output);
    std::string Line;
    while (std::getline(SS, Line)) {
      size_t LB = Line.find('[');
      size_t RB = Line.find(']', LB);
      if (LB == std::string::npos || RB == std::string::npos) continue;

      std::string Loc = Line.substr(LB + 1, RB - LB - 1);
      size_t Colon = Loc.find(':');
      if (Colon == std::string::npos) continue;

      try {
        Diagnostic D;
        D.line    = std::stoi(Loc.substr(0, Colon)) - 1; // 0-indexed
        D.col     = std::stoi(Loc.substr(Colon + 1)) - 1;
        D.message = Line.substr(RB + 1);
        // trim
        while (!D.message.empty() && (D.message[0]==' '||D.message[0]==':'))
          D.message = D.message.substr(1);

        bool isWarn = Line.find("warning") != std::string::npos;
        D.severity = isWarn ? 2 : 1;
        if (D.line >= 0) Diags.push_back(std::move(D));
      } catch (...) {}
    }
  } else {
    sys::fs::remove(StderrFile);
  }

  return Diags;
}

// ---------------------------------------------------------------------------
// Publier les diagnostics
// ---------------------------------------------------------------------------

static void publishDiagnostics(const std::string &Uri,
                               const std::vector<Diagnostic> &Diags) {
  json::Array JDiags;
  for (const auto &D : Diags) {
    JDiags.push_back(json::Object{
      {"range", json::Object{
        {"start", json::Object{{"line", D.line}, {"character", D.col}}},
        {"end",   json::Object{{"line", D.line}, {"character", D.col + 1}}},
      }},
      {"severity", D.severity},
      {"source",   "maratine-cc"},
      {"message",  D.message},
    });
  }
  sendMessage(makeNotification("textDocument/publishDiagnostics", json::Object{
    {"uri",         Uri},
    {"diagnostics", std::move(JDiags)},
  }));
}

// ---------------------------------------------------------------------------
// Completion items
// ---------------------------------------------------------------------------

static json::Array buildCompletionItems() {
  struct Item { std::string label; int kind; std::string detail; std::string doc; };
  // LSP completion kinds: 1=text,14=keyword,25=type,3=function,6=variable
  static const Item Items[] = {
    // Keywords
    {"rel",     14, "Declarateur de fonction",    "rel op / rel cl"},
    {"op",      14, "Visibilite publique",         "Fonction publique (ExternalLinkage)"},
    {"cl",      14, "Visibilite privee",           "Fonction privee (InternalLinkage si imbriquee)"},
    {"let",     14, "Declaration constante",       "let nom: <type> = valeur;"},
    {"var",     14, "Declaration variable",        "var nom: <type> = valeur;"},
    {"if",      14, "Conditionnel",                "if (cond) [ ... ];"},
    {"else",    14, "Sinon",                       "else [ ... ]"},
    {"loop",    14, "Boucle",                      "loop cond [ ... ];"},
    {"break",   14, "Sortie de boucle",            "break;"},
    {"ret",     14, "Retour",                      "ret valeur;"},
    {"log",     14, "Affichage console",           "log: expression;"},
    {"self",    14, "Reference interne",           "self***methode()"},
    {"null",    14, "Valeur nulle",                "null"},
    {"nullptr", 14, "Pointeur nul",                "nullptr"},
    {"true",    14, "Booleen vrai",                "true"},
    {"false",   14, "Booleen faux",                "false"},
    // Types primitifs
    {"string",  25, "Chaine de caracteres",        "<string>"},
    {"i32",     25, "Entier 32 bits signe",        "<i32>"},
    {"i64",     25, "Entier 64 bits signe",        "<i64>"},
    {"u64",     25, "Entier 64 bits non signe",    "<u64>"},
    {"bool",    25, "Booleen",                     "<bool>"},
    {"ptr",     25, "Pointeur opaque",             "<ptr>"},
    {"array",   25, "Tableau",                     "<array>"},
    // Patterns FFI courants
    {"DrvAPIInterCon***GpuFlushRenderContext***", 3,  "FFI GPU flush", "Declenche le rendu GPU"},
    {"MaratineKit***RenderContext***New",          3,  "FFI RenderContext", "Cree un contexte de rendu"},
    {"MaratineKit***RenderContext***Attach",       3,  "FFI attach",    "Attache un composant au contexte"},
    {"MaratineKit***RenderContext***Detach",       3,  "FFI detach",    "Detache un composant"},
    {"MaratineKit***RenderContext***Suspend",      3,  "FFI suspend",   "Suspend le rendu"},
    {"MaratineKit***RenderContext***Resume",       3,  "FFI resume",    "Reprend le rendu"},
    {"MaratineKit***RenderContext***Destroy",      3,  "FFI destroy",   "Libere le contexte"},
    {"MaratineKit***UI***TextLabel***New",         3,  "FFI TextLabel", "Cree un label texte"},
    {"MaratineKit***UI***TextLabel***SetAlign",    3,  "FFI align",     "Aligne le texte"},
    {"MaratineKit***UI***TextLabel***SetFontSize", 3,  "FFI fontsize",  "Definit la taille de police"},
    // Imports courants
    {"#base <MaratineKit>;",                6, "Import", "Importe MaratineKit"},
    {"#base <std***core***self***[ ComponentView, RenderContext ]>;", 6, "Import", "Imports stdlib UI"},
    {"#base <std***mem***[ MaraMem ]>;",    6, "Import", "Importe le gestionnaire memoire"},
    {"#base <std***net***[ MaraNet ]>;",    6, "Import", "Importe la stack reseau"},
    {"#base <std***io***[ MaraIO ]>;",      6, "Import", "Importe le gestionnaire I/O"},
  };

  json::Array Result;
  int Idx = 0;
  for (const auto &I : Items) {
    Result.push_back(json::Object{
      {"label",            I.label},
      {"kind",             I.kind},
      {"detail",           I.detail},
      {"documentation",    json::Object{{"kind","markdown"},{"value",I.doc}}},
      {"sortText",         std::to_string(Idx++)},
      {"insertText",       I.label},
    });
  }
  return Result;
}

// ---------------------------------------------------------------------------
// Hover — description des types et mots-cles
// ---------------------------------------------------------------------------

static std::optional<std::string> hoverFor(const std::string &Word) {
  static const std::unordered_map<std::string,std::string> Docs = {
    {"string",  "**Type Mara** `<string>` — Chaine de caracteres (pointeur opaque vers donnees UTF-8).\n\nEquivaut a `ptr` au niveau LLVM IR."},
    {"i32",     "**Type Mara** `<i32>` — Entier 32 bits signe.\n\nLLVM IR: `i32`"},
    {"i64",     "**Type Mara** `<i64>` — Entier 64 bits signe.\n\nLLVM IR: `i64`"},
    {"u64",     "**Type Mara** `<u64>` — Entier 64 bits non signe.\n\nLLVM IR: `i64` (interpretation non signee)"},
    {"bool",    "**Type Mara** `<bool>` — Booleen.\n\nLLVM IR: `i1`"},
    {"ptr",     "**Type Mara** `<ptr>` — Pointeur opaque (adresse memoire brute).\n\nLLVM IR: `ptr` (opaque pointer LLVM 20)"},
    {"array",   "**Type Mara** `<array>` — Tableau Mara.\n\nRepresente comme `ptr` en LLVM IR. Taille resolue au runtime."},
    {"rel",     "**Declarateur Mara** `rel` — Declare une fonction.\n\n```mara\nrel op NomPublic: [param type] [ ... ];\nrel cl NomPrive: [] [ ... ];\n```"},
    {"op",      "**Modificateur Mara** `op` — *open* (public).\n\nLa fonction recoit `ExternalLinkage` LLVM et est accessible depuis d'autres bundles."},
    {"cl",      "**Modificateur Mara** `cl` — *closed* (prive).\n\nFonctions imbriquees : `InternalLinkage` (eligible DCE).\nFonctions module : `ExternalLinkage` (entry-points Slura OS)."},
    {"let",     "**Declaration Mara** `let` — Constante.\n\n```mara\nlet nom: <i32> = 42;\n```\nNe peut pas etre reassignee apres initialisation."},
    {"var",     "**Declaration Mara** `var` — Variable mutable.\n\n```mara\nvar compteur: <i32> = 0;\n```"},
    {"ret",     "**Instruction Mara** `ret` — Retour de fonction.\n\n```mara\nret valeur;\nret;  // retour void\n```"},
    {"log",     "**Instruction Mara** `log` — Affichage console (appel printf interne).\n\n```mara\nlog: \"message \" + variable;\n```"},
    {"if",      "**Conditionnel Mara** `if` — Toujours avec `[ ]`.\n\n```mara\nif (cond) [\n    ...\n] else [\n    ...\n];\n```"},
    {"loop",    "**Boucle Mara** `loop` — Boucle avec condition ou infinie.\n\n```mara\nloop i < 10 [\n    i = i + 1;\n];\n```"},
    {"break",   "**Instruction Mara** `break` — Sort de la boucle courante.\n\nEmet un branchement `br` vers le bloc `loop.exit` en LLVM IR."},
    {"self",    "**Reference Mara** `self` — Appel interne dans le corps d'un `rel`.\n\n```mara\nlet result: <i32> = self***methodePrivee();\n```"},
  };
  auto It = Docs.find(Word);
  return (It != Docs.end()) ? std::optional<std::string>(It->second) : std::nullopt;
}

// ---------------------------------------------------------------------------
// Type → module d'origine
// ---------------------------------------------------------------------------

static std::string typeOriginModule(const std::string &TypeName) {
  static const std::unordered_map<std::string, std::string> Map = {
    {"ComponentView",    "#base <std***core***self***[ ComponentView ]>;"},
    {"RenderContext",    "#base <std***core***self***[ RenderContext ]>;"},
    {"TextLabel",        "#base <MaratineKit***UI***TextLabel>;"},
    {"AppLifecycle",     "#base <std***core***self***[ AppLifecycle ]>;"},
    {"CycleActivity",    "#base <std***core***self***[ CycleActivity ]>;"},
    {"RegistrationRoot", "#base <std***core***self***[ RegistrationRoot ]>;"},
    {"CaseComponentActivity", "#base <std***core***self***[ CaseComponentActivity ]>;"},
    {"PIDActivity",      "#base <MaratineKit***PIDActivity***ObtInfo>;"},
    {"AuthARoot",        "#base <MaratineKit***AuthARoot>;"},
    {"KeyReg",           "#base <std***core***self***RegistrationRoot***[ KeyReg ]>;"},
    {"SRIDAtt",          "#base <std***core***self***RegistrationRoot***[ SRIDAtt ]>;"},
    {"PAccessAuth",      "#base <std***core***self***RegistrationRoot***[ PAccessAuth ]>;"},
    {"MaraMem",          "#base <std***mem***[ MaraMem ]>;"},
    {"MaraFS",           "#base <std***fs***[ MaraFS ]>;"},
    {"MaraIO",           "#base <std***io***[ MaraIO ]>;"},
    {"MaraNet",          "#base <std***net***[ MaraNet ]>;"},
    {"MaraCrypto",       "#base <std***crypto***[ MaraCrypto ]>;"},
    {"MaraProc",         "#base <std***proc***[ MaraProc ]>;"},
    {"MaraGrphclCpeAPI", "#base <std***ComTpe***MarepFrmt***[ MaraGrphclCpeAPI ]>;"},
    {"MaraTypes",        "#base <std***core***types***[ MaraTypes ]>;"},
    {"ArchitectureInfo", "#base <std***gcc***arch***[ ArchitectureInfo ]>;"},
    {"MGC",              "#base <MaratineKit***MGC***[ MGC ]>;"},
    {"MGCScene",         "#base <MaratineKit***MGC***[ MGCScene ]>;"},
    {"MGCPhysics",       "#base <MaratineKit***MGC***[ MGCPhysics ]>;"},
    {"MGCAudio",         "#base <MaratineKit***MGC***[ MGCAudio ]>;"},
    {"MGCMesh",          "#base <MaratineKit***MGC***[ MGCMesh ]>;"},
    {"MGCRenderer",      "#base <MaratineKit***MGC***[ MGCRenderer ]>;"},
    {"MGCShader",        "#base <MaratineKit***MGC***[ MGCShader ]>;"},
    {"MGCTexture",       "#base <MaratineKit***MGC***[ MGCTexture ]>;"},
    {"MGCCompute",       "#base <MaratineKit***MGC***[ MGCCompute ]>;"},
    {"MGCParticle",      "#base <MaratineKit***MGC***[ MGCParticle ]>;"},
  };
  auto It = Map.find(TypeName);
  return (It != Map.end()) ? It->second : "";
}

// ---------------------------------------------------------------------------
// Extraction de symboles depuis le texte source Mara
// ---------------------------------------------------------------------------

struct DocSymbol {
  std::string name;
  std::string type;    // "<i32>", "<[string TextLabel]>", etc.
  std::string kind;    // "let", "var", "fn", "param"
  std::string detail;  // detail supplementaire (signature fn)
};

static std::unordered_map<std::string, DocSymbol>
parseDocSymbols(const std::string &Text) {
  std::unordered_map<std::string, DocSymbol> Syms;
  auto Lines = splitLines(Text);

  for (const auto &Raw : Lines) {
    std::string L = trimLeft(Raw);
    if (L.empty() || L.rfind("//", 0) == 0) continue;

    // --- let/var name: <type>
    for (const std::string &kw : {"let ", "var "}) {
      if (L.rfind(kw, 0) != 0) continue;
      std::string rest = L.substr(kw.size());
      size_t colonPos = rest.find(':');
      if (colonPos == std::string::npos) break;
      std::string name = rest.substr(0, colonPos);
      while (!name.empty() && name.back() == ' ') name.pop_back();
      while (!name.empty() && name[0] == ' ') name = name.substr(1);

      // Extract type between first < and matching >
      std::string after = rest.substr(colonPos + 1);
      size_t lt = after.find('<');
      if (lt == std::string::npos) break;
      // find matching '>' counting nesting
      int depth = 0;
      size_t gt = std::string::npos;
      for (size_t I = lt; I < after.size(); ++I) {
        if (after[I] == '<') depth++;
        else if (after[I] == '>') { depth--; if (depth == 0) { gt = I; break; } }
      }
      std::string type = (gt != std::string::npos)
          ? after.substr(lt, gt - lt + 1) : after.substr(lt);

      if (!name.empty()) {
        DocSymbol S;
        S.name   = name;
        S.type   = type;
        S.kind   = (kw == "let ") ? "let" : "var";
        S.detail = S.kind + " " + name + ": " + type;
        Syms[name] = S;
      }
      break;
    }

    // --- rel op/cl name: [params]
    if (L.rfind("rel ", 0) == 0) {
      size_t opCl = L.find("op ");
      if (opCl == std::string::npos) opCl = L.find("cl ");
      if (opCl == std::string::npos) continue;
      bool isPublic = (L.substr(opCl, 3) == "op ");
      std::string after = L.substr(opCl + 3);
      size_t colonPos = after.find(':');
      if (colonPos == std::string::npos) continue;
      std::string fname = after.substr(0, colonPos);
      while (!fname.empty() && fname.back() == ' ') fname.pop_back();

      // Extract params [...]
      std::string sig = "rel " + std::string(isPublic ? "op" : "cl") + " " + fname;
      size_t lsq = after.find('[', colonPos);
      size_t rsq = after.find(']', lsq != std::string::npos ? lsq : 0);
      if (lsq != std::string::npos && rsq != std::string::npos)
        sig += ": " + after.substr(lsq, rsq - lsq + 1);

      DocSymbol S;
      S.name   = fname;
      S.type   = isPublic ? "rel op" : "rel cl";
      S.kind   = "fn";
      S.detail = sig;
      Syms[fname] = S;

      // Parse params: [name type, ...]
      if (lsq != std::string::npos && rsq != std::string::npos) {
        std::string params = after.substr(lsq + 1, rsq - lsq - 1);
        std::istringstream PSS(params);
        std::string tok;
        while (std::getline(PSS, tok, ',')) {
          std::string pt = trimLeft(tok);
          size_t sp = pt.rfind(' ');
          if (sp == std::string::npos) continue;
          std::string pname = pt.substr(0, sp);
          std::string ptype = pt.substr(sp + 1);
          while (!pname.empty() && pname.back() == ' ') pname.pop_back();
          if (!pname.empty() && !ptype.empty()) {
            DocSymbol PS;
            PS.name   = pname;
            PS.type   = "<" + ptype + ">";
            PS.kind   = "param";
            PS.detail = "param " + pname + ": <" + ptype + ">";
            Syms[pname] = PS;
          }
        }
      }
    }
  }
  return Syms;
}

// ---------------------------------------------------------------------------
// Completions contextuelles pour les imports #base <...>
// ---------------------------------------------------------------------------

static json::Array buildImportCompletionItems() {
  struct Imp { std::string path; std::string desc; };
  static const Imp Imports[] = {
    // Runtime principal
    {"MaratineKit",
     "Runtime Maratine complet — RenderContext, UI, auth, timers"},
    // UI
    {"std***core***self***[ ComponentView, RenderContext ]",
     "Types UI de base"},
    {"std***core***self***[ ComponentView, RenderContext, TextLabel ]",
     "Types UI + TextLabel"},
    {"std***core***self***[ AppLifecycle ]",
     "Cycle de vie application .marep"},
    {"std***core***self***[ CycleActivity ]",
     "Cycle de vie driver .slul"},
    {"std***core***self***[ RegistrationRoot, CaseComponentActivity ]",
     "Securite + enregistrement bundle"},
    {"std***core***self***RegistrationRoot***[ KeyReg, SRIDAtt, PAccessAuth ]",
     "Registre de cles + authentification processus"},
    // Stdlib
    {"std***core***types***[ MaraTypes ]",
     "Types fondamentaux — conversions, clamp, abs"},
    {"std***mem***[ MaraMem ]",
     "Gestionnaire memoire — HEAP / STACK / DMA / SECURE"},
    {"std***fs***[ MaraFS ]",
     "Systeme de fichiers SDC (C: D: T:) — read/write/mkdir"},
    {"std***io***[ MaraIO ]",
     "I/O — tactile, stylet EMR, capteurs, camera, torche"},
    {"std***net***[ MaraNet ]",
     "Stack reseau — LTE / WiFi / LoRa, HTTP"},
    {"std***crypto***[ MaraCrypto ]",
     "Cryptographie — AES-256-GCM, SHA-256, HMAC-SHA256, TRNG"},
    {"std***proc***[ MaraProc ]",
     "Gestionnaire de processus — Spawn / Kill / IPC"},
    {"std***ComTpe***MarepFrmt***[ MaraGrphclCpeAPI ]",
     "API graphique bas niveau — colorimetrie, HDR, profil ICC"},
    // MGC
    {"MaratineKit***MGC***[ MGC, MGCScene, MGCMesh, MGCPhysics, MGCParticle, MGCAudio ]",
     "Moteur 3D MGC complet"},
    {"MaratineKit***MGC***[ MGC ]",
     "MGC facade — Init / Tick / Shutdown"},
    {"MaratineKit***MGC***[ MGCCore ]",
     "Pipeline GPU — resource manager, frame loop"},
    {"MaratineKit***MGC***[ MGCScene ]",
     "Scene graph — 4096 entites, 64 lumieres, frustum culling"},
    {"MaratineKit***MGC***[ MGCPhysics ]",
     "Physique — corps rigides, vehicules, raycast, BVH"},
    {"MaratineKit***MGC***[ MGCAudio ]",
     "Audio HRTF 3D — 64 sources, streaming AAC, reverb DSP"},
    {"MaratineKit***MGC***[ MGCShader ]",
     "Shaders — GLSL->SPIR-V, hot-reload, cache persistant"},
    {"MaratineKit***MGC***[ MGCTexture ]",
     "Textures — ASTC auto, LRU VRAM 256 Mo, HDR, cubemap"},
    {"MaratineKit***MGC***[ MGCRenderer ]",
     "Rendu — PBR deferred+forward, SSAO, bloom ACES"},
    {"MaratineKit***MGC***[ MGCMesh ]",
     "Maillages — VBO/IBO, skinning 64 bones, LOD x4"},
    {"MaratineKit***MGC***[ MGCParticle ]",
     "Particules GPU — 1M simultanees, 7 presets"},
    {"MaratineKit***MGC***[ MGCCompute ]",
     "GPU compute — particules, culling, terrain"},
  };

  json::Array Result;
  int Idx = 0;
  for (const auto &I : Imports) {
    // insertText is just the path (VSCode will wrap in #base <...>;)
    std::string insertText = "#base <" + I.path + ">;";
    Result.push_back(json::Object{
      {"label",         I.path},
      {"kind",          9},  // 9 = Module
      {"detail",        I.desc},
      {"documentation", json::Object{{"kind","markdown"},
        {"value", "**Import Mara**\n\n```mara\n" + insertText + "\n```\n\n" + I.desc}}},
      {"insertText",    insertText},
      {"filterText",    I.path},
      {"sortText",      std::to_string(Idx++)},
    });
  }
  return Result;
}

// ---------------------------------------------------------------------------
// Detection de contexte import : curseur apres '#base <' sur la meme ligne
// ---------------------------------------------------------------------------

static bool isImportContext(const std::string &DocText, int Line, int Col) {
  auto Lines = splitLines(DocText);
  if (Line < 0 || Line >= (int)Lines.size()) return false;
  std::string L = Lines[Line];
  // Text up to cursor
  std::string prefix = (Col <= (int)L.size()) ? L.substr(0, Col) : L;
  std::string trimmed = trimLeft(prefix);
  return (trimmed.rfind("#base <", 0) == 0 ||
          trimmed.rfind("#include <", 0) == 0 ||
          trimmed == "#base " || trimmed == "#base");
}

// ---------------------------------------------------------------------------
// Documents ouverts (contenu + URI → chemin)
// ---------------------------------------------------------------------------

static std::unordered_map<std::string, std::string> OpenDocuments; // uri -> path
static std::unordered_map<std::string, std::string> DocContents;  // uri -> source text

static std::string uriToPath(const std::string &Uri) {
  std::string P = Uri;
  if (P.rfind("file:///", 0) == 0)   P = P.substr(8);
  else if (P.rfind("file://", 0) == 0) P = P.substr(7);
  // Decode %XX
  std::string Out;
  for (size_t I = 0; I < P.size(); ++I) {
    if (P[I] == '%' && I + 2 < P.size()) {
      char Hex[3] = {P[I+1], P[I+2], 0};
      Out += (char)std::stoi(Hex, nullptr, 16);
      I += 2;
    } else if (P[I] == '/') {
      Out += '\\'; // Windows paths
    } else {
      Out += P[I];
    }
  }
  return Out;
}

// ---------------------------------------------------------------------------
// Boucle principale LSP
// ---------------------------------------------------------------------------

int marai::runLSP(const std::string &MaraiExePath) {
  std::string Compiler;
  std::string CfgCompiler;
  bool Initialized = false;

  while (true) {
    std::string Body = readMessage();
    if (Body.empty()) break;

    auto ParsedOrErr = json::parse(Body);
    if (!ParsedOrErr) { consumeError(ParsedOrErr.takeError()); continue; }

    auto &Msg = *ParsedOrErr;
    auto *Obj = Msg.getAsObject();
    if (!Obj) continue;

    std::string Method;
    if (auto M = Obj->getString("method")) Method = M->str();

    json::Value Id = nullptr;
    if (auto *I = Obj->get("id")) Id = *I;

    const json::Object *Params = Obj->getObject("params");

    // ---- initialize -------------------------------------------------------
    if (Method == "initialize") {
      Initialized = true;
      if (Params) {
        if (auto *Opts = Params->getObject("initializationOptions")) {
          if (auto CP = Opts->getString("compilerPath"))
            CfgCompiler = CP->str();
        }
      }
      Compiler = resolveCompiler(MaraiExePath, CfgCompiler);

      sendMessage(makeResponse(Id, json::Object{
        {"capabilities", json::Object{
          {"textDocumentSync", json::Object{
            {"openClose", true},
            {"change",    1},    // Full sync
            {"save",      true},
          }},
          {"completionProvider", json::Object{
            {"triggerCharacters", json::Array{"<","*","-",">","#","\""}},
            {"resolveProvider",   false},
          }},
          {"hoverProvider", true},
        }},
        {"serverInfo", json::Object{
          {"name",    "marai-lsp"},
          {"version", "0.1.0"},
        }},
      }));
      continue;
    }

    if (!Initialized && Method != "shutdown" && Method != "exit") continue;

    if (Method == "initialized") continue;

    // ---- shutdown / exit --------------------------------------------------
    if (Method == "shutdown") {
      sendMessage(makeResponse(Id, nullptr));
      continue;
    }
    if (Method == "exit") break;

    // ---- textDocument/didOpen --------------------------------------------
    if (Method == "textDocument/didOpen") {
      if (!Params) continue;
      auto *TD = Params->getObject("textDocument");
      if (!TD) continue;
      auto UriS = TD->getString("uri");
      if (!UriS) continue;
      std::string Uri   = UriS->str();
      std::string FPath = uriToPath(Uri);
      OpenDocuments[Uri] = FPath;
      // Stocker le contenu initial envoye par le client
      if (auto TextS = TD->getString("text"))
        DocContents[Uri] = TextS->str();

      auto Diags = compileDiagnostics(Compiler, FPath);
      publishDiagnostics(Uri, Diags);
      continue;
    }

    // ---- textDocument/didChange -----------------------------------------
    if (Method == "textDocument/didChange") {
      if (!Params) continue;
      auto *TD = Params->getObject("textDocument");
      if (!TD) continue;
      auto UriS = TD->getString("uri");
      if (!UriS) continue;
      std::string Uri   = UriS->str();
      std::string FPath = uriToPath(Uri);

      // Mettre a jour le contenu (Full sync — un seul changement)
      if (auto *Changes = Params->getArray("contentChanges")) {
        if (!Changes->empty()) {
          if (auto *First = Changes->front().getAsObject()) {
            if (auto TextS = First->getString("text"))
              DocContents[Uri] = TextS->str();
          }
        }
      }

      auto Diags = compileDiagnostics(Compiler, FPath);
      publishDiagnostics(Uri, Diags);
      continue;
    }

    // ---- textDocument/didSave -------------------------------------------
    if (Method == "textDocument/didSave") {
      if (!Params) continue;
      auto *TD = Params->getObject("textDocument");
      if (!TD) continue;
      auto UriS = TD->getString("uri");
      if (!UriS) continue;
      std::string Uri   = UriS->str();
      std::string FPath = uriToPath(Uri);

      auto Diags = compileDiagnostics(Compiler, FPath);
      publishDiagnostics(Uri, Diags);
      continue;
    }

    // ---- textDocument/didClose ------------------------------------------
    if (Method == "textDocument/didClose") {
      if (!Params) continue;
      auto *TD = Params->getObject("textDocument");
      if (!TD) continue;
      auto UriS = TD->getString("uri");
      if (!UriS) continue;
      std::string Uri = UriS->str();
      OpenDocuments.erase(Uri);
      DocContents.erase(Uri);
      publishDiagnostics(Uri, {});
      continue;
    }

    // ---- textDocument/completion ----------------------------------------
    if (Method == "textDocument/completion") {
      // Detecter si on est dans un contexte #base <...>
      bool inImport = false;
      if (Params) {
        auto *TD  = Params->getObject("textDocument");
        auto *Pos = Params->getObject("position");
        if (TD && Pos) {
          auto UriS2 = TD->getString("uri");
          if (UriS2) {
            auto It = DocContents.find(UriS2->str());
            if (It != DocContents.end()) {
              int CLine = (int)Pos->getInteger("line").value_or(0);
              int CCol  = (int)Pos->getInteger("character").value_or(0);
              inImport = isImportContext(It->second, CLine, CCol);
            }
          }
        }
      }

      if (inImport) {
        sendMessage(makeResponse(Id, json::Object{
          {"isIncomplete", false},
          {"items",        buildImportCompletionItems()},
        }));
      } else {
        sendMessage(makeResponse(Id, json::Object{
          {"isIncomplete", false},
          {"items",        buildCompletionItems()},
        }));
      }
      continue;
    }

    // ---- textDocument/hover ---------------------------------------------
    if (Method == "textDocument/hover") {
      if (!Params) { sendMessage(makeResponse(Id, nullptr)); continue; }
      auto *TD  = Params->getObject("textDocument");
      auto *Pos = Params->getObject("position");
      if (!TD || !Pos) { sendMessage(makeResponse(Id, nullptr)); continue; }

      auto UriS = TD->getString("uri");
      if (!UriS) { sendMessage(makeResponse(Id, nullptr)); continue; }

      std::string Uri   = UriS->str();
      std::string FPath = uriToPath(Uri);
      int HoverLine = (int)(Pos->getInteger("line").value_or(0));
      int HoverCol  = (int)(Pos->getInteger("character").value_or(0));

      // Obtenir le texte du document (du cache ou du disque)
      std::string DocText;
      {
        auto CacheIt = DocContents.find(Uri);
        if (CacheIt != DocContents.end()) {
          DocText = CacheIt->second;
        } else if (auto Buf = MemoryBuffer::getFile(FPath)) {
          DocText = Buf.get()->getBuffer().str();
        }
      }

      // Extraire le mot sous le curseur
      std::string Word;
      {
        auto Lines = splitLines(DocText);
        if (HoverLine >= 0 && HoverLine < (int)Lines.size()) {
          const std::string &L = Lines[HoverLine];
          int Pos2 = std::min(HoverCol, (int)L.size());
          int S = Pos2;
          while (S > 0 && (std::isalnum(L[S-1]) || L[S-1]=='_')) S--;
          int E = Pos2;
          while (E < (int)L.size() && (std::isalnum(L[E]) || L[E]=='_')) E++;
          Word = L.substr(S, E - S);
        }
      }

      if (Word.empty()) { sendMessage(makeResponse(Id, nullptr)); continue; }

      // 1. Chercher dans les mots-cles/types connus
      if (auto Doc = hoverFor(Word)) {
        std::string Md = *Doc;
        std::string Origin = typeOriginModule(Word);
        if (!Origin.empty())
          Md += "\n\n**Module :** `" + Origin + "`";
        sendMessage(makeResponse(Id, json::Object{
          {"contents", json::Object{{"kind","markdown"},{"value",Md}}},
        }));
        continue;
      }

      // 2. Chercher dans les symboles du document courant
      auto Syms = parseDocSymbols(DocText);
      auto SymIt = Syms.find(Word);
      if (SymIt != Syms.end()) {
        const auto &S = SymIt->second;
        std::string Md;

        if (S.kind == "fn") {
          Md = "**Fonction Mara** `" + S.type + " " + S.name + "`\n\n";
          Md += "```mara\n" + S.detail + " [ ... ];\n```";
        } else if (S.kind == "param") {
          Md = "**Parametre** `" + S.name + "`\n\n";
          Md += "```mara\ntype : " + S.type + "\n```";
        } else {
          Md = "**Variable Mara** `" + S.name + "`\n\n";
          Md += "```mara\n" + S.detail + "\n```";
          // Extraire le nom de type compose pour chercher l'origine du module
          std::string TN;
          const std::string &T = S.type;
          size_t LB = T.find('[');
          size_t RB = T.rfind(']');
          if (LB != std::string::npos && RB != std::string::npos) {
            // <[string TypeName]> -> extraire TypeName
            std::string inner = trimLeft(T.substr(LB + 1));
            size_t sp = inner.find(' ');
            if (sp != std::string::npos)
              TN = trimLeft(inner.substr(sp + 1));
            size_t eos = TN.find(']');
            if (eos != std::string::npos) TN = TN.substr(0, eos);
            while (!TN.empty() && TN.back() == ' ') TN.pop_back();
          }
          std::string Origin = typeOriginModule(TN.empty() ? Word : TN);
          if (!Origin.empty())
            Md += "\n\n**Module d'origine :** `" + Origin + "`";
        }

        sendMessage(makeResponse(Id, json::Object{
          {"contents", json::Object{{"kind","markdown"},{"value",Md}}},
        }));
        continue;
      }

      // 3. Chercher l'origine du type seul (compound type name dans le texte)
      {
        std::string Origin = typeOriginModule(Word);
        if (!Origin.empty()) {
          std::string Md = "**Type Mara** `" + Word + "`\n\n";
          Md += "**Module :** `" + Origin + "`";
          sendMessage(makeResponse(Id, json::Object{
            {"contents", json::Object{{"kind","markdown"},{"value",Md}}},
          }));
          continue;
        }
      }

      sendMessage(makeResponse(Id, nullptr));
      continue;
    }

    // Requetes non gerees — repondre null si on a un id
    if (!Id.getAsNull()) {
      sendMessage(makeResponse(Id, nullptr));
    }
  }

  return 0;
}
