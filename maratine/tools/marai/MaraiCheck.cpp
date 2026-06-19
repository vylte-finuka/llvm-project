// Vyft Ltd — marai check — Audit CLI du compilateur Maratine — 2026

#include "MaraiCheck.h"
#include "MaraiBuild.h"   // readManifest / resolveCompiler logic
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

using namespace llvm;
using namespace marai;

// ---------------------------------------------------------------------------
// Helpers
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

// ---------------------------------------------------------------------------
// ProjectCheckResult::print
// ---------------------------------------------------------------------------

void ProjectCheckResult::print(raw_ostream &OS, bool color) const {
  WithColor(OS, raw_ostream::CYAN, true)
      << "\n=== Audit : " << sys::path::filename(ProjectPath)
      << " [" << BundleType << "] ===\n\n";

  for (const auto &F : Files) {
    StringRef fname = sys::path::filename(F.SourceFile);

    if (F.ok()) {
      WithColor(OS, raw_ostream::GREEN) << "  [PASS] ";
    } else {
      WithColor(OS, raw_ostream::RED) << "  [FAIL] ";
    }
    OS << fname;

    if (F.ok()) {
      OS << "  (" << F.FunctionCount << " fn, "
         << F.FFICount << " FFI";
      if (F.HasEntryPoint) OS << ", entry-point";
      OS << ")\n";
    } else {
      OS << "\n";
    }

    for (const auto &E : F.Errors)
      WithColor(OS, raw_ostream::RED) << "    [ERR]  " << E << "\n";
    for (const auto &W : F.Warnings)
      WithColor(OS, raw_ostream::YELLOW) << "    [WARN] " << W << "\n";

    if (!F.ExportedFunctions.empty()) {
      OS << "    Exports : ";
      for (size_t I = 0; I < F.ExportedFunctions.size(); ++I) {
        if (I) OS << ", ";
        OS << "@" << F.ExportedFunctions[I];
      }
      OS << "\n";
    }
    if (!F.PrivateFunctions.empty()) {
      OS << "    Prive   : ";
      for (size_t I = 0; I < F.PrivateFunctions.size(); ++I) {
        if (I) OS << ", ";
        OS << "@" << F.PrivateFunctions[I];
      }
      OS << "\n";
    }
    if (!F.FFIDependencies.empty()) {
      OS << "    FFI dep : ";
      for (size_t I = 0; I < F.FFIDependencies.size(); ++I) {
        if (I) OS << ", ";
        OS << F.FFIDependencies[I];
      }
      OS << "\n";
    }
  }

  OS << "\n  Resultat : ";
  if (ok()) {
    WithColor(OS, raw_ostream::GREEN, true) << Pass << " PASS";
  } else {
    WithColor(OS, raw_ostream::RED, true) << Fail << " FAIL";
    OS << "  ";
    WithColor(OS, raw_ostream::GREEN) << Pass << " PASS";
  }
  if (Warn) {
    OS << "  ";
    WithColor(OS, raw_ostream::YELLOW) << Warn << " WARN";
  }
  OS << "\n";
}

