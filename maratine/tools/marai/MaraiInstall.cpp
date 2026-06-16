//===-- MaraiInstall.cpp – Package Installer ------------------------------===//
//
// Maratine Package Manager — marai
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
//===----------------------------------------------------------------------===//

#include "MaraiInstall.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

using namespace llvm;
using namespace marai;

//----------------------------------------------------------------------
// InstallResult::print
//----------------------------------------------------------------------

void InstallResult::print(raw_ostream &OS, bool Color) const {
  auto ok  = [&](StringRef s) { if (Color) WithColor(OS, raw_ostream::GREEN)  << s; else OS << s; };
  auto err = [&](StringRef s) { if (Color) WithColor(OS, raw_ostream::RED)    << s; else OS << s; };
  auto wrn = [&](StringRef s) { if (Color) WithColor(OS, raw_ostream::YELLOW) << s; else OS << s; };

  switch (Status) {
  case InstallStatus::Success:
    ok("  ✓ "); OS << PackageName << "@" << InstalledVersion << "\n";
    break;
  case InstallStatus::AlreadyInstalled:
    OS << "  = " << PackageName << "@" << InstalledVersion << " (déjà installé)\n";
    break;
  case InstallStatus::ABIConflict:
    wrn("  ⚠ "); OS << PackageName << " : conflit ABI ignoré (--force)\n";
    ABIResult.print(OS);
    break;
  case InstallStatus::ABIBreaking:
    err("  ✗ "); OS << PackageName << " : breaking change ABI bloqué\n";
    ABIResult.print(OS);
    OS << "    Utilisez --force pour forcer l'installation.\n";
    break;
  case InstallStatus::NotFound:
    err("  ✗ "); OS << PackageName << " : introuvable dans le registry.\n";
    break;
  case InstallStatus::HashMismatch:
    err("  ✗ "); OS << PackageName << " : intégrité compromise (SHA-256 invalide).\n";
    break;
  case InstallStatus::DependencyFailed:
    err("  ✗ "); OS << PackageName << " : résolution des dépendances échouée.\n";
    break;
  default:
    err("  ✗ "); OS << PackageName << " : erreur — " << ErrorMsg << "\n";
  }

  for (const auto &W : Warnings)
    OS << "    ! " << W << "\n";
  for (const auto &D : InstalledDeps)
    OS << "      + " << D << "\n";
}

//----------------------------------------------------------------------
// Installer::install
//----------------------------------------------------------------------

std::vector<InstallResult> Installer::install(
    const std::vector<std::string> &Specs) {
  std::vector<InstallResult> results;

  for (const auto &Spec : Specs) {
    InstallResult R;
    R.PackageName = Spec;

    // 1. Résoudre le package
    PackageMeta meta = resolve(Spec);
    if (meta.Name.empty()) {
      R.Status = InstallStatus::NotFound;
      results.push_back(R);
      continue;
    }
    R.InstalledVersion = meta.Version;

    // 2. Vérifier ABI MABI 1.0.1
    R.ABIResult = checkABI(meta);
    if (R.ABIResult.isBlocking() && !Opts.Force) {
      R.Status = InstallStatus::ABIBreaking;
      results.push_back(R);
      continue;
    }
    if (R.ABIResult.isBlocking() && Opts.Force) {
      R.Status = InstallStatus::ABIConflict;
      R.Warnings.push_back("Breaking ABI change ignoré via --force.");
    }

    // 3. Télécharger et vérifier SHA-256
    if (!Opts.DryRun) {
      std::string archive = download(meta);
      if (archive.empty()) {
        R.Status = InstallStatus::HashMismatch;
        results.push_back(R);
        continue;
      }

      // 4. Extraire et installer
      InstallStatus s = extract(archive, meta);
      if (s != InstallStatus::Success && s != InstallStatus::AlreadyInstalled) {
        R.Status = s;
        results.push_back(R);
        continue;
      }
    }

    // 5. Résoudre et installer les dépendances
    auto deps = resolveDeps(meta);
    for (const auto &D : deps)
      R.InstalledDeps.push_back(D.Name + "@" + D.ResolvedVersion);

    R.Status = Opts.DryRun ? InstallStatus::Success : InstallStatus::Success;
    results.push_back(R);
  }

  if (!Opts.DryRun) writeLockfile(results);
  return results;
}

//----------------------------------------------------------------------
// Stubs — implémentation réelle via registry HTTP
//----------------------------------------------------------------------

PackageMeta Installer::resolve(StringRef Spec) const {
  // TODO: GET {RegistryURL}/v1/packages/{name}/{version}
  // Parser le JSON de réponse → PackageMeta
  return {};
}

std::string Installer::download(const PackageMeta &Meta) const {
  // TODO: télécharger Meta.DownloadURL → fichier temporaire
  // Vérifier SHA-256 contre Meta.SHA256
  return {};
}

InstallStatus Installer::extract(const std::string &ArchivePath,
                                  const PackageMeta &Meta) {
  // TODO: décompresser .marpkg (tar.zst) dans Opts.InstallDir
  return InstallStatus::Success;
}

ABICheckResult Installer::checkABI(const PackageMeta &Meta) const {
  std::string installed = Opts.InstallDir + "/" + Meta.Name + "/.mabi";
  std::string required  = "./.mabi/" + Meta.Name;
  return ABIChecker::check(installed, required);
}

std::vector<PackageDep> Installer::resolveDeps(const PackageMeta &Meta) const {
  // TODO: SAT solver sur l'arbre de dépendances
  return Meta.Dependencies;
}

void Installer::writeLockfile(const std::vector<InstallResult> &Results) const {
  // TODO: écrire Maralock.yaml avec name/version/SHA256 de chaque package
}

std::vector<InstallResult> Installer::update(StringRef LockfilePath) {
  // TODO: parser Maralock.yaml, résoudre les nouvelles versions, réinstaller
  return {};
}

bool Installer::remove(StringRef PackageName, raw_ostream &OS) {
  // TODO: supprimer le répertoire du package + mettre à jour Maralock.yaml
  OS << "  Suppression de " << PackageName << "…\n";
  return true;
}

void Installer::list(raw_ostream &OS, bool ShowDeps) const {
  // TODO: parser Maralock.yaml et afficher les packages
  OS << "  (aucun package installé)\n";
}

bool Installer::publish(StringRef PackagePath, raw_ostream &OS) {
  // TODO: créer l'archive .marpkg, calculer SHA-256, POST vers le registry
  OS << "  Publication de " << PackagePath << "…\n";
  return true;
}
