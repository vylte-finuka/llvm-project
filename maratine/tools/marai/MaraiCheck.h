// Vyft Ltd — marai check — Audit CLI du compilateur Maratine — 2026

#pragma once

#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace llvm;

namespace marai {

struct CheckOptions {
  bool Verbose       = false;
  bool Optimize      = false;  // -O : audit avec optimisation O2
  bool ShowIR        = false;  // --show-ir : affiche l'IR LLVM de chaque OVC
  bool JSONOutput    = false;  // --json
  std::string CompilerOverride;
};

// Resultat d'un check sur un fichier .mara
struct FileCheckResult {
  std::string SourceFile;

  // Compilation
  bool CompileOK      = false;
  bool VerifierOK     = false; // IR valide apres O2 ?
  std::string CompileError;

  // Metriques IR
  int  FunctionCount  = 0;     // nombre de define dans l'IR
  int  FFICount       = 0;     // nombre de declare (symboles externes)
  bool HasEntryPoint  = false; // @OEntry / @APrevent present
  bool HasDeadCode    = false; // instructions apres terminateur detectees
  bool HasEmptyModule = false; // module vide (0 fonctions)

  std::vector<std::string> ExportedFunctions; // @fn avec ExternalLinkage
  std::vector<std::string> PrivateFunctions;  // define internal
  std::vector<std::string> FFIDependencies;   // declare @...
  std::vector<std::string> Warnings;
  std::vector<std::string> Errors;

  bool ok() const {
    return CompileOK && VerifierOK && !HasEmptyModule && Errors.empty();
  }
};

// Resultat global pour un projet
struct ProjectCheckResult {
  std::string ProjectPath;
  std::string BundleType;    // "marep" ou "slul"
  std::string PackageName;

  std::vector<FileCheckResult> Files;
  int Pass = 0, Fail = 0, Warn = 0;

  bool ok() const { return Fail == 0; }
  void print(raw_ostream &OS, bool color) const;
  void printJSON(raw_ostream &OS) const;
};

class Checker {
public:
  Checker(const CheckOptions &Opts, const std::string &MaraiExePath);

  std::vector<ProjectCheckResult> check(const std::vector<std::string> &Projects);

private:
  CheckOptions Opts;
  std::string  MaraiExePath;

  std::string resolveCompiler() const;

  ProjectCheckResult checkProject(const std::string &ProjectDir,
                                  const std::string &Compiler);

  FileCheckResult checkFile(const std::string &SrcPath,
                            const std::string &Compiler,
                            const std::string &BundleType);

  void analyseIR(const std::string &IRText,
                 FileCheckResult &R,
                 const std::string &BundleType);
};

} // namespace marai
