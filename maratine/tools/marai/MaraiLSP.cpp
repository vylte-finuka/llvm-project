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
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace llvm;
using namespace marai;

// ---------------------------------------------------------------------------
// String helpers (avant tout le reste pour eviter les forward references)
// ---------------------------------------------------------------------------

static std::vector<std::string> splitLines(const std::string &S) {
  std::vector<std::string> Lines;
  std::istringstream SS(S);
  std::string L;
  while (std::getline(SS, L)) {
    if (!L.empty() && L.back() == '\r') L.pop_back();
    Lines.push_back(L);
  }
  return Lines;
}

static std::string trimLeft(const std::string &S) {
  size_t P = S.find_first_not_of(" \t\r\n");
  return (P == std::string::npos) ? "" : S.substr(P);
}

// ---------------------------------------------------------------------------
// Registre dynamique — construit en lisant base/ sur le disque
// ---------------------------------------------------------------------------

// Un noeud de l'arbre base/ (repertoire ou fichier .mara)
struct BaseEntry {
  std::string maraPath;   // "MaratineKit***LCom***Text"
  std::string importLine; // "#base <MaratineKit***LCom***[ Text ]>;"
  std::string fsPath;     // chemin absolu sur disque
  std::string stem;       // nom sans extension
  bool        isDir;
};

// Un symbole exporte par un fichier .mara (rel op)
struct DynExport {
  std::string name;        // "Init", "New", "Attach" ...
  std::string maraPath;    // "MaratineKit***LCom***Text***Init"
  std::string importPath;  // "MaratineKit***LCom***Text"
  std::string params;      // "[ctx ptr, ...]"
  std::string retType;     // "<i32>", "<ptr>" ...
  std::string desc;        // commentaire au-dessus de la declaration
  std::string fsPath;      // fichier source
  int         srcLine = 0;
  bool        isPublic = true;
};

static std::vector<BaseEntry> DynEntries;
static std::vector<DynExport> DynExports;
static std::string             BaseDirPath;

// ---------------------------------------------------------------------------
// Parser minimal : extrait les rel op d'un fichier .mara
// ---------------------------------------------------------------------------

static void parseMaraFileExports(const std::string &FsPath,
                                  const std::string &ModPath) {
  auto BufOrErr = MemoryBuffer::getFile(FsPath);
  if (!BufOrErr) return;

  auto Lines = [&]() {
    std::vector<std::string> V;
    std::istringstream SS(BufOrErr.get()->getBuffer().str());
    std::string L;
    while (std::getline(SS, L)) {
      if (!L.empty() && L.back() == '\r') L.pop_back();
      V.push_back(L);
    }
    return V;
  }();

  auto trim = [](const std::string &S) -> std::string {
    size_t P = S.find_first_not_of(" \t");
    if (P == std::string::npos) return {};
    size_t Q = S.find_last_not_of(" \t\r\n");
    return S.substr(P, Q - P + 1);
  };

  std::string PendingComment;
  int Depth = 0; // nesting depth dans rel [...]

  for (int LN = 0; LN < (int)Lines.size(); ++LN) {
    std::string L = trim(Lines[LN]);
    if (L.empty()) { PendingComment.clear(); continue; }

    // Commentaire — accumuler pour la prochaine declaration
    if (L.rfind("//", 0) == 0) {
      std::string c = trim(L.substr(2));
      if (!c.empty() && c[0] != '=') // ignorer les separateurs ===
        PendingComment = c;
      continue;
    }

    // Compter les [ ] pour suivre la profondeur
    for (char ch : L) {
      if (ch == '[') Depth++;
      else if (ch == ']') Depth = std::max(0, Depth - 1);
    }

    // Chercher rel op / rel cl seulement au niveau module (Depth apres parsing)
    if (L.rfind("rel ", 0) != 0) { PendingComment.clear(); continue; }

    bool isPub = (L.find(" op ") != std::string::npos);
    bool isPrv = (L.find(" cl ") != std::string::npos);
    if (!isPub && !isPrv) { PendingComment.clear(); continue; }

    // Extraire le nom
    size_t nameStart = (isPub ? L.find(" op ") : L.find(" cl ")) + 4;
    size_t colonPos  = L.find(':', nameStart);
    if (colonPos == std::string::npos) { PendingComment.clear(); continue; }
    std::string fname = trim(L.substr(nameStart, colonPos - nameStart));
    if (fname.empty()) { PendingComment.clear(); continue; }

    // Extraire les params [...]
    size_t lsq = L.find('[', colonPos);
    size_t rsq = L.find(']', lsq != std::string::npos ? lsq : 0);
    std::string params = (lsq != std::string::npos && rsq != std::string::npos)
        ? L.substr(lsq, rsq - lsq + 1) : "[]";

    // Inférer le type de retour depuis les lignes suivantes
    std::string retType = "<i32>";
    for (int J = LN+1; J < std::min(LN+20, (int)Lines.size()); ++J) {
      std::string RL = trim(Lines[J]);
      if (RL.rfind("ret ", 0) == 0) {
        std::string rv = trim(RL.substr(4));
        // Si la valeur de retour est connue
        if (rv == "0" || rv == "ERR_OK" || rv.find("ERR") != std::string::npos) retType = "<i32>";
        else if (rv.rfind("nullptr", 0) == 0 || rv.rfind("null", 0) == 0 || rv == "p" || rv.rfind("ptr", 0) == 0) retType = "<ptr>";
        else if (rv.find("_") == 0) retType = "<i32>";
        else retType = "<ptr>"; // defaut pour les types composes
        break;
      }
      if (RL.find("ret ") != std::string::npos) break;
    }

    DynExport E;
    E.name       = fname;
    E.maraPath   = ModPath + "***" + fname;
    E.importPath = ModPath;
    E.params     = params;
    E.retType    = retType;
    E.desc       = PendingComment;
    E.fsPath     = FsPath;
    E.srcLine    = LN;
    E.isPublic   = isPub;
    if (isPub) DynExports.push_back(std::move(E));
    PendingComment.clear();
  }
}

// ---------------------------------------------------------------------------
// Scanner recursif de base/
// ---------------------------------------------------------------------------

static void scanBaseDir(const std::string &Dir, const std::string &MaraPrefix) {
  std::error_code EC;
  sys::fs::directory_iterator It(Dir, EC), End;
  std::vector<std::pair<std::string,std::string>> Subdirs, Files;

  for (; It != End && !EC; It.increment(EC)) {
    StringRef Entry = It->path();
    sys::fs::file_status St;
    sys::fs::status(Entry, St);

    if (St.type() == sys::fs::file_type::directory_file) {
      // Ignorer les dossiers cachés ou CMake
      StringRef name = sys::path::filename(Entry);
      if (name.starts_with(".") || name == "CMakeFiles") continue;
      Subdirs.push_back({std::string(Entry), name.str()});
    } else if (Entry.ends_with(".mara")) {
      StringRef stem = sys::path::stem(sys::path::filename(Entry));
      Files.push_back({std::string(Entry), stem.str()});
    }
  }

  // Sous-répertoires
  for (const auto &[path, name] : Subdirs) {
    std::string childPath = MaraPrefix.empty() ? name : (MaraPrefix + "***" + name);
    BaseEntry E;
    E.maraPath   = childPath;
    E.importLine = "";
    E.fsPath     = path;
    E.stem       = name;
    E.isDir      = true;
    DynEntries.push_back(E);
    scanBaseDir(path, childPath);
  }

  // Fichiers .mara
  for (const auto &[fpath, stem] : Files) {
    std::string fullPath = MaraPrefix.empty() ? stem : (MaraPrefix + "***" + stem);

    // Format de l'import selon la profondeur
    std::string importLine;
    size_t starCount = 0;
    for (size_t i = 0; i < MaraPrefix.size()-2; ++i)
      if (MaraPrefix[i]=='*' && MaraPrefix[i+1]=='*' && MaraPrefix[i+2]=='*') starCount++;

    if (starCount >= 1) // module feuille → notation [ ]
      importLine = "#base <" + MaraPrefix + "***[ " + stem + " ]>;";
    else  // module racine (MaratineKit/File) → notation directe
      importLine = "#base <" + fullPath + ">;";

    BaseEntry E;
    E.maraPath   = fullPath;
    E.importLine = importLine;
    E.fsPath     = fpath;
    E.stem       = stem;
    E.isDir      = false;
    DynEntries.push_back(E);

    // Parser le fichier pour ses exports
    parseMaraFileExports(fpath, fullPath);
  }
}

static std::string findBaseDir(const std::string &MaraiExe,
                                const std::string &WorkspaceRoot = "") {
  // 0. Variable d'environnement MARATINE_STDLIB (definie par build-llvm-win.ps1)
  if (const char *StdlibEnv = std::getenv("MARATINE_STDLIB")) {
    if (sys::fs::is_directory(StdlibEnv)) return StdlibEnv;
  }

  // 1. Depuis la racine du workspace (fournie par le client LSP)
  if (!WorkspaceRoot.empty()) {
    SmallString<256> P(WorkspaceRoot);
    sys::path::append(P, "base");
    sys::fs::make_absolute(P);
    if (sys::fs::is_directory(P)) return std::string(P);
    // Aussi essayer directement
    if (sys::fs::is_directory(WorkspaceRoot + "/base"))
      return WorkspaceRoot + "/base";
    if (sys::fs::is_directory(WorkspaceRoot + "\\base"))
      return WorkspaceRoot + "\\base";
  }

  // 2. Depuis le repertoire de marai.exe
  SmallString<256> Dir(MaraiExe);
  sys::path::remove_filename(Dir);
  static const char *Tries[] = {
    "../../../base", "../../base", "../base", "base", nullptr
  };
  for (int i = 0; Tries[i]; ++i) {
    SmallString<256> P(Dir);
    sys::path::append(P, Tries[i]);
    sys::fs::make_absolute(P);
    if (sys::fs::is_directory(P)) return std::string(P);
  }

  // 3. Chemins d'installation standard
  for (const char *S : {
      "D:\\maratine-install\\base",
      "C:\\maratine-install\\base",
      "/usr/local/maratine/base"}) {
    if (sys::fs::is_directory(S)) return S;
  }
  return {};
}

// ---------------------------------------------------------------------------
// Maraset.yaml — lecture des dependances projet
// ---------------------------------------------------------------------------

struct MarasetInfo {
  std::string packageName;
  std::string version;
  std::string bundleType;  // "marep" ou "slul"
  std::vector<std::string> depKeys; // cles de dependances (basecon)
};

