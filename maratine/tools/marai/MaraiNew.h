// Vyft Ltd — marai new — Creation de projet Maratine — 2026

#pragma once

#include "llvm/Support/raw_ostream.h"
#include <string>

using namespace llvm;

namespace marai {

struct NewOptions {
  std::string OutputDir;    // --dir (defaut : dossier courant)
  bool        Force = false;// --force : ecrase si le dossier existe
};

struct NewResult {
  std::string ProjectPath;  // chemin cree
  std::string BundleType;   // "marep" ou "slul"
  std::string ShortName;    // "HelloWorld"
  bool        Success = false;
  std::string ErrorMsg;

  void print(raw_ostream &OS, bool color) const;
};

// projectSpec = "NewApp.marep" ou "NewDriver.slul" ou "NewApp" (defaut marep)
NewResult createProject(const std::string &ProjectSpec,
                        const NewOptions &Opts,
                        const std::string &MaraiExePath);

} // namespace marai
