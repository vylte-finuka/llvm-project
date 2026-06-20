// Vyft Ltd — marai new — Creation de projet Maratine — 2026

#include "MaraiNew.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <functional>
#include <string>
#include <vector>

using namespace llvm;
using namespace marai;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

void NewResult::print(raw_ostream &OS, bool color) const {
  if (Success) {
    WithColor(OS, raw_ostream::GREEN) << "  [OK]  ";
    OS << ShortName << "." << BundleType << "  →  " << ProjectPath << "\n";
  } else {
    WithColor(OS, raw_ostream::RED) << "  [ERR] ";
    OS << ErrorMsg << "\n";
  }
}

// Copie recursive d'un dossier, en remplacant TemplateBaseName par ProjectName
static bool copyDir(const std::string &Src, const std::string &Dst,
                    const std::string &TemplateName,
                    const std::string &ProjectName,
                    std::string &ErrMsg) {
  std::error_code EC = sys::fs::create_directories(Dst);
  if (EC) { ErrMsg = "Impossible de creer " + Dst; return false; }

  std::error_code ItEC;
  sys::fs::directory_iterator It(Src, ItEC), End;
  for (; It != End && !ItEC; It.increment(ItEC)) {
    StringRef Entry = It->path();
    StringRef Name  = sys::path::filename(Entry);

    // Renommer les dossiers/fichiers contenant le nom du template
    std::string NewName = Name.str();
    size_t Pos;
    while ((Pos = NewName.find(TemplateName)) != std::string::npos)
      NewName.replace(Pos, TemplateName.size(), ProjectName);

    SmallString<256> DstEntry(Dst);
    sys::path::append(DstEntry, NewName);

    sys::fs::file_status St;
    sys::fs::status(Entry, St);

    if (St.type() == sys::fs::file_type::directory_file) {
      if (!copyDir(Entry.str(), std::string(DstEntry),
                   TemplateName, ProjectName, ErrMsg)) return false;
    } else {
      // Copier le fichier et substituer le nom du template dedans (Maraset.yaml etc.)
      if (auto Buf = MemoryBuffer::getFile(Entry)) {
        std::string Content = Buf.get()->getBuffer().str();
        // Remplacer dans le contenu
        size_t P;
        while ((P = Content.find(TemplateName)) != std::string::npos)
          Content.replace(P, TemplateName.size(), ProjectName);

        std::string DstStr(DstEntry);
        std::error_code WEC;
        raw_fd_ostream OS(DstStr, WEC, sys::fs::OF_None);
        if (WEC) { ErrMsg = "Ecriture impossible : " + DstStr; return false; }
        OS << Content;
      } else {
        std::error_code CpEC = sys::fs::copy_file(Entry, DstEntry);
        if (CpEC) { ErrMsg = "Copie impossible : " + Entry.str(); return false; }
      }
    }
  }
  return true;
}

// Trouver le dossier templates installe
static std::string findTemplatesDir(const std::string &MaraiExe) {
  // 1. Variable d'environnement MARATINE_TEMPLATES
  if (const char *E = std::getenv("MARATINE_TEMPLATES"))
    if (sys::fs::is_directory(E)) return E;

  // 2. Relatif a marai.exe (build/tools/marai/ → ../../templates/)
  SmallString<256> Dir(MaraiExe);
  sys::path::remove_filename(Dir);
  static const char *Tries[] = {
    "../../../lib/maratine/templates",
    "../../lib/maratine/templates",
    "../lib/maratine/templates",
    "templates",
    nullptr
  };
  for (int i = 0; Tries[i]; ++i) {
    SmallString<256> P(Dir);
    sys::path::append(P, Tries[i]);
    sys::fs::make_absolute(P);
    if (sys::fs::is_directory(P)) return std::string(P);
  }

  // 3. Installation standard
  for (const char *S : {
      "D:\\maratine-install\\lib\\maratine\\templates",
      "C:\\maratine-install\\lib\\maratine\\templates",
      "/usr/local/maratine/templates"}) {
    if (sys::fs::is_directory(S)) return S;
  }
  return {};
}

// ---------------------------------------------------------------------------
// createProject
// ---------------------------------------------------------------------------