static MarasetInfo parseMaraset(const std::string &YamlPath) {
  MarasetInfo Info;
  auto BufOrErr = MemoryBuffer::getFile(YamlPath);
  if (!BufOrErr) return Info;

  std::string Section;
  for (const auto &Raw : splitLines(BufOrErr.get()->getBuffer().str())) {
    std::string L = trimLeft(Raw);
    if (L.empty() || L[0] == '#') continue;
    if (L == "package:")    { Section = "package";  continue; }
    if (L == "metadata:")   { Section = "metadata"; continue; }
    if (L == "dependencies:"){ Section = "deps";    continue; }
    if (L == "basecon:")    { Section = "basecon";  continue; }
    if (L == "resources:")  { Section = "";         continue; }

    if (Section == "package") {
      if (L.rfind("name:", 0)    == 0) Info.packageName = trimLeft(L.substr(5));
      if (L.rfind("version:", 0) == 0) Info.version     = trimLeft(L.substr(8));
    } else if (Section == "metadata") {
      if (L.rfind("bundle:", 0)  == 0) Info.bundleType  = trimLeft(L.substr(7));
    } else if (Section == "basecon") {
      // "base***example***LAPrevent: 1.0.0" → extraire le nom
      size_t colon = L.find(':');
      if (colon != std::string::npos) {
        std::string depKey = trimLeft(L.substr(0, colon));
        // Extraire le dernier segment
        size_t lastStar = depKey.rfind("***");
        if (lastStar != std::string::npos)
          depKey = depKey.substr(lastStar + 3);
        if (!depKey.empty()) Info.depKeys.push_back(depKey);
      }
    }
  }
  return Info;
}

// Trouver le dossier projet (.marep ou .slul) qui contient un fichier
static std::string findProjectDir(const std::string &FilePath) {
  SmallString<256> Dir(FilePath);
  sys::path::remove_filename(Dir);
  // Remonter jusqu'a trouver Maraset.yaml
  for (int Depth = 0; Depth < 5; ++Depth) {
    SmallString<256> Yaml(Dir);
    sys::path::append(Yaml, "Maraset.yaml");
    if (sys::fs::exists(Yaml)) return std::string(Dir);
    SmallString<256> Parent(Dir);
    sys::path::remove_filename(Parent);
    if (Parent == Dir) break;
    Dir = Parent;
  }
  return {};
}

// Struct pour les infos d'un projet ouvert
struct ProjectContext {
  std::string projectDir;      // MaratineProjectAppTemplate.marep/
  std::string bundleType;      // "marep" ou "slul"
  std::string packageName;
  std::vector<std::string> localMara; // fichiers .mara locaux (base/)
  std::vector<DynExport>   localExports; // symboles locaux
  bool hasSlasset = false;
  std::string slassetDir;
};

static ProjectContext CurrentProject;

static void loadProjectContext(const std::string &FilePath) {
  std::string PDir = findProjectDir(FilePath);
  if (PDir.empty() || PDir == CurrentProject.projectDir) return;

  CurrentProject = ProjectContext{};
  CurrentProject.projectDir = PDir;

  // Lire Maraset.yaml
  SmallString<256> YamlPath(PDir);
  sys::path::append(YamlPath, "Maraset.yaml");
  if (sys::fs::exists(YamlPath)) {
    auto Info = parseMaraset(std::string(YamlPath));
    CurrentProject.bundleType  = Info.bundleType;
    CurrentProject.packageName = Info.packageName;
  }

  // Scanner base/ local du projet
  SmallString<256> LocalBase(PDir);
  sys::path::append(LocalBase, "base");
  if (sys::fs::is_directory(LocalBase)) {
    std::error_code EC;
    sys::fs::directory_iterator It(LocalBase, EC), End;
    for (; It != End && !EC; It.increment(EC)) {
      StringRef Entry = It->path();
      if (!Entry.ends_with(".mara")) continue;
      StringRef stem = sys::path::stem(sys::path::filename(Entry));
      CurrentProject.localMara.push_back(std::string(Entry));
      // Parser le fichier pour ses exports locaux
      std::string maraPath = "local***" + stem.str();
      auto prevSize = DynExports.size();
      parseMaraFileExports(std::string(Entry), stem.str());
      // Marquer les nouveaux exports comme locaux
      for (size_t i = prevSize; i < DynExports.size(); ++i) {
        DynExports[i].importPath = "local";
        DynExports[i].maraPath   = stem.str() + "***" + DynExports[i].name;
        CurrentProject.localExports.push_back(DynExports[i]);
      }
    }
  }

  // Detecter .slasset/
  std::error_code EC;
  sys::fs::directory_iterator It(PDir, EC), End;
  for (; It != End && !EC; It.increment(EC)) {
    StringRef Entry = It->path();
    if (Entry.ends_with(".slasset")) {
      CurrentProject.hasSlasset = true;
      CurrentProject.slassetDir = std::string(Entry);
    }
  }
}

// ---------------------------------------------------------------------------
// Initialisation complete du registre dynamique
// ---------------------------------------------------------------------------

static void initDynamicRegistry(const std::string &MaraiExe,
                                 const std::string &WorkspaceRoot = "") {
  DynEntries.clear();
  DynExports.clear();
  BaseDirPath = findBaseDir(MaraiExe, WorkspaceRoot);
  if (!BaseDirPath.empty())
    scanBaseDir(BaseDirPath, "");
  // Scanner aussi le workspace/base si different
  if (!WorkspaceRoot.empty()) {
    SmallString<256> WBase(WorkspaceRoot);
    sys::path::append(WBase, "base");
    sys::fs::make_absolute(WBase);
    if (sys::fs::is_directory(WBase) && std::string(WBase) != BaseDirPath)
      scanBaseDir(std::string(WBase), "");
  }
}

// ---------------------------------------------------------------------------
// Completions dynamiques d'import — depuis DynEntries
// ---------------------------------------------------------------------------

static json::Array buildDynImportCompletions(const std::string &PathPrefix) {
  // Extraire la partie complete et le segment partiel
  std::string CompletePart;
  std::string PartialSeg;
  if (!PathPrefix.empty()) {
    size_t lastStar = PathPrefix.rfind("***");
    if (lastStar != std::string::npos) {
      CompletePart = PathPrefix.substr(0, lastStar + 3);
      PartialSeg   = PathPrefix.substr(lastStar + 3);
    } else {
      CompletePart = "";
      PartialSeg   = PathPrefix;
    }
  }

  auto prefixMatch = [](const std::string &s, const std::string &p) {
    if (p.empty()) return true;
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
      if (tolower((unsigned char)s[i]) != tolower((unsigned char)p[i]))
        return false;
    return true;
  };

  // Collecter les enfants directs de CompletePart
  std::map<std::string, const BaseEntry*> NextLevel;

  for (const auto &E : DynEntries) {
    // Le maraPath doit commencer par CompletePart
    if (E.maraPath.rfind(CompletePart, 0) != 0) continue;
    std::string rest = E.maraPath.substr(CompletePart.size());
    if (rest.empty()) continue;
    if (rest.rfind("***", 0) == 0) rest = rest.substr(3);
    if (rest.empty()) continue;
    // Segment suivant direct (pas de *** dedans)
    size_t sep = rest.find("***");
    std::string nextSeg = (sep == std::string::npos) ? rest : rest.substr(0, sep);
    if (nextSeg.empty()) continue;
    if (!prefixMatch(nextSeg, PartialSeg)) continue;
    if (!NextLevel.count(nextSeg) || !E.isDir)
      NextLevel[nextSeg] = &E;
  }

  json::Array Result;
  int Idx = 0;
  for (const auto &[seg, entry] : NextLevel) {
    bool isLeaf = !entry->isDir;
    std::string insertText = isLeaf
        ? entry->importLine
        : (seg + "***");
    std::string detail = isLeaf
        ? ("📄 " + entry->stem + ".mara")
        : ("📂 " + seg + "/");
    std::string docMd = "**`" + seg + "`**\n\n";
    if (isLeaf && !entry->importLine.empty())
      docMd += "```mara\n" + entry->importLine + "\n```";
    else
      docMd += "Module → `" + entry->maraPath + "***`";

    Result.push_back(json::Object{
      {"label",         seg},
      {"kind",          isLeaf ? 17 : 9}, // 17=File 9=Module
      {"detail",        detail},
      {"documentation", json::Object{{"kind","markdown"},{"value",docMd}}},
      {"insertText",    isLeaf ? entry->importLine : (seg + "***")},
      {"sortText",      (entry->isDir ? "0" : "1") + std::to_string(Idx++)},
    });
  }

  return Result;
}

// ---------------------------------------------------------------------------
// Completions FFI dynamiques — depuis DynExports
// ---------------------------------------------------------------------------

static json::Array buildDynFFICompletions(const std::string &PathPrefix,
                                           const std::vector<std::string> &ImportedKeys) {
  std::string CompletePart;
  std::string PartialSeg;
  if (!PathPrefix.empty()) {
    size_t lastStar = PathPrefix.rfind("***");
    if (lastStar != std::string::npos) {
      CompletePart = PathPrefix.substr(0, lastStar + 3);
      PartialSeg   = PathPrefix.substr(lastStar + 3);
    } else {
      CompletePart = "";
      PartialSeg   = PathPrefix;
    }
  }

  auto prefixMatch = [](const std::string &s, const std::string &p) {
    if (p.empty()) return true;
    if (s.size() < p.size()) return false;
    for (size_t i = 0; i < p.size(); ++i)
      if (tolower((unsigned char)s[i]) != tolower((unsigned char)p[i]))
        return false;
    return true;
  };

  // Verifier si un module est importe
  std::unordered_set<std::string> importedSet(ImportedKeys.begin(), ImportedKeys.end());
  bool filterByImport = !ImportedKeys.empty() && !PathPrefix.empty();

  // Rassembler les noeuds intermediaires (depuis DynEntries) + feuilles (DynExports)
  std::map<std::string, bool> SeenDirs;   // dir segment → already added
  json::Array Result;
  int Idx = 0;

  // 1. Noeuds intermediaires depuis DynEntries
  for (const auto &E : DynEntries) {
    if (E.maraPath.rfind(CompletePart, 0) != 0) continue;
    std::string rest = E.maraPath.substr(CompletePart.size());
    if (rest.empty()) continue;
    if (rest.rfind("***", 0) == 0) rest = rest.substr(3);
    if (rest.empty()) continue;
    size_t sep = rest.find("***");
    std::string nextSeg = (sep == std::string::npos) ? rest : rest.substr(0, sep);
    if (nextSeg.empty() || SeenDirs.count(nextSeg)) continue;
    if (!prefixMatch(nextSeg, PartialSeg)) continue;

    bool childIsDir = (sep != std::string::npos) || E.isDir;
    if (!childIsDir) continue; // les feuilles sont traitees via DynExports
    SeenDirs[nextSeg] = true;

    bool isImported = importedSet.count(nextSeg) > 0;
    Result.push_back(json::Object{
      {"label",      nextSeg},
      {"kind",       9}, // Module
      {"detail",     "📂 → " + CompletePart + nextSeg + "***..."},
      {"insertText", nextSeg + "***"},
      {"sortText",   (isImported ? "0" : "1") + std::to_string(Idx++)},
    });
  }

  // 2. Fonctions exportees depuis DynExports
  std::set<std::string> SeenFns;
  for (const auto &E : DynExports) {
    if (!E.isPublic) continue;
    if (E.maraPath.rfind(CompletePart, 0) != 0) continue;
    std::string rest = E.maraPath.substr(CompletePart.size());
    if (rest.empty()) continue;
    if (rest.rfind("***", 0) == 0) rest = rest.substr(3);
    if (rest.empty()) continue;
    // Seulement les feuilles directes (pas de *** dans rest)
    if (rest.find("***") != std::string::npos) continue;
    std::string fnName = rest;
    if (!prefixMatch(fnName, PartialSeg)) continue;
    if (SeenFns.count(fnName)) continue;
    SeenFns.insert(fnName);

    std::string paramsFmt = (E.params.size() >= 2)
        ? E.params.substr(1, E.params.size()-2) : "";
    std::string docMd = "**`" + fnName + "`** → `" + E.retType + "`\n\n";
    if (!E.desc.empty()) docMd += E.desc + "\n\n";
    docMd += "```mara\n<" + E.maraPath + ">(" + paramsFmt + ")\n```";
    if (!E.fsPath.empty()) {
      StringRef stem = sys::path::stem(sys::path::filename(E.fsPath));
      docMd += "\n\n*Défini dans* `" + stem.str() + ".mara`";
    }

    Result.push_back(json::Object{
      {"label",         fnName},
      {"kind",          3}, // Function
      {"detail",        E.retType + "  " + E.params},
      {"documentation", json::Object{{"kind","markdown"},{"value",docMd}}},
      {"insertText",    fnName},
      {"filterText",    fnName},
      {"labelDetails",  json::Object{
        {"detail",      E.params},
        {"description", E.retType},
      }},
      {"sortText",      "2" + std::to_string(Idx++)},
    });
  }

  return Result;
}

