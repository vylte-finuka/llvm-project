//===-- MaraiInstall.h – Package Installer --------------------------------===//
//
// Maratine Package Manager — marai
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
// Résolution, téléchargement et installation de packages Mara (.marpkg).
// Vérifie la compatibilité MABI avant installation.
// --force ignore les conflits ABI et les avertissements de version.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "MaraiABI.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace marai {

//----------------------------------------------------------------------
// Options d'installation
//----------------------------------------------------------------------
struct InstallOptions {
  bool Force       = false;  // --force : ignore conflits ABI
  bool DryRun      = false;  // --dry-run : simule sans installer
  bool Verbose     = false;  // --verbose : détail complet
  bool NoVerify    = false;  // --no-verify : skip hash check (déconseillé)
  bool GlobalScope = false;  // --global : installe globalement
  std::string RegistryURL;   // URL du registry Maratine
  std::string InstallDir;    // répertoire cible
};

//----------------------------------------------------------------------
// Dépendance d'un package
//----------------------------------------------------------------------
struct PackageDep {
  std::string Name;
  std::string VersionRange;   // ex. ">=1.2.0 <2.0.0"
  std::string ResolvedVersion;
  bool        Optional = false;
};

//----------------------------------------------------------------------
// Métadonnées d'un package
//----------------------------------------------------------------------
struct PackageMeta {
  std::string Name;
  std::string Version;
  std::string MABIRequirement;   // ex. "MABI 1.0.1"
  std::string License;
  std::string Supplier;
  std::string SHA256;
  std::string DownloadURL;
  std::vector<PackageDep> Dependencies;
  std::vector<std::string> ExposedModules;
};

//----------------------------------------------------------------------
// Résultat d'une installation
//----------------------------------------------------------------------
enum class InstallStatus {
  Success,
  ABIConflict,       // résolu avec --force
  ABIBreaking,       // breaking change — bloqué sans --force
  NotFound,          // package introuvable dans le registry
  HashMismatch,      // intégrité compromise
  DependencyFailed,  // dépendance non résoluble
  AlreadyInstalled,
  Error,
};

struct InstallResult {
  InstallStatus         Status;
  std::string           PackageName;
  std::string           InstalledVersion;
  std::vector<std::string> InstalledDeps;
  std::vector<std::string> Warnings;
  std::string           ErrorMsg;
  ABICheckResult        ABIResult;

  bool success() const {
    return Status == InstallStatus::Success ||
           Status == InstallStatus::AlreadyInstalled;
  }
  void print(llvm::raw_ostream &OS, bool Color) const;
};

//----------------------------------------------------------------------
// Installeur
//----------------------------------------------------------------------
class Installer {
public:
  explicit Installer(const InstallOptions &Opts) : Opts(Opts) {}

  // Installe un ou plusieurs packages par nom[@version]
  std::vector<InstallResult> install(const std::vector<std::string> &Specs);

  // Met à jour tous les packages du lockfile
  std::vector<InstallResult> update(llvm::StringRef LockfilePath);

  // Supprime un package
  bool remove(llvm::StringRef PackageName, llvm::raw_ostream &OS);

  // Liste les packages installés
  void list(llvm::raw_ostream &OS, bool ShowDeps) const;

  // Publie un package local vers le registry
  bool publish(llvm::StringRef PackagePath, llvm::raw_ostream &OS);

private:
  InstallOptions Opts;

  // Résout les métadonnées d'un package depuis le registry
  PackageMeta resolve(llvm::StringRef Spec) const;

  // Télécharge et vérifie l'intégrité SHA-256
  std::string download(const PackageMeta &Meta) const;

  // Extrait et installe le .marpkg dans InstallDir
  InstallStatus extract(const std::string &ArchivePath,
                         const PackageMeta &Meta);

  // Vérifie la compatibilité MABI 1.0.1
  ABICheckResult checkABI(const PackageMeta &Meta) const;

  // Résout l'arbre de dépendances (SAT solver simplifié)
  std::vector<PackageDep> resolveDeps(const PackageMeta &Meta) const;

  // Écrit le lockfile Maralock.yaml après installation
  void writeLockfile(const std::vector<InstallResult> &Results) const;
};

} // namespace marai
