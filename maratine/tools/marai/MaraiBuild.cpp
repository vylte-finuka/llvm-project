// Vyft Ltd — marai build — Proprietary — 2026

#include "MaraiBuild.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <string>
#include <vector>

using namespace llvm;
using namespace marai;

// ---------------------------------------------------------------------------
// BuildResult
// ---------------------------------------------------------------------------

void BuildResult::print(raw_ostream &OS, bool color) const {
  if (Success) {
    WithColor(OS, raw_ostream::GREEN) << "  [OK]  ";
    OS << OutputPackage << "\n";
  } else {
    WithColor(OS, raw_ostream::RED) << "  [ERR] ";
    OS << ProjectPath << "\n";
    if (!ErrorMsg.empty())
      OS << "         " << ErrorMsg << "\n";
  }
}

// ---------------------------------------------------------------------------
// Builder
// ---------------------------------------------------------------------------

Builder::Builder(const BuildOptions &Opts, const std::string &MaraiExePath)
    : Opts(Opts), MaraiExePath(MaraiExePath) {}

// ---------------------------------------------------------------------------
// resolveCompiler — find maratine-cc relative to marai or via PATH
// ---------------------------------------------------------------------------

std::string Builder::resolveCompiler() const {
  if (!Opts.CompilerOverride.empty() &&
      sys::fs::exists(Opts.CompilerOverride))
    return Opts.CompilerOverride;

  // marai lives in build/tools/marai/ → maratine-cc is in build/
  SmallString<256> CC(MaraiExePath);
  sys::path::remove_filename(CC);
  sys::path::append(CC, "..", "..", "maratine-cc.exe");
  sys::fs::make_absolute(CC);
  if (sys::fs::exists(CC)) return std::string(CC);

  // Same directory as marai
  SmallString<256> Same(MaraiExePath);
  sys::path::remove_filename(Same);
  sys::path::append(Same, "maratine-cc.exe");
  if (sys::fs::exists(Same)) return std::string(Same);

  if (auto F = sys::findProgramByName("maratine-cc")) return *F;
  return {};
}

// ---------------------------------------------------------------------------
// readManifest — minimal YAML line-scanner for Maraset.yaml
// ---------------------------------------------------------------------------

ManifestInfo Builder::readManifest(const std::string &ProjectDir) const {
  ManifestInfo Info;

  SmallString<256> YamlPath(ProjectDir);
  sys::path::append(YamlPath, "Maraset.yaml");

  auto BufOrErr = MemoryBuffer::getFile(YamlPath);
  if (!BufOrErr) return Info;

  std::string Section;
  StringRef Content = BufOrErr.get()->getBuffer();

  SmallVector<StringRef, 64> Lines;
  Content.split(Lines, '\n');

  auto trim = [](StringRef S) { return S.trim(" \t\r"); };

  for (auto &Raw : Lines) {
    StringRef Line = trim(Raw);

    // Detect top-level section changes (unindented keys ending with ':')
    if (Line == "package:")    { Section = "package";    continue; }
    if (Line == "metadata:")   { Section = "metadata";   continue; }
    if (Line == "dependencies:") { Section = "";         continue; }
    if (Line == "resources:")  { Section = "";           continue; }
    if (Line == "scripts:")    { Section = "";           continue; }
    // Any other top-level key (no leading space) resets section
    if (!Raw.empty() && Raw[0] != ' ' && Raw[0] != '\t' && Raw[0] != '#' &&
        Line.contains(':') && !Line.starts_with("-"))
      Section = "";

    if (Section == "package") {
      if (Line.starts_with("name:")) {
        Info.PackageName = trim(Line.substr(5)).str();
        auto P = Info.PackageName.rfind("***");
        Info.ShortName = (P != std::string::npos)
            ? Info.PackageName.substr(P + 3)
            : Info.PackageName;
      } else if (Line.starts_with("version:")) {
        Info.Version = trim(Line.substr(8)).str();
      }
    } else if (Section == "metadata") {
      if (Line.starts_with("bundle:"))
        Info.BundleType = trim(Line.substr(7)).str();
      if (Line.starts_with("target:"))
        Info.Target = trim(Line.substr(7)).str();
    }
  }

  // Normaliser la valeur de target
  if (Info.Target == "aarch64" || Info.Target == "arm64")
    Info.Target = "arm64";
  else if (Info.Target == "x86_64" || Info.Target == "amd64" || Info.Target == "x64")
    Info.Target = "x64";
  else
    Info.Target = "arm64"; // defaut Slura OS

  Info.Valid = !Info.ShortName.empty() && !Info.BundleType.empty();
  return Info;
}