// ---------------------------------------------------------------------------
// Hover dynamique depuis DynExports
// ---------------------------------------------------------------------------

static std::optional<std::string> hoverForDynSymbol(const std::string &Word) {
  // Chercher d'abord dans les exports (fonctions)
  for (const auto &E : DynExports) {
    if (E.name != Word) continue;
    std::string paramsFmt = (E.params.size() >= 2)
        ? E.params.substr(1, E.params.size()-2) : "";
    std::string Md = "**`" + E.name + "`** → `" + E.retType + "`\n\n";
    if (!E.desc.empty()) Md += "*" + E.desc + "*\n\n";
    Md += "```mara\n<" + E.maraPath + ">(" + paramsFmt + ")\n```\n\n";
    Md += "**Import :** `#base <" + E.importPath + "***[ ... ]>;`";
    if (!E.fsPath.empty()) {
      StringRef stem = sys::path::stem(sys::path::filename(E.fsPath));
      Md += "\n\n*Source :* `" + stem.str() + ".mara`";
    }
    return Md;
  }
  // Chercher dans les entrees (modules/types)
  for (const auto &E : DynEntries) {
    if (E.stem != Word) continue;
    std::string Md = (E.isDir ? "**Module** `" : "**Fichier** `") + E.maraPath + "`\n\n";
    if (!E.importLine.empty())
      Md += "```mara\n" + E.importLine + "\n```";
    return Md;
  }
  return {};
}

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
// String helpers (suite)
// ---------------------------------------------------------------------------

static bool strContains(const std::string &S, const std::string &Sub) {
  return S.find(Sub) != std::string::npos;
}