void ProjectCheckResult::printJSON(raw_ostream &OS) const {
  OS << "{\n";
  OS << "  \"project\": \"" << ProjectPath << "\",\n";
  OS << "  \"bundle\": \"" << BundleType << "\",\n";
  OS << "  \"pass\": " << Pass << ",\n";
  OS << "  \"fail\": " << Fail << ",\n";
  OS << "  \"warn\": " << Warn << ",\n";
  OS << "  \"ok\": " << (ok() ? "true" : "false") << ",\n";
  OS << "  \"files\": [\n";
  for (size_t I = 0; I < Files.size(); ++I) {
    const auto &F = Files[I];
    OS << "    {\n";
    OS << "      \"source\": \"" << F.SourceFile << "\",\n";
    OS << "      \"compile_ok\": " << (F.CompileOK ? "true" : "false") << ",\n";
    OS << "      \"verifier_ok\": " << (F.VerifierOK ? "true" : "false") << ",\n";
    OS << "      \"functions\": " << F.FunctionCount << ",\n";
    OS << "      \"ffi\": " << F.FFICount << ",\n";
    OS << "      \"entry_point\": " << (F.HasEntryPoint ? "true" : "false") << ",\n";
    OS << "      \"dead_code\": " << (F.HasDeadCode ? "true" : "false") << ",\n";
    OS << "      \"empty_module\": " << (F.HasEmptyModule ? "true" : "false") << ",\n";

    auto jsonArray = [&](const std::vector<std::string> &V, const std::string &Key) {
      OS << "      \"" << Key << "\": [";
      for (size_t J = 0; J < V.size(); ++J) {
        if (J) OS << ", ";
        OS << "\"" << V[J] << "\"";
      }
      OS << "]";
    };

    jsonArray(F.ExportedFunctions, "exports");    OS << ",\n";
    jsonArray(F.PrivateFunctions,  "private");    OS << ",\n";
    jsonArray(F.FFIDependencies,   "ffi_deps");   OS << ",\n";
    jsonArray(F.Errors,            "errors");     OS << ",\n";
    jsonArray(F.Warnings,          "warnings");   OS << "\n";
    OS << "    }" << (I + 1 < Files.size() ? "," : "") << "\n";
  }
  OS << "  ]\n}\n";
}

// ---------------------------------------------------------------------------
// Checker
// ---------------------------------------------------------------------------

Checker::Checker(const CheckOptions &Opts, const std::string &MaraiExePath)
    : Opts(Opts), MaraiExePath(MaraiExePath) {}

std::string Checker::resolveCompiler() const {
  if (!Opts.CompilerOverride.empty() && sys::fs::exists(Opts.CompilerOverride))
    return Opts.CompilerOverride;

  SmallString<256> CC(MaraiExePath);
  sys::path::remove_filename(CC);
  sys::path::append(CC, "..", "..", "maratine-cc.exe");
  sys::fs::make_absolute(CC);
  if (sys::fs::exists(CC)) return std::string(CC);

  SmallString<256> Same(MaraiExePath);
  sys::path::remove_filename(Same);
  sys::path::append(Same, "maratine-cc.exe");
  if (sys::fs::exists(Same)) return std::string(Same);

  if (auto F = sys::findProgramByName("maratine-cc")) return *F;
  return {};
}

// ---------------------------------------------------------------------------
// analyseIR — parse le texte LLVM IR et remplit FileCheckResult
// ---------------------------------------------------------------------------