NewResult marai::createProject(const std::string &ProjectSpec,
                                const NewOptions &Opts,
                                const std::string &MaraiExePath) {
  NewResult R;

  // Analyser le nom : "MyApp.marep" ou "MyDriver.slul" ou "MyApp"
  std::string ShortName = ProjectSpec;
  std::string BundleType = "marep"; // defaut : application

  if (ProjectSpec.size() > 6 &&
      ProjectSpec.substr(ProjectSpec.size() - 6) == ".marep") {
    ShortName  = ProjectSpec.substr(0, ProjectSpec.size() - 6);
    BundleType = "marep";
  } else if (ProjectSpec.size() > 5 &&
             ProjectSpec.substr(ProjectSpec.size() - 5) == ".slul") {
    ShortName  = ProjectSpec.substr(0, ProjectSpec.size() - 5);
    BundleType = "slul";
  }

  if (ShortName.empty()) {
    R.ErrorMsg = "Nom de projet invalide : '" + ProjectSpec + "'";
    return R;
  }

  // Valider le nom (pas d'espaces, pas de caracteres speciaux)
  for (char C : ShortName) {
    if (!isalnum((unsigned char)C) && C != '_' && C != '-') {
      R.ErrorMsg = "Le nom du projet ne doit contenir que des lettres, chiffres, _ ou -";
      return R;
    }
  }

  R.ShortName  = ShortName;
  R.BundleType = BundleType;

  // Trouver les templates
  std::string TemplatesDir = findTemplatesDir(MaraiExePath);
  if (TemplatesDir.empty()) {
    R.ErrorMsg = "Templates introuvables. "
                 "Installez la toolchain : .\\build-llvm-win.ps1\n"
                 "         ou definissez MARATINE_TEMPLATES=<chemin>";
    return R;
  }

  // Template source selon le type de bundle
  std::string TemplateName = "MaratineProjectAppTemplate";
  SmallString<256> TplSrc(TemplatesDir);
  sys::path::append(TplSrc, TemplateName + "." + BundleType);

  if (!sys::fs::is_directory(TplSrc)) {
    R.ErrorMsg = "Template introuvable : " + std::string(TplSrc) +
                 "\n         La toolchain est-elle correctement installee ?";
    return R;
  }

  // Destination : <OutputDir>/<ShortName>.<BundleType>/
  std::string OutDir = Opts.OutputDir.empty() ? "." : Opts.OutputDir;
  SmallString<256> DstDir(OutDir);
  sys::path::append(DstDir, ShortName + "." + BundleType);
  sys::fs::make_absolute(DstDir);

  if (sys::fs::exists(DstDir)) {
    if (!Opts.Force) {
      R.ErrorMsg = "Le dossier existe deja : " + std::string(DstDir) +
                   "\n         Utilisez --force pour ecraser.";
      return R;
    }
    sys::fs::remove_directories(DstDir);
  }

  // Copie + substitution du nom
  std::string ErrMsg;
  if (!copyDir(std::string(TplSrc), std::string(DstDir),
               TemplateName, ShortName, ErrMsg)) {
    R.ErrorMsg = ErrMsg;
    sys::fs::remove_directories(DstDir);
    return R;
  }

  // Post-traitement : mettre a jour Maraset.yaml avec le vrai nom du projet
  SmallString<256> YamlPath(DstDir);
  sys::path::append(YamlPath, "Maraset.yaml");
  if (sys::fs::exists(YamlPath)) {
    if (auto Buf = MemoryBuffer::getFile(YamlPath)) {
      std::string Content = Buf.get()->getBuffer().str();

      // Remplacer le nom du package (line: "  name: base***<OldName>")
      // et le nom du SRID dans metadata
      auto replaceLine = [&](const std::string &Prefix,
                              const std::string &NewVal) {
        size_t P = 0;
        while ((P = Content.find(Prefix, P)) != std::string::npos) {
          size_t EOL = Content.find('\n', P + Prefix.size());
          if (EOL == std::string::npos) EOL = Content.size();
          Content.replace(P + Prefix.size(), EOL - P - Prefix.size(), NewVal);
          P += Prefix.size() + NewVal.size();
        }
      };

      // package.name
      replaceLine("  name: ", "base***" + ShortName);
      // SRID dans metadata.attributes
      replaceLine("    SRID: SRID_", ShortName.empty() ? "project_001" :
                  [&]() -> std::string {
                    std::string s;
                    for (char c : ShortName)
                      s += tolower((unsigned char)c);
                    s += "_001";
                    return s;
                  }());
      // description
      replaceLine("      description: |\n        Application d",
                  "|\n        " + ShortName + " — bundle Maratine.");

      std::string YamlStr(YamlPath);
      std::error_code WEC;
      raw_fd_ostream YOS(YamlStr, WEC, sys::fs::OF_None);
      if (!WEC) YOS << Content;
    }
  }

  R.ProjectPath = std::string(DstDir);
  R.Success     = true;
  return R;
}