static std::string trimLeftUnused(const std::string &S) {
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
// Registre de modules Mara — symboles exportes par chaque #base
// ---------------------------------------------------------------------------

struct MaraExport {
  std::string ffiPath;   // "MaratineKit***RenderContext***New"
  std::string display;   // "RenderContext.New"  (label dans l'IDE)
  std::string params;    // "[ctx ptr]"
  std::string ret;       // "<ptr>"
  std::string desc;
  int lspKind;           // 3=function, 25=type, 6=variable
};

// module_key → exports   (module_key = le token principal du path #base)
static const std::unordered_map<std::string, std::vector<MaraExport>> ModuleRegistry = {
  {"MaratineKit", {
    {"MaratineKit***RenderContext***New",      "RenderContext.New",      "[]",                        "<ptr>",  "Cree un contexte de rendu", 3},
    {"MaratineKit***RenderContext***Attach",   "RenderContext.Attach",   "[ctx ptr, widget ptr]",     "<i32>",  "Attache un composant", 3},
    {"MaratineKit***RenderContext***Detach",   "RenderContext.Detach",   "[ctx ptr, widget ptr]",     "<i32>",  "Detache un composant", 3},
    {"MaratineKit***RenderContext***Suspend",  "RenderContext.Suspend",  "[]",                        "<i32>",  "Suspend le rendu (arriere-plan)", 3},
    {"MaratineKit***RenderContext***Resume",   "RenderContext.Resume",   "[]",                        "<i32>",  "Reprend le rendu (premier plan)", 3},
    {"MaratineKit***RenderContext***Destroy",  "RenderContext.Destroy",  "[]",                        "<i32>",  "Libere le contexte de rendu", 3},
    {"MaratineKit***UI***TextLabel***New",     "UI.TextLabel.New",       "[text string, fg string, bg string]", "<ptr>", "Cree un label texte", 3},
    {"MaratineKit***UI***TextLabel***SetAlign","UI.TextLabel.SetAlign",  "[label ptr, align string]", "<i32>",  "Aligne le texte (center/left/right)", 3},
    {"MaratineKit***UI***TextLabel***SetFontSize","UI.TextLabel.SetFontSize","[label ptr, size i32]", "<i32>",  "Definit la taille de police (px)", 3},
    {"MaratineKit***Time***Now",               "Time.Now",               "[]",                        "<i32>",  "Timestamp Unix courant", 3},
    {"MaratineKit***Time***NowStr",            "Time.NowStr",            "[]",                        "<string>","Timestamp en chaine", 3},
    {"MaratineKit***Format***IntToStr",        "Format.IntToStr",        "[n i32]",                   "<string>","Entier vers chaine", 3},
    {"MaratineKit***Format***IntToHex",        "Format.IntToHex",        "[n i32]",                   "<string>","Entier vers hex", 3},
    {"MaratineKit***Format***IntToHex6",       "Format.IntToHex6",       "[n i32]",                   "<string>","Hex 6 chiffres (ex: 0xFF00FF)", 3},
    {"MaratineKit***Format***IntToHex8",       "Format.IntToHex8",       "[n i32]",                   "<string>","Hex 8 chiffres", 3},
    {"MaratineKit***Str***Len",                "Str.Len",                "[s string]",                "<i32>",  "Longueur de la chaine", 3},
    {"MaratineKit***Str***Contains",           "Str.Contains",           "[hay string, needle string]","<bool>","Chaine contient sous-chaine", 3},
    {"MaratineKit***Str***ToLower",            "Str.ToLower",            "[s string]",                "<string>","Minuscules", 3},
    {"MaratineKit***Str***StartsWith",         "Str.StartsWith",         "[s string, prefix string]", "<bool>", "Commence par", 3},
    {"MaratineKit***Str***EndsWith",           "Str.EndsWith",           "[s string, suffix string]", "<bool>", "Finit par", 3},
    {"MaratineKit***Str***Substring",          "Str.Substring",          "[s string, start i32, end i32]","<string>","Sous-chaine", 3},
    {"MaratineKit***AuthARoot***Verify",       "AuthARoot.Verify",       "[srid string, key ptr, level i32]","<i32>","Verifie l'authentification", 3},
    {"MaratineKit***PIDActivity***ObtInfo",    "PIDActivity.ObtInfo",    "[]",                        "<ptr>",  "Obtient l'info du processus courant", 3},
    {"MaratineKit***RenRootUI",                "RenRootUI",              "[]",                        "<ptr>",  "Racine UI (MAREP)", 3},
    {"MaratineKit***RenRoot",                  "RenRoot",                "[]",                        "<ptr>",  "Racine driver (SLUL)", 3},
    {"MaratineKit***AuthARoot",                "AuthARoot",              "[]",                        "<ptr>",  "Racine d'autorisation", 3},
  }},
  {"DrvAPIInterCon", {
    {"DrvAPIInterCon***GpuFlushRenderContext***","GpuFlushRenderContext","[ctx ptr]",  "<i32>", "Flush GPU — declenche le rendu a l'ecran", 3},
    {"DrvAPIInterCon***GpuInit***",            "GpuInit",                "[]",          "<i32>", "Initialise le pipeline GPU", 3},
    {"DrvAPIInterCon***GpuShutdown***",        "GpuShutdown",            "[]",          "<i32>", "Eteint le pipeline GPU", 3},
    {"DrvAPIInterCon***GpuSuspend***",         "GpuSuspend",             "[]",          "<i32>", "Suspend le GPU (driver onSuspend)", 3},
    {"DrvAPIInterCon***GpuResume***",          "GpuResume",              "[]",          "<i32>", "Reprend le GPU (driver onResume)", 3},
    {"DrvAPIInterCon***GpuDrawPixel***",       "GpuDrawPixel",           "[x i32, y i32, color i32]","<i32>","Dessine un pixel a la position (stylet)", 3},
    {"DrvAPIInterCon***GpuSetColorSpace***",   "GpuSetColorSpace",       "[space string]","<i32>","Espace de couleur (sRGB/DCI-P3/...)", 3},
    {"DrvAPIInterCon***GpuSetHDR***",          "GpuSetHDR",              "[enabled i32]","<i32>","Active/desactive le HDR", 3},
    {"DrvAPIInterCon***TouchGetEvent***",      "TouchGetEvent",           "[]",          "<ptr>", "Dernier evenement tactile", 3},
    {"DrvAPIInterCon***StylusGetEvent***",     "StylusGetEvent",          "[]",          "<ptr>", "Etat du stylet EMR", 3},
    {"DrvAPIInterCon***PedometerGetSteps***",  "PedometerGetSteps",       "[]",          "<i32>", "Nombre de pas (BMI270)", 3},
    {"DrvAPIInterCon***PedometerReset***",     "PedometerReset",          "[]",          "<i32>", "Remet le compteur a zero", 3},
    {"DrvAPIInterCon***AltimeterGetAltitude***","AltimeterGetAltitude",  "[]",          "<i32>", "Altitude en metres", 3},
    {"DrvAPIInterCon***TorchSetLevel***",      "TorchSetLevel",           "[level i32]", "<i32>", "Intensite torche 0-100%%", 3},
    {"DrvAPIInterCon***CameraCapture***",      "CameraCapture",           "[]",          "<ptr>", "Capture photo (OV2740)", 3},
    {"DrvAPIInterCon***CrownGetState***",      "CrownGetState",           "[]",          "<i32>", "Etat couronne physique", 3},
    {"DrvAPIInterCon***HapticVibrate***",      "HapticVibrate",           "[ms i32, intensity i32]","<i32>","Vibration haptique", 3},
    {"DrvAPIInterCon***MemAlloc***",           "MemAlloc",                "[size i32, zone i32, align i32]","<ptr>","Alloue de la memoire", 3},
    {"DrvAPIInterCon***MemFree***",            "MemFree",                 "[p ptr, zone i32]","<i32>","Libere un bloc memoire", 3},
    {"DrvAPIInterCon***MemZero***",            "MemZero",                 "[p ptr, size i32]","<i32>","Efface un bloc memoire", 3},
    {"DrvAPIInterCon***FSOpen***",             "FSOpen",                  "[path string, mode i32]","<ptr>","Ouvre un fichier", 3},
    {"DrvAPIInterCon***FSReadAll***",          "FSReadAll",               "[handle ptr]","<ptr>","Lit tout le fichier", 3},
    {"DrvAPIInterCon***FSWrite***",            "FSWrite",                 "[handle ptr, data ptr, size i32]","<i32>","Ecrit dans un fichier", 3},
    {"DrvAPIInterCon***FSClose***",            "FSClose",                 "[handle ptr]","<i32>","Ferme un handle fichier", 3},
    {"DrvAPIInterCon***FSDelete***",           "FSDelete",                "[path string]","<i32>","Supprime un fichier", 3},
    {"DrvAPIInterCon***FSMkDir***",            "FSMkDir",                 "[path string, recursive bool]","<i32>","Cree un repertoire", 3},
    {"DrvAPIInterCon***FSGetFreeSpace***",     "FSGetFreeSpace",          "[drive string]","<i64>","Espace libre en octets", 3},
    {"DrvAPIInterCon***WifiGetSignal***",      "WifiGetSignal",           "[]",          "<i32>", "Force signal WiFi (0-100)", 3},
    {"DrvAPIInterCon***LteGetSignal***",       "LteGetSignal",            "[]",          "<i32>", "Force signal LTE (0-100)", 3},
    {"DrvAPIInterCon***LoRaGetSignal***",      "LoRaGetSignal",           "[]",          "<i32>", "Signal LoRa disponible", 3},
    {"DrvAPIInterCon***WifiConnect***",        "WifiConnect",             "[apn string]","<i32>", "Connexion WiFi", 3},
    {"DrvAPIInterCon***LteConnect***",         "LteConnect",              "[apn string, iccid string]","<i32>","Connexion LTE", 3},
    {"DrvAPIInterCon***LoRaConnect***",        "LoRaConnect",             "[apn string]","<i32>", "Connexion LoRa longue portee", 3},
    {"DrvAPIInterCon***NetDisconnect***",      "NetDisconnect",           "[net i32]",   "<i32>", "Deconnexion reseau", 3},
    {"DrvAPIInterCon***NetGetSignal***",       "NetGetSignal",            "[net i32]",   "<i32>", "Signal du reseau actif", 3},
    {"DrvAPIInterCon***ESimInit***",           "ESimInit",                "[iccid string]","<i32>","Init eSIM (GSMA SGP.02)", 3},
    {"DrvAPIInterCon***ProcSpawn***",          "ProcSpawn",               "[path string, pid i32, prio i32]","<i32>","Lance un bundle .marep", 3},
    {"DrvAPIInterCon***ProcKill***",           "ProcKill",                "[pid i32]",   "<i32>", "Arrete un processus par PID", 3},
    {"DrvAPIInterCon***CryptoTRNGFill***",     "CryptoTRNGFill",          "[buf ptr, size i32]","<i32>","Remplit avec des octets aleatoires (TRNG)", 3},
    {"DrvAPIInterCon***CryptoAES256GCMEncrypt***","CryptoAES256GCMEncrypt","[key ptr, iv ptr, data ptr, size i32, out ptr]","<i32>","Chiffrement AES-256-GCM", 3},
    {"DrvAPIInterCon***CryptoSHA256***",       "CryptoSHA256",            "[data ptr, size i32, out ptr]","<i32>","Hachage SHA-256", 3},
    {"DrvAPIInterCon***CryptoHMACSHA256***",   "CryptoHMACSHA256",        "[key ptr, data ptr, size i32, out ptr]","<i32>","HMAC-SHA256", 3},
  }},
  {"MaraMem", {
    {"MaraMem***Alloc***",  "Alloc",  "[size i32, zone i32, align i32]", "<ptr>", "Alloue (zones: 0=HEAP 1=STACK 2=SHARED 3=DMA 4=SECURE)", 3},
    {"MaraMem***Free***",   "Free",   "[p ptr, zone i32]",               "<i32>", "Libere un bloc", 3},
    {"MaraMem***Zero***",   "Zero",   "[p ptr, size i32]",               "<i32>", "Efface un bloc", 3},
    {"MaraMem***GetStats***","GetStats","[]",                            "<array>","Stats: heap/stack/shared/dma/secure/alloc/free", 3},
    {"MaraMem***IsLow***",  "IsLow",  "[]",                              "<bool>","Memoire < 10%% disponible", 3},
  }},
  {"MaraFS", {
    {"MaraFS***Exists***",       "Exists",       "[path string]",             "<bool>","Verifie si le fichier/dossier existe", 3},
    {"MaraFS***Read***",         "Read",         "[path string]",             "<ptr>", "Lit le contenu d'un fichier (C:/D:/T:)", 3},
    {"MaraFS***Write***",        "Write",        "[path string, data ptr, size i32]","<i32>","Ecrit dans un fichier", 3},
    {"MaraFS***Delete***",       "Delete",       "[path string]",             "<i32>","Supprime un fichier", 3},
    {"MaraFS***MakeDir***",      "MakeDir",      "[path string]",             "<i32>","Cree un repertoire", 3},
    {"MaraFS***GetFreeSpace***", "GetFreeSpace", "[drive string]",            "<i64>","Espace libre (C: D: T:)", 3},
  }},
  {"MaraIO", {
    {"MaraIO***GetTouch***",    "GetTouch",    "[]",                        "<ptr>", "Dernier evenement tactile", 3},
    {"MaraIO***GetStylus***",   "GetStylus",   "[]",                        "<ptr>", "Etat stylet EMR (4096 niveaux de pression)", 3},
    {"MaraIO***GetSteps***",    "GetSteps",    "[]",                        "<i32>", "Nombre de pas du podometre", 3},
    {"MaraIO***ResetSteps***",  "ResetSteps",  "[]",                        "<i32>", "Remet le compteur de pas a zero", 3},
    {"MaraIO***GetAltitude***", "GetAltitude", "[]",                        "<i32>", "Altitude en metres (barometre)", 3},
    {"MaraIO***SetTorch***",    "SetTorch",    "[level i32]",               "<i32>", "Torche 0-100%%", 3},
    {"MaraIO***CapturePhoto***","CapturePhoto","[]",                        "<ptr>", "Capture une photo (camera 2MP)", 3},
    {"MaraIO***GetCrown***",    "GetCrown",    "[]",                        "<i32>", "Etat de la couronne physique", 3},
    {"MaraIO***Vibrate***",     "Vibrate",     "[durationMs i32, intensity i32]","<i32>","Vibration haptique", 3},
  }},
  {"MaraNet", {
    {"MaraNet***Init***",        "Init",        "[apn string, iccid string]","<i32>", "Initialise la stack reseau", 3},
    {"MaraNet***Connect***",     "Connect",     "[]",                        "<i32>", "Connexion au meilleur reseau (WiFi>LTE>LoRa)", 3},
    {"MaraNet***Disconnect***",  "Disconnect",  "[]",                        "<i32>", "Ferme la connexion reseau", 3},
    {"MaraNet***GetSignal***",   "GetSignal",   "[]",                        "<i32>", "Force du signal 0-100", 3},
    {"MaraNet***GetActiveNet***","GetActiveNet","[]",                        "<i32>", "Type reseau actif (0=none 1=LTE 2=WiFi 3=BT 4=LoRa)", 3},
    {"MaraNet***IsConnected***", "IsConnected", "[]",                        "<bool>","Connecte au reseau", 3},
    {"MaraNet***SetPowerSave***","SetPowerSave","[enabled i32]",            "<i32>", "Mode economie energie (bascule vers LoRa)", 3},
  }},
  {"MaraCrypto", {
    {"MaraCrypto***GenerateKey***","GenerateKey","[name string, algo string]","<i32>","Genere et stocke une cle (AES-256-GCM / ECDSA-P256 / HMAC-SHA256)", 3},
    {"MaraCrypto***Encrypt***",   "Encrypt",    "[keyName string, data ptr, size i32]","<ptr>","Chiffrement AES-256-GCM", 3},
    {"MaraCrypto***Hash***",      "Hash",       "[data ptr, size i32]",      "<ptr>", "Hachage SHA-256 (32 octets)", 3},
    {"MaraCrypto***HMAC***",      "HMAC",       "[keyName string, data ptr, size i32]","<ptr>","HMAC-SHA256", 3},
  }},
  {"MaraProc", {
    {"MaraProc***Spawn***",       "Spawn",      "[bundlePath string, prio i32]","<i32>","Lance un bundle .marep (retourne PID)", 3},
    {"MaraProc***Kill***",        "Kill",       "[pid i32]",                 "<i32>", "Arrete un processus par PID", 3},
    {"MaraProc***GetState***",    "GetState",   "[pid i32]",                 "<i32>", "Etat: 0=none 1=starting 2=running 3=paused 4=stopping 5=dead", 3},
    {"MaraProc***ListRunning***", "ListRunning","[]",                        "<array>","PIDs de tous les processus en cours", 3},
  }},
  {"MaraGrphclCpeAPI", {
    {"MaraGrphclCpeAPI***Init***",        "Init",        "[]",              "<i32>", "Initialise l'API graphique", 3},
    {"MaraGrphclCpeAPI***Shutdown***",    "Shutdown",    "[]",              "<i32>", "Libere les ressources graphiques", 3},
    {"MaraGrphclCpeAPI***SetColorSpace***","SetColorSpace","[space string]","<i32>", "Espace de couleur (sRGB/AdobeRGB/DCI-P3/Rec2020/DisplayP3)", 3},
    {"MaraGrphclCpeAPI***GetColorSpace***","GetColorSpace","[]",            "<string>","Espace de couleur courant", 3},
    {"MaraGrphclCpeAPI***SetHDR***",      "SetHDR",      "[enabled i32]",  "<i32>", "Active/desactive le HDR", 3},
    {"MaraGrphclCpeAPI***LoadICCProfile***","LoadICCProfile","[path string]","<i32>","Charge un profil ICC depuis un fichier", 3},
  }},
  {"KeyReg", {
    {"KeyReg***AddKey***",     "AddKey",     "[name string, key ptr, algo string]","<i32>","Ajoute une cle au registre", 3},
    {"KeyReg***GetKey***",     "GetKey",     "[name string]",  "<ptr>", "Recupere une cle active", 3},
    {"KeyReg***GetKeyAlgo***", "GetKeyAlgo", "[name string]",  "<string>","Algorithme de la cle", 3},
    {"KeyReg***RotateKey***",  "RotateKey",  "[name string, newKey ptr]","<i32>","Rotation de cle", 3},
    {"KeyReg***RevokeKey***",  "RevokeKey",  "[name string]",  "<i32>", "Revoque une cle", 3},
    {"KeyReg***RemoveKey***",  "RemoveKey",  "[name string]",  "<i32>", "Supprime definitivement", 3},
    {"KeyReg***KeyExists***",  "KeyExists",  "[name string]",  "<bool>","Verifie existence", 3},
    {"KeyReg***Count***",      "Count",      "[]",             "<i32>", "Nombre de cles", 3},
  }},
  {"SRIDAtt", {
    {"SRIDAtt***Generate***", "Generate", "[processName string, timestamp string]","<i32>","Genere le SRID du processus courant", 3},
    {"SRIDAtt***Validate***", "Validate", "[srid string]",  "<bool>","Verifie un SRID externe", 3},
    {"SRIDAtt***GetSRID***",  "GetSRID",  "[]",             "<string>","Retourne le SRID courant", 3},
    {"SRIDAtt***Lock***",     "Lock",     "[]",             "<i32>", "Verrouille le SRID", 3},
  }},
  {"PAccessAuth", {
    {"PAccessAuth***Authenticate***", "Authenticate", "[level i32, caKey string]","<i32>","Authentifie le processus (niveau 1=user 2=system)", 3},
    {"PAccessAuth***IsAuthorized***", "IsAuthorized", "[level i32]",  "<bool>","Verifie si le token autorise le niveau", 3},
    {"PAccessAuth***GetToken***",     "GetToken",     "[]",           "<string>","Token d'acces courant", 3},
    {"PAccessAuth***GetAccessLevel***","GetAccessLevel","[]",         "<i32>", "Niveau d'acces courant (0-3)", 3},
    {"PAccessAuth***Refresh***",      "Refresh",      "[]",           "<i32>", "Renouvelle le token", 3},
    {"PAccessAuth***Logout***",       "Logout",       "[]",           "<i32>", "Revoque l'authentification", 3},
  }},
};

// Types exportes par module (pour hover et type completions)
static const std::unordered_map<std::string, std::string> TypeModuleMap = {
  {"ComponentView",    "std***core***self"},
  {"RenderContext",    "std***core***self"},
  {"TextLabel",        "MaratineKit***UI***TextLabel"},
  {"AppLifecycle",     "std***core***self"},
  {"CycleActivity",    "std***core***self"},
  {"RegistrationRoot", "std***core***self***RegistrationRoot"},
  {"CaseComponentActivity","std***core***self"},
  {"PIDActivity",      "MaratineKit***PIDActivity"},
  {"AuthARoot",        "MaratineKit"},
  {"KeyReg",           "std***core***self***RegistrationRoot"},
  {"SRIDAtt",          "std***core***self***RegistrationRoot"},
  {"PAccessAuth",      "std***core***self***RegistrationRoot"},
  {"MaraMem",          "std***mem"},
  {"MaraFS",           "std***fs"},
  {"MaraIO",           "std***io"},
  {"MaraNet",          "std***net"},
  {"MaraCrypto",       "std***crypto"},
  {"MaraProc",         "std***proc"},
  {"MaraGrphclCpeAPI", "std***ComTpe***MarepFrmt"},
  {"ArchitectureInfo", "std***gcc***arch"},
  {"MGC",              "MaratineKit***MGC"},
  {"MGCScene",         "MaratineKit***MGC"},
  {"MGCPhysics",       "MaratineKit***MGC"},
  {"MGCAudio",         "MaratineKit***MGC"},
  {"MGCMesh",          "MaratineKit***MGC"},
  {"MGCRenderer",      "MaratineKit***MGC"},
  {"MGCShader",        "MaratineKit***MGC"},
  {"MGCTexture",       "MaratineKit***MGC"},
  {"MGCCompute",       "MaratineKit***MGC"},
  {"MGCParticle",      "MaratineKit***MGC"},
};

// ---------------------------------------------------------------------------
// Parser les imports du document (#base <...>)
// ---------------------------------------------------------------------------

static std::vector<std::string> parseImports(const std::string &DocText) {
  std::vector<std::string> Keys;
  for (const auto &Raw : splitLines(DocText)) {
    std::string L = trimLeft(Raw);
    if (L.rfind("#base <", 0) != 0 && L.rfind("#include <", 0) != 0) continue;
    size_t lt = L.find('<');
    size_t gt = L.rfind('>');
    if (lt == std::string::npos || gt == std::string::npos || gt <= lt) continue;
    std::string path = L.substr(lt + 1, gt - lt - 1);
    // Extraire la cle principale (premier token avant ***  ou [ )
    size_t sep   = path.find("***");
    size_t bracket = path.find('[');
    size_t end   = std::min(sep, bracket);
    std::string key = (end == std::string::npos) ? path : path.substr(0, end);
    while (!key.empty() && key.back() == ' ') key.pop_back();
    if (!key.empty()) Keys.push_back(key);

    // Extraire aussi les noms de membres entre [ ]
    if (bracket != std::string::npos) {
      size_t rb = path.find(']', bracket);
      if (rb != std::string::npos) {
        std::string members = path.substr(bracket + 1, rb - bracket - 1);
        std::istringstream SS(members);
        std::string tok;
        while (std::getline(SS, tok, ',')) {
          std::string m = trimLeft(tok);
          while (!m.empty() && m.back() == ' ') m.pop_back();
          if (!m.empty()) Keys.push_back(m);
        }
      }
    }
  }
  return Keys;
}

// ---------------------------------------------------------------------------
// Completion items
// ---------------------------------------------------------------------------

// Completions depuis les modules importes dans le document
static json::Array buildModuleCompletionItems(const std::string &DocText) {
  json::Array Result;
  auto ImportedKeys = parseImports(DocText);
  int Idx = 0;

  // Ajouter les symboles de chaque module importe
  for (const auto &Key : ImportedKeys) {
    auto It = ModuleRegistry.find(Key);
    if (It == ModuleRegistry.end()) continue;
    for (const auto &Exp : It->second) {
      std::string Md = "**" + Exp.display + "**  `" + Exp.ret + "`\n\n";
      Md += "```mara\n<" + Exp.ffiPath + ">(" + Exp.params.substr(1, Exp.params.size()-2) + ")\n```\n\n";
      Md += Exp.desc + "\n\n*Module:* `" + Key + "`";

      Result.push_back(json::Object{
        {"label",         Exp.ffiPath},
        {"kind",          Exp.lspKind},
        {"detail",        Exp.ret + "  " + Exp.desc.substr(0, 45)},
        {"documentation", json::Object{{"kind","markdown"},{"value",Md}}},
        {"insertText",    Exp.ffiPath},
        {"filterText",    Exp.display + " " + Exp.ffiPath},
        {"sortText",      "0" + std::to_string(Idx++)},
      });
    }
  }
  return Result;
}

// Completions de base : mots-cles + types + variables locales
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
// Hover — mot-cle, type connu ou symbole du registre
// ---------------------------------------------------------------------------

static std::optional<std::string> hoverForFFIPath(const std::string &Word) {
  // Cherche un symbole dont le ffiPath ou display contient le mot
  for (const auto &[modKey, exports] : ModuleRegistry) {
    for (const auto &Exp : exports) {
      // Match sur la derniere partie du chemin (ex: "New" dans "RenderContext***New")
      size_t P = Exp.ffiPath.rfind("***");
      std::string last = (P != std::string::npos) ? Exp.ffiPath.substr(P+3) : Exp.ffiPath;
      if (last == Word || Exp.display == Word ||
          Exp.ffiPath == Word || Exp.ffiPath.rfind(Word) != std::string::npos) {
        std::string Md = "**" + Exp.display + "**\n\n";
        Md += "```mara\nlet result: " + Exp.ret + " = <" + Exp.ffiPath + ">(";
        // Format params sans crochets
        if (Exp.params.size() >= 2)
          Md += Exp.params.substr(1, Exp.params.size()-2);
        Md += ");\n```\n\n";
        Md += Exp.desc + "\n\n*Module :* `" + modKey + "`";
        return Md;
      }
    }
  }
  return {};
}

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
  // Cherche dans TypeModuleMap d'abord
  auto It = TypeModuleMap.find(TypeName);
  if (It != TypeModuleMap.end())
    return "#base <" + It->second + "***[ " + TypeName + " ]>;";
  // Fallback: cherche un export dont le display ou ffiPath finit par TypeName
  for (const auto &[modKey, exports] : ModuleRegistry) {
    for (const auto &Exp : exports) {
      size_t P = Exp.ffiPath.rfind("***");
      std::string last = (P != std::string::npos) ? Exp.ffiPath.substr(P+3) : Exp.ffiPath;
      if (last == TypeName)
        return "#base <" + Exp.ffiPath + ">;";
    }
  }
  return {};
}