void Checker::analyseIR(const std::string &IRText,
                        FileCheckResult &R,
                        const std::string &BundleType) {
  auto Lines = splitLines(IRText);

  // Regex patterns
  static const std::string defExternal = "^define\\s+i";
  static const std::string defInternal = "^define\\s+internal\\s+i";
  static const std::string decl        = "^declare\\s+";
  static const std::string retInstr    = "\\bret\\b";
  static const std::string termInstr   =
      "\\b(ret|br|switch|indirectbr|invoke|callbr|resume|unreachable)\\b";

  bool insideFunc = false;
  bool prevWasTerm = false;

  for (const auto &Raw : Lines) {
    std::string L = trimLeft(Raw);

    // --- Function definitions ---
    if (L.rfind("define ", 0) == 0) {
      insideFunc = true;
      prevWasTerm = false;
      R.FunctionCount++;

      // Extract name: define [internal] i32 @Name(
      size_t At = L.find('@');
      size_t Paren = L.find('(', At);
      if (At != std::string::npos && Paren != std::string::npos) {
        std::string FnName = L.substr(At + 1, Paren - At - 1);
        bool isInternal = strContains(L, "internal");

        if (isInternal) R.PrivateFunctions.push_back(FnName);
        else            R.ExportedFunctions.push_back(FnName);

        // Entry-point detection
        if (FnName == "OEntry" || FnName == "APrevent")
          R.HasEntryPoint = true;
      }
      continue;
    }

    // --- Declarations (FFI / external) ---
    if (L.rfind("declare ", 0) == 0 && !strContains(L, "@printf")) {
      size_t At = L.find('@');
      size_t Paren = L.find('(', At);
      if (At != std::string::npos && Paren != std::string::npos) {
        std::string Sym = L.substr(At + 1, Paren - At - 1);
        R.FFICount++;
        if (R.FFIDependencies.size() < 16) // cap display at 16
          R.FFIDependencies.push_back(Sym);
      }
      continue;
    }

    if (!insideFunc) continue;

    if (L == "}") { insideFunc = false; prevWasTerm = false; continue; }

    // --- Dead code detection (instruction after terminator) ---
    if (prevWasTerm && !L.empty() && L[0] != '}' &&
        L.rfind("define ", 0) != 0 && L.rfind("declare ", 0) != 0 &&
        L.rfind("@", 0) != 0 && L.rfind(";", 0) != 0 &&
        L.rfind("label ", 0) != 0 && L.find(':') == std::string::npos) {
      // Instruction after a terminator = dead code
      R.HasDeadCode = true;
    }

    // Check if current line is a terminator instruction
    // Simple heuristic: starts with ret / br (after optional %var = )
    std::string stripped = L;
    size_t eq = L.find('=');
    if (eq != std::string::npos) stripped = trimLeft(L.substr(eq + 1));

    prevWasTerm = (stripped.rfind("ret ", 0) == 0 ||
                   stripped == "ret" ||
                   stripped.rfind("br ", 0) == 0 ||
                   stripped.rfind("unreachable", 0) == 0);
  }

  R.HasEmptyModule = (R.FunctionCount == 0);

  // Static analysis — results reported here; verifier result set by checkFile Step 3.
  if (R.HasEmptyModule)
    R.Errors.push_back("Module vide : aucune fonction Mara generee");
  if (R.HasDeadCode)
    R.Warnings.push_back("Code mort detecte (instructions apres terminateur)");
}

// ---------------------------------------------------------------------------
// checkFile — compile un fichier .mara et analyse l'IR
// ---------------------------------------------------------------------------

FileCheckResult Checker::checkFile(const std::string &SrcPath,
                                   const std::string &Compiler,
                                   const std::string &BundleType) {
  FileCheckResult R;
  R.SourceFile = SrcPath;

  // Step 1: compile to LLVM IR (no optimization — raw IR)
  SmallString<256> IRPath;
  sys::fs::createTemporaryFile("marai-check", "ll", IRPath);

  std::vector<StringRef> Args;
  Args.push_back(Compiler);
  Args.push_back(SrcPath);
  Args.push_back("-emit");
  Args.push_back("llvm");
  Args.push_back("-o");
  Args.push_back(IRPath.str());

  std::string CompErr;
  int ExitRaw = sys::ExecuteAndWait(Compiler, Args,
      std::nullopt, {}, 60, 0, &CompErr);

  if (ExitRaw != 0) {
    R.CompileOK   = false;
    R.VerifierOK  = false;
    R.CompileError = CompErr.empty()
        ? ("exit code " + std::to_string(ExitRaw)) : CompErr;
    sys::fs::remove(IRPath);
    R.Errors.push_back("Echec compilation : " + R.CompileError);
    return R;
  }
  R.CompileOK = true;

  // Step 2: read IR text for static analysis
  std::string IRText;
  if (auto Buf = MemoryBuffer::getFile(IRPath)) {
    IRText = Buf.get()->getBuffer().str();
    analyseIR(IRText, R, BundleType);
  }

  // Step 3: compile WITH -O to run built-in LLVM verifier
  SmallString<256> OVCPath;
  sys::fs::createTemporaryFile("marai-check-opt", "ovc", OVCPath);

  std::vector<StringRef> OptArgs;
  OptArgs.push_back(Compiler);
  OptArgs.push_back(SrcPath);
  OptArgs.push_back("-emit");
  OptArgs.push_back("ovc");
  OptArgs.push_back("-O");
  OptArgs.push_back("-o");
  OptArgs.push_back(OVCPath.str());

  std::string OptErr;
  int ExitOpt = sys::ExecuteAndWait(Compiler, OptArgs,
      std::nullopt, {}, 60, 0, &OptErr);

  R.VerifierOK = (ExitOpt == 0);
  if (!R.VerifierOK)
    R.Errors.push_back("Verifier LLVM O2 : " + (OptErr.empty()
        ? "echec (voir sortie compilateur)" : OptErr));

  if (Opts.ShowIR && !IRText.empty()) {
    outs() << "\n  --- IR : " << sys::path::filename(SrcPath) << " ---\n"
           << IRText << "\n";
  }

  sys::fs::remove(IRPath);
  sys::fs::remove(OVCPath);
  return R;
}