// ---------------------------------------------------------------------------
// compileBase — compile all .mara in SrcBase → DstBase as .ovc
// ---------------------------------------------------------------------------

bool Builder::compileBase(const std::string &SrcBase,
                          const std::string &DstBase,
                          const std::string &Compiler,
                          std::string &ErrMsg,
                          const std::string &Arch) {
  std::error_code EC = sys::fs::create_directories(DstBase);
  if (EC) { ErrMsg = "Impossible de créer base/ : " + EC.message(); return false; }

  std::error_code IterEC;
  sys::fs::directory_iterator It(SrcBase, IterEC), End;
  if (IterEC) { ErrMsg = "Impossible de lire base/ : " + IterEC.message(); return false; }

  bool AnySource = false;

  for (; It != End && !IterEC; It.increment(IterEC)) {
    StringRef EntryPath = It->path();
    if (!EntryPath.ends_with(".mara")) continue;

    AnySource = true;
    StringRef Filename = sys::path::filename(EntryPath);
    SmallString<256> OutPath(DstBase);
    sys::path::append(OutPath, Filename);
    sys::path::replace_extension(OutPath, ".ovc");

    if (Opts.Verbose)
      outs() << "    Compiling " << Filename << " ...\n";

    std::vector<StringRef> Args;
    Args.push_back(Compiler);
    Args.push_back(EntryPath);
    Args.push_back("-emit");
    Args.push_back("ovc");
    Args.push_back("-o");
    Args.push_back(OutPath.str());
    if (Opts.Optimize) Args.push_back("-O");
    // Architecture cible
    std::string ArchArg = "-arch";
    std::string ArchVal = Arch.empty() ? "arm64" : Arch;
    Args.push_back(ArchArg);
    Args.push_back(ArchVal);

    std::string CompErr;
    int ExitCode = sys::ExecuteAndWait(Compiler, Args,
        std::nullopt, {}, 120, 0, &CompErr);

    if (ExitCode != 0) {
      ErrMsg = "Échec compilation " + Filename.str();
      if (!CompErr.empty()) ErrMsg += " : " + CompErr;
      return false;
    }
    if (Opts.Verbose)
      WithColor(outs(), raw_ostream::GREEN) << "    [OK] " << Filename << "\n";
  }

  if (!AnySource) {
    ErrMsg = "Aucun fichier .mara trouvé dans base/";
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// copyAssets — copy everything from ProjectDir except base/*.mara source files
// ---------------------------------------------------------------------------

bool Builder::copyAssets(const std::string &ProjectDir,
                         const std::string &BundleDir,
                         std::string &ErrMsg) {
  // Helper: copy a single file to destination directory
  auto copyFile = [&](const std::string &Src,
                      const std::string &DstDir) -> bool {
    SmallString<256> Dst(DstDir);
    sys::path::append(Dst, sys::path::filename(Src));
    std::error_code EC = sys::fs::copy_file(Src, Dst);
    if (EC) { ErrMsg = "Copie échouée pour " + Src + " : " + EC.message(); return false; }
    return true;
  };

  // Helper: recursively copy a directory
  std::function<bool(const std::string&, const std::string&)> copyDir;
  copyDir = [&](const std::string &Src, const std::string &Dst) -> bool {
    sys::fs::create_directories(Dst);
    std::error_code EC;
    sys::fs::directory_iterator It(Src, EC), End;
    for (; It != End && !EC; It.increment(EC)) {
      StringRef Entry = It->path();
      SmallString<256> DstEntry(Dst);
      sys::path::append(DstEntry, sys::path::filename(Entry));
      sys::fs::file_status St;
      sys::fs::status(Entry, St);
      if (St.type() == sys::fs::file_type::directory_file) {
        if (!copyDir(Entry.str(), DstEntry.str().str())) return false;
      } else {
        std::error_code CpEC = sys::fs::copy_file(Entry, DstEntry);
        if (CpEC) { ErrMsg = "Copie échouée : " + Entry.str(); return false; }
      }
    }
    return true;
  };

  // Scan project root
  std::error_code IterEC;
  sys::fs::directory_iterator It(ProjectDir, IterEC), End;
  if (IterEC) { ErrMsg = "Lecture projet : " + IterEC.message(); return false; }

  for (; It != End && !IterEC; It.increment(IterEC)) {
    StringRef Entry = It->path();
    StringRef Name  = sys::path::filename(Entry);

    // Skip .mara sources in base/ — those are compiled separately
    if (Name == "base") continue;

    sys::fs::file_status St;
    sys::fs::status(Entry, St);

    if (St.type() == sys::fs::file_type::directory_file) {
      // Copy entire subdirectory (slasset, res, etc.)
      SmallString<256> DstSub(BundleDir);
      sys::path::append(DstSub, Name);
      if (!copyDir(Entry.str(), DstSub.str().str())) return false;
      if (Opts.Verbose)
        outs() << "    Copié dossier " << Name << "/\n";
    } else {
      // Copy individual file (Maraset.yaml, RAbstractallowing.xml, ...)
      if (!copyFile(Entry.str(), BundleDir)) return false;
      if (Opts.Verbose)
        outs() << "    Copié " << Name << "\n";
    }
  }
  return true;
}

// ---------------------------------------------------------------------------
// createPackage — zip BundleDir/* → OutputPath using PowerShell
// ---------------------------------------------------------------------------

bool Builder::createPackage(const std::string &BundleDir,
                            const std::string &OutputPath,
                            std::string &ErrMsg) {
  // Find PowerShell
  auto PSOrErr = sys::findProgramByName("powershell");
  if (!PSOrErr) { ErrMsg = "PowerShell introuvable"; return false; }

  // Remove previous output if it exists
  sys::fs::remove(OutputPath);

  // Sanitize path for embedding in a PowerShell single-quoted string:
  // replace \ with / and escape ' as '' (PowerShell single-quote escape).
  auto toPS = [](const std::string &S) -> std::string {
    std::string R;
    R.reserve(S.size() + 8);
    for (char C : S) {
      if (C == '\\')      R += '/';
      else if (C == '\'') R += "''"; // '' = literal ' inside PS single-quoted string
      else                R += C;
    }
    return R;
  };

  std::string Cmd =
      "Add-Type -A 'System.IO.Compression.FileSystem'; "
      "[IO.Compression.ZipFile]::CreateFromDirectory('"
      + toPS(BundleDir) + "', '"
      + toPS(OutputPath) + "')";

  std::vector<StringRef> Args;
  Args.push_back(*PSOrErr);
  Args.push_back("-NonInteractive");
  Args.push_back("-Command");
  Args.push_back(Cmd);

  std::string PSErr;
  int ExitCode = sys::ExecuteAndWait(*PSOrErr, Args,
      std::nullopt, {}, 120, 0, &PSErr);

  if (ExitCode != 0 || !sys::fs::exists(OutputPath)) {
    ErrMsg = "Packaging échoué (code=" + std::to_string(ExitCode) + ")";
    if (!PSErr.empty()) ErrMsg += " : " + PSErr;
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// buildProject — orchestrate build for one project directory
// ---------------------------------------------------------------------------

BuildResult Builder::buildProject(const std::string &ProjectDir,
                                  const ManifestInfo &Info,
                                  const std::string &Compiler) {
  BuildResult R;
  R.ProjectPath = ProjectDir;

  // Determine output path
  std::string OutDir = Opts.OutputDir.empty() ? "." : Opts.OutputDir;
  if (!Opts.OutputFile.empty()) {
    R.OutputPackage = Opts.OutputFile;
  } else {
    SmallString<256> Out(OutDir);
    sys::path::append(Out, Info.ShortName + "." + Info.BundleType);
    sys::fs::make_absolute(Out);
    R.OutputPackage = std::string(Out);
  }

  // Create unique temp directory
  SmallString<256> TmpDir;
  if (auto EC = sys::fs::createUniqueDirectory("marai-build", TmpDir)) {
    R.ErrorMsg = "Impossible de créer tmp dir : " + EC.message();
    return R;
  }

  // Bundle dir inside temp
  SmallString<256> BundleDir(TmpDir);
  sys::path::append(BundleDir, Info.ShortName +
      (Info.BundleType == "slul" ? ".slul" : ".marep"));
  sys::fs::create_directories(BundleDir);

  // Compile .mara → .ovc into temp/bundle/base/
  SmallString<256> SrcBase(ProjectDir);
  sys::path::append(SrcBase, "base");
  SmallString<256> DstBase(BundleDir);
  sys::path::append(DstBase, "base");

  // Architecture : priorité à l'option CLI, sinon Maraset.yaml, sinon arm64
  std::string EffectiveArch = Opts.Arch.empty() ? Info.Target : Opts.Arch;
  if (EffectiveArch.empty()) EffectiveArch = "arm64";

  if (Opts.Verbose)
    outs() << "  Compilation de " << sys::path::filename(ProjectDir)
           << "  [" << EffectiveArch << "]\n";
  else
    outs() << "  Architecture : " << EffectiveArch << "\n";

  std::string Err;
  if (!compileBase(SrcBase.str().str(), DstBase.str().str(), Compiler, Err, EffectiveArch)) {
    R.ErrorMsg = Err;
    sys::fs::remove_directories(TmpDir);
    return R;
  }

  // Copy assets (manifests, icons, slasset, ...)
  if (!copyAssets(ProjectDir, BundleDir.str().str(), Err)) {
    R.ErrorMsg = Err;
    sys::fs::remove_directories(TmpDir);
    return R;
  }

  // Ensure output directory exists
  sys::fs::create_directories(sys::path::parent_path(R.OutputPackage));

  // Package
  if (!createPackage(BundleDir.str().str(), R.OutputPackage, Err)) {
    R.ErrorMsg = Err;
    sys::fs::remove_directories(TmpDir);
    return R;
  }

  // Cleanup temp
  sys::fs::remove_directories(TmpDir);

  R.Success = true;
  return R;
}

// ---------------------------------------------------------------------------
// Auto-detection du projet depuis le repertoire courant
// Remonte l'arborescence jusqu'a trouver Maraset.yaml (comme cargo build)
// OU scanne le dossier courant pour des sous-dossiers .marep / .slul
// ---------------------------------------------------------------------------

static std::vector<std::string> autoDetectProjects() {
  std::vector<std::string> Found;

  SmallString<256> Cwd;
  if (sys::fs::current_path(Cwd)) return Found;

  // 1. Remonter depuis le dossier courant pour trouver Maraset.yaml
  SmallString<256> Dir(Cwd);
  for (int Depth = 0; Depth < 6; ++Depth) {
    SmallString<256> Yaml(Dir);
    sys::path::append(Yaml, "Maraset.yaml");
    if (sys::fs::exists(Yaml)) {
      Found.push_back(std::string(Dir));
      return Found;
    }
    SmallString<256> Parent(Dir);
    sys::path::remove_filename(Parent);
    if (Parent == Dir) break; // racine du systeme
    Dir = Parent;
  }

  // 2. Scanner le dossier courant pour des projets .marep / .slul
  std::error_code EC;
  sys::fs::directory_iterator It(std::string(Cwd), EC), End;
  for (; It != End && !EC; It.increment(EC)) {
    StringRef Entry = It->path();
    sys::fs::file_status St;
    sys::fs::status(Entry, St);
    if (St.type() != sys::fs::file_type::directory_file) continue;

    // Verifier si c'est un .marep ou .slul avec Maraset.yaml
    StringRef Name = sys::path::filename(Entry);
    if (!Name.ends_with(".marep") && !Name.ends_with(".slul")) continue;

    SmallString<256> Yaml(Entry);
    sys::path::append(Yaml, "Maraset.yaml");
    if (sys::fs::exists(Yaml))
      Found.push_back(std::string(Entry));
  }

  return Found;
}

// ---------------------------------------------------------------------------
// build — public entry point
// ---------------------------------------------------------------------------

std::vector<BuildResult>
Builder::build(const std::vector<std::string> &Projects) {
  std::vector<BuildResult> Results;

  // Si aucun projet specifie, detecter depuis le dossier courant
  std::vector<std::string> Targets = Projects;
  if (Targets.empty()) {
    Targets = autoDetectProjects();
    if (Targets.empty()) {
      BuildResult R;
      R.ProjectPath = ".";
      R.ErrorMsg = "Aucun projet .marep / .slul trouve dans le dossier courant "
                   "ou ses parents.\n"
                   "         Positionnez-vous dans un dossier contenant Maraset.yaml\n"
                   "         ou passez le chemin : marai build MonApp.marep";
      Results.push_back(std::move(R));
      return Results;
    }
    outs() << "  Projet detecte automatiquement : " << Targets.size()
           << " bundle(s)\n\n";
  }

  std::string Compiler = resolveCompiler();
  if (Compiler.empty()) {
    BuildResult R;
    R.ErrorMsg = "maratine-cc introuvable. Utilisez --compiler <chemin>.";
    R.ProjectPath = "(compiler)";
    Results.push_back(std::move(R));
    return Results;
  }
  if (Opts.Verbose)
    outs() << "  Compilateur : " << Compiler << "\n\n";

  for (const auto &P : Targets) {
    if (!sys::fs::is_directory(P)) {
      BuildResult R;
      R.ProjectPath = P;
      R.ErrorMsg = "n'est pas un repertoire de projet (.marep/.slul)";
      Results.push_back(std::move(R));
      continue;
    }

    ManifestInfo Info = readManifest(P);
    if (!Info.Valid) {
      BuildResult R;
      R.ProjectPath = P;
      R.ErrorMsg = "Maraset.yaml manquant ou invalide (champs name/bundle requis)";
      Results.push_back(std::move(R));
      continue;
    }

    outs() << "  Projet : " << sys::path::filename(P)
           << "  ->  " << Info.ShortName << "." << Info.BundleType << "\n";

    Results.push_back(buildProject(P, Info, Compiler));
  }
  return Results;
}