static std::string typeOriginModuleLegacy(const std::string &TypeName) {
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
  (void)TypeName; (void)Map; // unused — replaced by TypeModuleMap
  return {};
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
// Arbre des chemins d'import connus (hierarchie std***core***self***...)
// ---------------------------------------------------------------------------

struct ImportNode {
  std::string path;      // chemin complet: "std***core***self"
  std::string fullLine;  // "#base <std***core***self***[ ... ]>;"
  std::string desc;
  std::vector<std::string> members; // types dans [ ]
};

static const std::vector<ImportNode> ImportTree = {
  // Top-level
  {"MaratineKit", "#base <MaratineKit>;",
   "Runtime Maratine — RenderContext, UI, auth, timers, string utils", {}},
  {"std",         "",
   "Librairie standard Mara", {}},
  {"gcc",         "", "Toolchain GCC (architecture info)", {}},
  // std***
  {"std***core",  "", "Noyau stdlib", {}},
  {"std***mem",   "#base <std***mem***[ MaraMem ]>;",
   "Gestionnaire memoire HEAP/STACK/DMA/SECURE", {"MaraMem"}},
  {"std***fs",    "#base <std***fs***[ MaraFS ]>;",
   "Systeme de fichiers SDC (C: D: T:)", {"MaraFS"}},
  {"std***io",    "#base <std***io***[ MaraIO ]>;",
   "I/O — tactile, stylet, capteurs, camera, torche", {"MaraIO"}},
  {"std***net",   "#base <std***net***[ MaraNet ]>;",
   "Stack reseau LTE/WiFi/LoRa", {"MaraNet"}},
  {"std***crypto","#base <std***crypto***[ MaraCrypto ]>;",
   "Crypto AES-256-GCM, SHA-256, HMAC, TRNG", {"MaraCrypto"}},
  {"std***proc",  "#base <std***proc***[ MaraProc ]>;",
   "Gestionnaire de processus Slura", {"MaraProc"}},
  {"std***ComTpe","", "Types de composants", {}},
  // std***core***
  {"std***core***self", "",
   "Types UI et cycle de vie", {}},
  {"std***core***types","#base <std***core***types***[ MaraTypes ]>;",
   "Types fondamentaux et utilitaires", {"MaraTypes"}},
  // std***core***self***
  {"std***core***self***[ ComponentView, RenderContext ]",
   "#base <std***core***self***[ ComponentView, RenderContext ]>;",
   "Types UI de base",
   {"ComponentView","RenderContext"}},
  {"std***core***self***[ ComponentView, RenderContext, TextLabel ]",
   "#base <std***core***self***[ ComponentView, RenderContext, TextLabel ]>;",
   "Types UI + TextLabel",
   {"ComponentView","RenderContext","TextLabel"}},
  {"std***core***self***[ AppLifecycle ]",
   "#base <std***core***self***[ AppLifecycle ]>;",
   "Cycle de vie application .marep", {"AppLifecycle"}},
  {"std***core***self***[ CycleActivity ]",
   "#base <std***core***self***[ CycleActivity ]>;",
   "Cycle de vie driver .slul", {"CycleActivity"}},
  {"std***core***self***[ RegistrationRoot, CaseComponentActivity ]",
   "#base <std***core***self***[ RegistrationRoot, CaseComponentActivity ]>;",
   "Securite + enregistrement bundle",
   {"RegistrationRoot","CaseComponentActivity"}},
  {"std***core***self***RegistrationRoot",
   "", "Registre de securite", {}},
  {"std***core***self***RegistrationRoot***[ KeyReg, SRIDAtt, PAccessAuth ]",
   "#base <std***core***self***RegistrationRoot***[ KeyReg, SRIDAtt, PAccessAuth ]>;",
   "Cles + SRID + authentification processus",
   {"KeyReg","SRIDAtt","PAccessAuth"}},
  // std***ComTpe***
  {"std***ComTpe***MarepFrmt",
   "", "Format MAREP — composants graphiques", {}},
  {"std***ComTpe***MarepFrmt***[ MaraGrphclCpeAPI ]",
   "#base <std***ComTpe***MarepFrmt***[ MaraGrphclCpeAPI ]>;",
   "API graphique bas niveau — colorimetrie, HDR, ICC",
   {"MaraGrphclCpeAPI"}},
  // MaratineKit***MGC
  {"MaratineKit***MGC***[ MGC, MGCScene, MGCMesh, MGCPhysics, MGCParticle, MGCAudio ]",
   "#base <MaratineKit***MGC***[ MGC, MGCScene, MGCMesh, MGCPhysics, MGCParticle, MGCAudio ]>;",
   "Moteur 3D MGC complet",
   {"MGC","MGCScene","MGCMesh","MGCPhysics","MGCParticle","MGCAudio"}},
  {"MaratineKit***MGC***[ MGC ]",    "#base <MaratineKit***MGC***[ MGC ]>;",    "Facade MGC Init/Tick/Shutdown", {"MGC"}},
  {"MaratineKit***MGC***[ MGCCore ]","#base <MaratineKit***MGC***[ MGCCore ]>;","Pipeline GPU frame loop", {"MGCCore"}},
  {"MaratineKit***MGC***[ MGCScene ]","#base <MaratineKit***MGC***[ MGCScene ]>;","Scene graph 4096 entites", {"MGCScene"}},
  {"MaratineKit***MGC***[ MGCPhysics ]","#base <MaratineKit***MGC***[ MGCPhysics ]>;","Corps rigides, vehicules", {"MGCPhysics"}},
  {"MaratineKit***MGC***[ MGCAudio ]","#base <MaratineKit***MGC***[ MGCAudio ]>;","HRTF 3D, AAC, reverb", {"MGCAudio"}},
  {"MaratineKit***MGC***[ MGCShader ]","#base <MaratineKit***MGC***[ MGCShader ]>;","GLSL->SPIR-V, hot-reload", {"MGCShader"}},
  {"MaratineKit***MGC***[ MGCRenderer ]","#base <MaratineKit***MGC***[ MGCRenderer ]>;","PBR deferred+forward, SSAO", {"MGCRenderer"}},
  {"MaratineKit***MGC***[ MGCParticle ]","#base <MaratineKit***MGC***[ MGCParticle ]>;","1M particules GPU", {"MGCParticle"}},
  // gcc***
  {"gcc***arch***[ ArchitectureInfo ]",
   "#base <std***gcc***arch***[ ArchitectureInfo ]>;",
   "Info architecture cible (ARM64/X64)", {"ArchitectureInfo"}},
};

// Completions hierarchiques pour les imports — montre le NIVEAU SUIVANT seulement
static json::Array buildImportLevelCompletions(const std::string &PathPrefix) {
  std::map<std::string, const ImportNode*> NextLevel;

  for (const auto &Node : ImportTree) {
    if (Node.path.rfind(PathPrefix, 0) != 0) continue;
    std::string rest = Node.path.substr(PathPrefix.size());
    if (rest.empty()) { // Exact match — propose les membres si [ ]
      if (!Node.members.empty() && !Node.fullLine.empty()) {
        // Propose chaque membre individuellement
        for (const auto &M : Node.members) {
          if (!NextLevel.count(M)) NextLevel[M] = &Node;
        }
      }
      continue;
    }
    if (!PathPrefix.empty() && rest[0] != '*') continue; // pas un sous-chemin direct
    std::string skipStar = rest;
    while (skipStar.rfind("***", 0) == 0) skipStar = skipStar.substr(3);
    size_t sep = skipStar.find("***");
    size_t bracket = skipStar.find('[');
    size_t end = std::min(sep, bracket);
    std::string nextSeg = (end == std::string::npos) ? skipStar : skipStar.substr(0, end);
    while (!nextSeg.empty() && nextSeg.back() == ' ') nextSeg.pop_back();
    if (!nextSeg.empty() && !NextLevel.count(nextSeg)) NextLevel[nextSeg] = &Node;
  }

  json::Array Result;
  int Idx = 0;
  for (const auto &[seg, node] : NextLevel) {
    bool isLeaf = !node->fullLine.empty();
    std::string insertText = isLeaf ? ("#base <" + node->path + ">;") : (seg + "***");
    std::string detail     = isLeaf ? ("→ " + node->desc) : ("→ " + seg + "***...");
    std::string docMd      = "**`" + seg + "`**\n\n" + node->desc;
    if (isLeaf) docMd += "\n\n```mara\n" + node->fullLine + "\n```";
    Result.push_back(json::Object{
      {"label",         seg},
      {"kind",          isLeaf ? 9 : 4}, // 9=Module 4=Enum(path node)
      {"detail",        detail},
      {"documentation", json::Object{{"kind","markdown"},{"value",docMd}}},
      {"insertText",    isLeaf ? node->fullLine : (seg + "***")},
      {"sortText",      std::to_string(Idx++)},
    });
  }
  return Result;
}

// ---------------------------------------------------------------------------
// Completions FFI hierarchiques — niveau par niveau de chemin ***
// Gere les prefixes partiels : "MaratineKit***UI***Text" → montre TextLabel
// ---------------------------------------------------------------------------

static json::Array buildFFILevelCompletions(const std::string &PathPrefix) {
  // Séparer la partie complète (se termine par ***) du segment partiel
  std::string CompletePart;
  std::string PartialSeg;

  if (PathPrefix.empty()) {
    // Rien de tape — montrer tous les modules racine
    CompletePart = "";
    PartialSeg   = "";
  } else {
    size_t lastStar = PathPrefix.rfind("***");
    if (lastStar != std::string::npos) {
      CompletePart = PathPrefix.substr(0, lastStar + 3); // inclut le ***
      PartialSeg   = PathPrefix.substr(lastStar + 3);    // segment partiel apres
    } else {
      // Pas de *** → c'est un segment partiel de premier niveau
      CompletePart = "";
      PartialSeg   = PathPrefix;
    }
  }

  auto prefixMatch = [](const std::string &seg, const std::string &partial) {
    if (partial.empty()) return true;
    if (seg.size() < partial.size()) return false;
    for (size_t i = 0; i < partial.size(); ++i)
      if (tolower((unsigned char)seg[i]) != tolower((unsigned char)partial[i]))
        return false;
    return true;
  };

  std::map<std::string, const MaraExport*> NextLevel;

  for (const auto &[modKey, exports] : ModuleRegistry) {
    // Modules racine quand aucune partie complete
    if (CompletePart.empty()) {
      if (prefixMatch(modKey, PartialSeg) && !NextLevel.count(modKey))
        NextLevel[modKey] = nullptr;
    }

    for (const auto &Exp : exports) {
      const std::string &fp = Exp.ffiPath;
      // Le chemin doit commencer par la partie complete
      if (fp.rfind(CompletePart, 0) != 0) continue;
      std::string rest = fp.substr(CompletePart.size());
      if (rest.empty()) continue;
      if (rest.rfind("***", 0) == 0) rest = rest.substr(3);
      if (rest.empty()) continue;
      size_t sep    = rest.find("***");
      bool   isLeaf = (sep == std::string::npos);
      std::string nextSeg = isLeaf ? rest : rest.substr(0, sep);
      if (nextSeg.empty()) continue;
      // Filtrer par le segment partiel
      if (!prefixMatch(nextSeg, PartialSeg)) continue;
      if (!NextLevel.count(nextSeg) || isLeaf)
        NextLevel[nextSeg] = isLeaf ? &Exp : nullptr;
    }
  }

  json::Array Result;
  int Idx = 0;
  for (const auto &[seg, exp] : NextLevel) {
    if (exp) {
      // Feuille — vraie fonction avec signature complete
      std::string paramsFmt;
      if (exp->params.size() >= 2) paramsFmt = exp->params.substr(1, exp->params.size()-2);
      std::string docMd = "**`" + seg + "`** → `" + exp->ret + "`\n\n";
      docMd += "```mara\n<" + exp->ffiPath + ">(" + paramsFmt + ")\n```\n\n";
      docMd += exp->desc;
      Result.push_back(json::Object{
        {"label",         seg},
        {"kind",          3},
        {"detail",        exp->ret + "  " + exp->params},
        {"documentation", json::Object{{"kind","markdown"},{"value",docMd}}},
        {"insertText",    seg},
        {"filterText",    seg},
        {"labelDetails",  json::Object{
          {"detail",      exp->params},
          {"description", exp->ret},
        }},
        {"sortText",      std::to_string(Idx++)},
      });
    } else {
      // Noeud intermediaire — il y a d'autres niveaux apres
      Result.push_back(json::Object{
        {"label",         seg},
        {"kind",          9},
        {"detail",        "→ " + CompletePart + seg + "***..."},
        {"insertText",    seg + "***"},
        {"sortText",      std::to_string(Idx++)},
      });
    }
  }
  return Result;
}

// ---------------------------------------------------------------------------
// Signature Help — parametres actifs comme rust-analyzer
// ---------------------------------------------------------------------------

static json::Value buildSignatureHelp(const std::string &FFIPath, int ActiveParam) {
  for (const auto &[modKey, exports] : ModuleRegistry) {
    for (const auto &Exp : exports) {
      if (Exp.ffiPath != FFIPath && Exp.ffiPath != FFIPath + "***") continue;
      // Construire les parametres
      json::Array Params;
      std::string paramsFmt;
      if (Exp.params.size() >= 2) {
        paramsFmt = Exp.params.substr(1, Exp.params.size()-2);
        std::istringstream SS(paramsFmt);
        std::string tok;
        while (std::getline(SS, tok, ',')) {
          std::string p = trimLeft(tok);
          while (!p.empty() && p.back() == ' ') p.pop_back();
          // "name type" → split last space
          size_t sp = p.rfind(' ');
          std::string pname = (sp != std::string::npos) ? p.substr(0, sp) : p;
          std::string ptype = (sp != std::string::npos) ? p.substr(sp+1) : "";
          std::string doc = pname + (ptype.empty() ? "" : (": <" + ptype + ">"));
          Params.push_back(json::Object{
            {"label",         p},
            {"documentation", json::Object{{"kind","markdown"},{"value","**" + doc + "**"}}},
          });
        }
      }
      std::string label = "<" + Exp.ffiPath + ">(" + paramsFmt + ") → " + Exp.ret;
      return json::Object{
        {"signatures", json::Array{json::Object{
          {"label",         label},
          {"documentation", json::Object{{"kind","markdown"},{"value",Exp.desc}}},
          {"parameters",    std::move(Params)},
        }}},
        {"activeSignature", 0},
        {"activeParameter", ActiveParam},
      };
    }
  }
  return nullptr;
}

// ---------------------------------------------------------------------------
// Detection de contexte au curseur (comme rust-analyzer)
// ---------------------------------------------------------------------------

enum class TypingCtx {
  FFI_PATH,      // <MaratineKit***UI***...
  IMPORT_PATH,   // #base <std***core***...
  FUNC_ARGS,     // <Path***Fn***>(arg1, ...
  GENERAL,
};

struct CursorCtx {
  TypingCtx kind = TypingCtx::GENERAL;
  std::string pathPrefix;  // chemin tape jusqu'ici
  std::string ffiFullPath; // pour FUNC_ARGS
  int activeParam = 0;
};

static CursorCtx detectCursorContext(const std::string &LineText, int Col) {
  CursorCtx Ctx;
  if (LineText.empty()) return Ctx;
  std::string prefix = (Col <= (int)LineText.size()) ? LineText.substr(0, Col) : LineText;
  std::string trimmed = trimLeft(prefix);

  // 1. Import path: ligne commence par #base ou #include
  if (trimmed.rfind("#base", 0) == 0 || trimmed.rfind("#include", 0) == 0) {
    size_t lt = prefix.rfind('<');
    if (lt != std::string::npos) {
      Ctx.kind = TypingCtx::IMPORT_PATH;
      Ctx.pathPrefix = prefix.substr(lt + 1);
      // Supprimer le '[' et ce qui suit si present
      size_t br = Ctx.pathPrefix.find('[');
      if (br != std::string::npos) Ctx.pathPrefix = Ctx.pathPrefix.substr(0, br);
      while (!Ctx.pathPrefix.empty() && Ctx.pathPrefix.back() == ' ')
        Ctx.pathPrefix.pop_back();
    }
    return Ctx;
  }

  // 2. Args de fonction: trouver dernier '(' non ferme
  {
    int depth = 0;
    int lastOpen = -1;
    for (int i = (int)prefix.size()-1; i >= 0; --i) {
      if (prefix[i] == ')') depth++;
      else if (prefix[i] == '(') {
        if (depth == 0) { lastOpen = i; break; }
        depth--;
      }
    }
    if (lastOpen >= 0) {
      // Compter les virgules depuis lastOpen
      int pIdx = 0;
      for (int i = lastOpen+1; i < (int)prefix.size(); ++i)
        if (prefix[i] == ',') pIdx++;
      // Trouver le chemin FFI avant '('
      size_t closeAng = prefix.rfind('>', (size_t)lastOpen);
      if (closeAng != std::string::npos) {
        size_t openAng = prefix.rfind('<', closeAng);
        if (openAng != std::string::npos) {
          Ctx.kind = TypingCtx::FUNC_ARGS;
          Ctx.ffiFullPath = prefix.substr(openAng+1, closeAng-openAng-1);
          // Nettoyer "()" inline si present
          size_t pp = Ctx.ffiFullPath.rfind('(');
          if (pp != std::string::npos) Ctx.ffiFullPath = Ctx.ffiFullPath.substr(0, pp);
          while (!Ctx.ffiFullPath.empty() && Ctx.ffiFullPath.back() == ' ')
            Ctx.ffiFullPath.pop_back();
          Ctx.activeParam = pIdx;
          return Ctx;
        }
      }
    }
  }

  // 3. Chemin FFI: trouve le dernier '<' non ferme
  {
    int depth = 0;
    int lastLt = -1;
    for (int i = (int)prefix.size()-1; i >= 0; --i) {
      if (prefix[i] == '>') depth++;
      else if (prefix[i] == '<') {
        if (depth == 0) { lastLt = i; break; }
        depth--;
      }
    }
    if (lastLt >= 0) {
      std::string inner = prefix.substr(lastLt + 1);
      // '<' seul OU chemin commencant par lettre/_
      // Ne pas confondre avec les annotations de type <[string X]>
      bool isTypeAnnot = (!inner.empty() && inner[0] == '[');
      if (!isTypeAnnot) {
        Ctx.kind = TypingCtx::FFI_PATH;
        Ctx.pathPrefix = inner; // peut etre vide (<), partiel (<Mar) ou complet (<MaratineKit***)
        return Ctx;
      }
    }
  }

  return Ctx;
}

// ---------------------------------------------------------------------------
// Inlay hints : type retour sur les appels FFI et type des variables
// ---------------------------------------------------------------------------

static json::Array buildInlayHintsForDoc(const std::string &DocText) {
  json::Array Hints;
  auto Lines = splitLines(DocText);

  for (int LN = 0; LN < (int)Lines.size(); ++LN) {
    const std::string &Raw = Lines[LN];
    std::string L = trimLeft(Raw);

    // Chercher <FFIPath***>( patterns pour afficher le type de retour
    size_t pos = 0;
    while ((pos = Raw.find('<', pos)) != std::string::npos) {
      // Verifier que ce n'est pas une annotation de type
      if (pos > 0) {
        std::string before = trimLeft(Raw.substr(0, pos));
        if (before.rfind("let ", 0) == 0 || before.rfind("var ", 0) == 0) {
          // Annotation de type explicite — pas besoin d'un hint
          pos++;
          continue;
        }
      }
      // Trouver '>' correspondant
      size_t gt = std::string::npos;
      int depth = 0;
      for (size_t i = pos; i < Raw.size(); ++i) {
        if (Raw[i] == '<') depth++;
        else if (Raw[i] == '>') { depth--; if (depth == 0) { gt = i; break; } }
      }
      if (gt == std::string::npos) { pos++; continue; }
      std::string path = Raw.substr(pos+1, gt-pos-1);
      // Retirer "()" inline
      size_t pp = path.rfind('(');
      if (pp != std::string::npos) path = path.substr(0, pp);
      while (!path.empty() && path.back() == ' ') path.pop_back();

      // Chercher dans le registre
      for (const auto &[modKey, exports] : ModuleRegistry) {
        for (const auto &Exp : exports) {
          if (Exp.ffiPath == path) {
            // Verifier qu'il y a '(' apres '>'
            size_t nextNonSpace = gt + 1;
            while (nextNonSpace < Raw.size() && Raw[nextNonSpace] == ' ') nextNonSpace++;
            if (nextNonSpace < Raw.size() && Raw[nextNonSpace] == '(') {
              // Chercher la fin ')' sur la meme ligne ou plusieurs lignes
              // Ajouter hint apres la parenthese fermante
              size_t closeP = Raw.find(')', nextNonSpace);
              size_t hintCol = (closeP != std::string::npos) ? closeP + 1 : gt + 1;
              Hints.push_back(json::Object{
                {"position", json::Object{{"line",LN},{"character",(int)hintCol}}},
                {"label",    ": " + Exp.ret},
                {"kind",     1}, // 1=Type
                {"paddingLeft", true},
                {"tooltip", json::Object{{"kind","markdown"},
                  {"value","**Retour** `" + Exp.ret + "`\n\n" + Exp.desc}}},
              });
            }
            break;
          }
        }
      }
      pos = gt + 1;
    }
  }
  return Hints;
}

// ---------------------------------------------------------------------------
// Detection de contexte import (ligne courante, disque ou cache)
// ---------------------------------------------------------------------------

static std::string getLineText(const std::string &FPath,
                               const std::string &DocCached,
                               int Line) {
  // Utilise le cache si disponible, sinon lit le fichier depuis le disque
  const std::string *Src = nullptr;
  std::string DiskContent;
  if (!DocCached.empty()) {
    Src = &DocCached;
  } else if (auto Buf = MemoryBuffer::getFile(FPath)) {
    DiskContent = Buf.get()->getBuffer().str();
    Src = &DiskContent;
  }
  if (!Src) return {};
  auto Lines = splitLines(*Src);
  if (Line < 0 || Line >= (int)Lines.size()) return {};
  return Lines[Line];
}

static bool isImportContext(const std::string &LineText, int Col) {
  std::string prefix = (Col <= (int)LineText.size())
      ? LineText.substr(0, Col) : LineText;
  std::string t = trimLeft(prefix);
  return (t.rfind("#base", 0) == 0 || t.rfind("#include", 0) == 0);
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
      // Extraire rootUri du workspace pour trouver base/
      std::string WorkspaceRoot;
      if (Params) {
        if (auto RootUri = Params->getString("rootUri")) {
          WorkspaceRoot = uriToPath(RootUri->str());
        } else if (auto *WF = Params->getArray("workspaceFolders")) {
          if (!WF->empty()) {
            if (auto *First = WF->front().getAsObject()) {
              if (auto U = First->getString("uri"))
                WorkspaceRoot = uriToPath(U->str());
            }
          }
        }
      }
      // Scanner base/ depuis le disque pour le registre dynamique
      initDynamicRegistry(MaraiExePath, WorkspaceRoot);

      sendMessage(makeResponse(Id, json::Object{
        {"capabilities", json::Object{
          {"textDocumentSync", json::Object{
            {"openClose", true},
            {"change",    1},    // Full sync
            {"save",      true},
          }},
          {"completionProvider", json::Object{
            {"triggerCharacters", json::Array{"<","*","#","\"",",","(","["}},
            {"resolveProvider",   false},
          }},
          {"signatureHelpProvider", json::Object{
            {"triggerCharacters",   json::Array{"(", ","}},
            {"retriggerCharacters", json::Array{","}},
          }},
          {"hoverProvider",    true},
          {"inlayHintProvider",true},
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
      if (auto TextS = TD->getString("text"))
        DocContents[Uri] = TextS->str();
      // Charger le contexte projet (Maraset.yaml + base/ local)
      loadProjectContext(FPath);

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

      // Recharger le contenu depuis le disque (fichier sauvegarde)
      if (auto Buf = MemoryBuffer::getFile(FPath))
        DocContents[Uri] = Buf.get()->getBuffer().str();

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
      // Recuperer le contexte curseur
      CursorCtx CCtx;
      std::string DocForCompl;
      if (Params) {
        auto *TD  = Params->getObject("textDocument");
        auto *Pos = Params->getObject("position");
        if (TD && Pos) {
          if (auto UriS2 = TD->getString("uri")) {
            int CLine = (int)Pos->getInteger("line").value_or(0);
            int CCol  = (int)Pos->getInteger("character").value_or(0);
            auto CacheIt = DocContents.find(UriS2->str());
            if (CacheIt != DocContents.end()) DocForCompl = CacheIt->second;
            else if (auto Buf = MemoryBuffer::getFile(uriToPath(UriS2->str()))) {
              DocForCompl = Buf.get()->getBuffer().str();
              DocContents[UriS2->str()] = DocForCompl;
            }
            std::string LineText = getLineText(uriToPath(UriS2->str()), DocForCompl, CLine);
            CCtx = detectCursorContext(LineText, CCol);
          }
        }
      }

      json::Array Items;

      if (CCtx.kind == TypingCtx::IMPORT_PATH) {
        // Completions depuis base/ reel sur le disque
        Items = buildDynImportCompletions(CCtx.pathPrefix);

      } else if (CCtx.kind == TypingCtx::FFI_PATH) {
        // Completions FFI depuis base/ reel + filtrage par modules importes
        auto importedKeys = DocForCompl.empty()
            ? std::vector<std::string>{}
            : parseImports(DocForCompl);
        Items = buildDynFFICompletions(CCtx.pathPrefix, importedKeys);

        // Mettre les modules importes par le fichier courant en tete de liste
        if (!DocForCompl.empty()) {
          auto importedKeys = parseImports(DocForCompl);
          std::unordered_set<std::string> importedSet(importedKeys.begin(), importedKeys.end());
          for (auto &Item : Items) {
            if (auto *Obj = Item.getAsObject()) {
              if (auto LabelStr = Obj->getString("label")) {
                if (importedSet.count(LabelStr->str()) > 0) {
                  json::Value *ST = Obj->get("sortText");
                  if (ST) *ST = json::Value("0_" + LabelStr->str());
                }
              }
            }
          }
        }

      } else {
        // Contexte general :
        // 1. Mots-cles Mara
        Items = buildCompletionItems();
        int VIdx = 0;
        if (!DocForCompl.empty()) {
          // 2. Fonctions/types des modules importes (via DynExports)
          json::Array ModItems = buildModuleCompletionItems(DocForCompl);
          for (auto &I : ModItems) Items.push_back(std::move(I));
          // 3. Variables/fonctions locales du fichier courant
          auto Syms = parseDocSymbols(DocForCompl);
          for (const auto &[Name, S] : Syms) {
            std::string Md = (S.kind == "fn")
                ? "**Fonction Mara** `" + S.detail + "`"
                : "**`" + std::string(S.kind=="let"?"let":"var") + " " + Name + ": " + S.type + "`**";
            Items.push_back(json::Object{
              {"label",         Name},
              {"kind",          S.kind == "fn" ? 3 : 6},
              {"detail",        S.type},
              {"documentation", json::Object{{"kind","markdown"},{"value",Md}}},
              {"sortText",      "2" + std::to_string(VIdx++)},
            });
          }
        }
        // 4. Composants du projet courant (autres .mara dans base/)
        if (!CurrentProject.localExports.empty()) {
          int LIdx = 0;
          for (const auto &E : CurrentProject.localExports) {
            if (!E.isPublic) continue;
            std::string paramsFmt = (E.params.size() >= 2)
                ? E.params.substr(1, E.params.size()-2) : "";
            std::string Md = "**`" + E.name + "`** (" + CurrentProject.bundleType + ")\n\n";
            if (!E.desc.empty()) Md += "*" + E.desc + "*\n\n";
            Md += "```mara\n<" + E.maraPath + ">(" + paramsFmt + ")\n```";
            StringRef stem = sys::path::stem(sys::path::filename(E.fsPath));
            Md += "\n\n*Source :* `" + stem.str() + ".mara` (projet local)";
            Items.push_back(json::Object{
              {"label",         E.name},
              {"kind",          3},
              {"detail",        "📦 " + stem.str() + " → " + E.retType},
              {"documentation", json::Object{{"kind","markdown"},{"value",Md}}},
              {"insertText",    E.name},
              {"sortText",      "1" + std::to_string(LIdx++)},
            });
          }
        }
      }

      sendMessage(makeResponse(Id, json::Object{
        {"isIncomplete", CCtx.kind == TypingCtx::FFI_PATH},
        {"items",        std::move(Items)},
      }));
      continue;
    }

    // ---- textDocument/signatureHelp -------------------------------------
    if (Method == "textDocument/signatureHelp") {
      if (!Params) { sendMessage(makeResponse(Id, nullptr)); continue; }
      auto *TD  = Params->getObject("textDocument");
      auto *Pos = Params->getObject("position");
      if (!TD || !Pos) { sendMessage(makeResponse(Id, nullptr)); continue; }
      auto UriSH = TD->getString("uri");
      if (!UriSH) { sendMessage(makeResponse(Id, nullptr)); continue; }

      int SLine = (int)Pos->getInteger("line").value_or(0);
      int SCol  = (int)Pos->getInteger("character").value_or(0);
      std::string DocSH;
      auto CIt2 = DocContents.find(UriSH->str());
      if (CIt2 != DocContents.end()) DocSH = CIt2->second;
      else if (auto Buf = MemoryBuffer::getFile(uriToPath(UriSH->str())))
        DocSH = Buf.get()->getBuffer().str();

      std::string LineText = getLineText(uriToPath(UriSH->str()), DocSH, SLine);
      CursorCtx SCtx = detectCursorContext(LineText, SCol);

      if (SCtx.kind == TypingCtx::FUNC_ARGS && !SCtx.ffiFullPath.empty()) {
        json::Value SigHelp = buildSignatureHelp(SCtx.ffiFullPath, SCtx.activeParam);
        sendMessage(makeResponse(Id, SigHelp.getAsNull() ? json::Value(nullptr) : std::move(SigHelp)));
      } else {
        sendMessage(makeResponse(Id, nullptr));
      }
      continue;
    }

    // ---- textDocument/inlayHint -----------------------------------------
    if (Method == "textDocument/inlayHint") {
      if (!Params) { sendMessage(makeResponse(Id, json::Array{})); continue; }
      auto *TD = Params->getObject("textDocument");
      if (!TD) { sendMessage(makeResponse(Id, json::Array{})); continue; }
      auto UriIH = TD->getString("uri");
      if (!UriIH) { sendMessage(makeResponse(Id, json::Array{})); continue; }

      std::string DocIH;
      auto CIt3 = DocContents.find(UriIH->str());
      if (CIt3 != DocContents.end()) DocIH = CIt3->second;
      else if (auto Buf = MemoryBuffer::getFile(uriToPath(UriIH->str())))
        DocIH = Buf.get()->getBuffer().str();

      sendMessage(makeResponse(Id, buildInlayHintsForDoc(DocIH)));
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
          DocContents[Uri] = DocText; // mettre en cache
        }
      }

      // Extraire le mot sous le curseur depuis la ligne courante
      std::string Word;
      {
        std::string LineText = getLineText(FPath, DocText, HoverLine);
        if (!LineText.empty()) {
          int ColClamped = std::min(HoverCol, (int)LineText.size());
          // Etendre a gauche
          int S = ColClamped;
          while (S > 0 && (std::isalnum((unsigned char)LineText[S-1])
                            || LineText[S-1] == '_')) S--;
          // Etendre a droite
          int E = ColClamped;
          while (E < (int)LineText.size()
                 && (std::isalnum((unsigned char)LineText[E])
                     || LineText[E] == '_')) E++;
          if (E > S) Word = LineText.substr(S, E - S);
        }
      }

      if (Word.empty()) { sendMessage(makeResponse(Id, nullptr)); continue; }

      // 0. Chercher dans le registre dynamique (base/ reel sur disque)
      if (auto Doc = hoverForDynSymbol(Word)) {
        sendMessage(makeResponse(Id, json::Object{
          {"contents", json::Object{{"kind","markdown"},{"value",*Doc}}},
        }));
        continue;
      }

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

      // 1b. Chercher dans le registre FFI (fonctions des modules)
      if (auto Doc = hoverForFFIPath(Word)) {
        sendMessage(makeResponse(Id, json::Object{
          {"contents", json::Object{{"kind","markdown"},{"value",*Doc}}},
        }));
        continue;
      }

      // 1c. Chercher le type seul dans TypeModuleMap
      {
        std::string Origin = typeOriginModule(Word);
        if (!Origin.empty()) {
          std::string Md = "**Type Mara** `" + Word + "`\n\n**Module :** `" + Origin + "`";
          sendMessage(makeResponse(Id, json::Object{
            {"contents", json::Object{{"kind","markdown"},{"value",Md}}},
          }));
          continue;
        }
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