// ---------------------------------------------------------------------------
// checkProject — audit d'un projet .marep ou .slul
// ---------------------------------------------------------------------------

ProjectCheckResult Checker::checkProject(const std::string &ProjectDir,
                                         const std::string &Compiler) {
  ProjectCheckResult PR;
  PR.ProjectPath = ProjectDir;

  // Read manifest
  Builder tmpBuilder(BuildOptions{}, MaraiExePath);
  ManifestInfo Info = tmpBuilder.readManifest(ProjectDir);
  PR.BundleType   = Info.Valid ? Info.BundleType : "unknown";
  PR.PackageName  = Info.Valid ? Info.ShortName   : sys::path::filename(ProjectDir).str();

  // Scan base/ for .mara files
  SmallString<256> BaseDir(ProjectDir);
  sys::path::append(BaseDir, "base");

  std::error_code EC;
  sys::fs::directory_iterator It(BaseDir, EC), End;
  if (EC) {
    ProjectCheckResult Bad;
    Bad.ProjectPath = ProjectDir;
    Bad.BundleType  = PR.BundleType;
    FileCheckResult F;
    F.SourceFile = BaseDir.str().str();
    F.Errors.push_back("Impossible de lire base/ : " + EC.message());
    Bad.Files.push_back(F);
    Bad.Fail = 1;
    return Bad;
  }

  for (; It != End && !EC; It.increment(EC)) {
    StringRef Entry = It->path();
    if (!Entry.ends_with(".mara")) continue;

    if (Opts.Verbose)
      outs() << "  Checking " << sys::path::filename(Entry) << "...\n";

    FileCheckResult FR = checkFile(Entry.str(), Compiler, PR.BundleType);
    if (FR.ok()) PR.Pass++;
    else         PR.Fail++;
    PR.Warn += (int)FR.Warnings.size();
    PR.Files.push_back(std::move(FR));
  }

  // Sort by filename for deterministic output
  std::sort(PR.Files.begin(), PR.Files.end(), [](const FileCheckResult &A, const FileCheckResult &B) {
    return sys::path::filename(A.SourceFile) < sys::path::filename(B.SourceFile);
  });

  return PR;
}

// ---------------------------------------------------------------------------
// check — public entry point
// ---------------------------------------------------------------------------

std::vector<ProjectCheckResult>
Checker::check(const std::vector<std::string> &Projects) {
  std::string Compiler = resolveCompiler();
  std::vector<ProjectCheckResult> Results;

  if (Compiler.empty()) {
    ProjectCheckResult Bad;
    Bad.ProjectPath = "(compiler)";
    FileCheckResult F;
    F.Errors.push_back("maratine-cc introuvable. Utilisez --compiler <chemin>.");
    Bad.Files.push_back(F);
    Bad.Fail = 1;
    Results.push_back(std::move(Bad));
    return Results;
  }

  if (Opts.Verbose)
    outs() << "  Compilateur : " << Compiler << "\n\n";

  for (const auto &P : Projects) {
    if (!sys::fs::is_directory(P)) {
      ProjectCheckResult Bad;
      Bad.ProjectPath = P;
      FileCheckResult F;
      F.SourceFile = P;
      F.Errors.push_back("N'est pas un repertoire de projet (.marep/.slul)");
      Bad.Files.push_back(F);
      Bad.Fail = 1;
      Results.push_back(std::move(Bad));
      continue;
    }
    Results.push_back(checkProject(P, Compiler));
  }
  return Results;
}
