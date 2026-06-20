//===-- Marai.cpp – Maratine Package Manager ─────────────────────────────===//
//
// marai — Maratine Package Manager
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
// Usage :
//   marai install <pkg[@ver]> [--force] [--dry-run] [--verbose]
//   marai update
//   marai remove <pkg>
//   marai list [--deps]
//   marai audit [--json] [--min-severity LEVEL]
//   marai audit --aude [--sbom] [--json]
//   marai publish <path>
//   marai abi check <pkg>
//   marai version
//
//===----------------------------------------------------------------------===//

#include "MaraiABI.h"
#include "MaraiAudit.h"
#include "MaraiBuild.h"
#include "MaraiCheck.h"
#include "MaraiInstall.h"
#include "MaraiLSP.h"
#include "MaraiNew.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace llvm;
using namespace marai;

//----------------------------------------------------------------------
// Définition des options CLI
//----------------------------------------------------------------------

static cl::SubCommand NewCmd     ("new",      "Creer un nouveau projet .marep ou .slul depuis un template");
static cl::SubCommand LSPCmd     ("lsp",      "Demarrer le serveur LSP Maratine (stdio JSON-RPC)");
static cl::SubCommand CheckCmd   ("check",    "Auditer les fichiers Mara d'un projet (IR + securite)");
static cl::SubCommand BuildCmd   ("build",    "Compiler un ou plusieurs fichiers .mara");
static cl::SubCommand InstallCmd ("install",  "Installer un ou plusieurs packages");
static cl::SubCommand UpdateCmd  ("update",   "Mettre à jour tous les packages");
static cl::SubCommand RemoveCmd  ("remove",   "Supprimer un package");
static cl::SubCommand ListCmd    ("list",     "Lister les packages installés");
static cl::SubCommand AuditCmd   ("audit",    "Analyser les failles de sécurité");
static cl::SubCommand PublishCmd ("publish",  "Publier un package sur le registry");
static cl::SubCommand ABICmd     ("abi",      "Opérations ABI MABI 1.0.1");
static cl::SubCommand VersionCmd ("version",  "Afficher la version de marai");

// -- check
static cl::list<std::string> CheckProjects(cl::Positional,
  cl::desc("<projet.marep|projet.slul>..."), cl::sub(CheckCmd));
static cl::opt<bool> CheckOptimize("O",
  cl::desc("Activer O2 pour la verification du verifier LLVM"), cl::sub(CheckCmd));
static cl::opt<bool> CheckVerbose("verbose",
  cl::desc("Sortie detaillee"), cl::sub(CheckCmd));
static cl::opt<bool> CheckShowIR("show-ir",
  cl::desc("Afficher l'IR LLVM de chaque fichier"), cl::sub(CheckCmd));
static cl::opt<bool> CheckJSON("json",
  cl::desc("Sortie JSON"), cl::sub(CheckCmd));
static cl::opt<std::string> CheckCompiler("compiler",
  cl::desc("Chemin vers maratine-cc (auto-detecte par defaut)"),
  cl::sub(CheckCmd));

// -- new
static cl::opt<std::string> NewProjectSpec(cl::Positional,
  cl::desc("<NomProjet[.marep|.slul]>"), cl::sub(NewCmd));
static cl::opt<std::string> NewDir("dir",
  cl::desc("Repertoire de creation (defaut : dossier courant)"),
  cl::sub(NewCmd));
static cl::opt<bool> NewForce("force",
  cl::desc("Ecraser si le dossier existe deja"), cl::sub(NewCmd));

// -- build
static cl::list<std::string> BuildSources(cl::Positional,
  cl::desc("[projet.marep|projet.slul]...  (auto-detecte si absent)"),
  cl::sub(BuildCmd));
static cl::opt<std::string> BuildOutput("o",
  cl::desc("Chemin de sortie (.marpkg) — mode projet unique"), cl::sub(BuildCmd));
static cl::opt<std::string> BuildOutDir("out-dir",
  cl::desc("Répertoire de sortie (défaut : répertoire courant)"),
  cl::sub(BuildCmd));
static cl::opt<bool> BuildOptimize("O",
  cl::desc("Activer les optimisations O2"), cl::sub(BuildCmd));
static cl::opt<bool> BuildVerbose("verbose",
  cl::desc("Sortie détaillée"), cl::sub(BuildCmd));
static cl::opt<std::string> BuildCompiler("compiler",
  cl::desc("Chemin vers maratine-cc (auto-détecté par défaut)"),
  cl::sub(BuildCmd));

// -- install
static cl::list<std::string> InstallSpecs(cl::Positional,
  cl::desc("<package[@version]>..."), cl::sub(InstallCmd));
static cl::opt<bool> Force("force",
  cl::desc("Forcer l'installation (ignore conflits ABI)"), cl::sub(InstallCmd));
static cl::opt<bool> DryRun("dry-run",
  cl::desc("Simuler sans installer"), cl::sub(InstallCmd));
static cl::opt<bool> Verbose("verbose",
  cl::desc("Sortie détaillée"), cl::sub(InstallCmd));
static cl::opt<bool> GlobalScope("global",
  cl::desc("Installation globale"), cl::sub(InstallCmd));
static cl::opt<std::string> Registry("registry",
  cl::desc("URL du registry"), cl::init("https://registry.maratine.dev"),
  cl::sub(InstallCmd));

// -- remove
static cl::opt<std::string> RemovePkg(cl::Positional,
  cl::desc("<package>"), cl::sub(RemoveCmd));

// -- list
static cl::opt<bool> ListDeps("deps",
  cl::desc("Afficher les dépendances"), cl::sub(ListCmd));

// -- audit
static cl::opt<bool> AudeMode("aude",
  cl::desc("Audit cybersec avancé niveau ingénieur"), cl::sub(AuditCmd));
static cl::opt<bool> AuditJSON("json",
  cl::desc("Sortie JSON"), cl::sub(AuditCmd));
static cl::opt<bool> AuditSBOM("sbom",
  cl::desc("Exporter le SBOM complet (avec --aude)"), cl::sub(AuditCmd));
static cl::opt<std::string> MinSeverity("min-severity",
  cl::desc("Niveau minimum : INFO LOW MEDIUM HIGH CRITICAL"),
  cl::init("LOW"), cl::sub(AuditCmd));
static cl::opt<std::string> LockfilePath("lockfile",
  cl::desc("Chemin du Maralock.yaml"), cl::init("./Maralock.yaml"),
  cl::sub(AuditCmd));

// -- publish
static cl::opt<std::string> PublishPath(cl::Positional,
  cl::desc("<package-path>"), cl::sub(PublishCmd));

// -- abi
static cl::opt<std::string> ABISubCmd(cl::Positional,
  cl::desc("check"), cl::sub(ABICmd));
static cl::opt<std::string> ABIPkg(cl::Positional,
  cl::desc("<package>"), cl::sub(ABICmd));

//----------------------------------------------------------------------
// Aide contextuelle
//----------------------------------------------------------------------

static void printHeader(raw_ostream &OS) {
  OS << "\n";
  WithColor(OS, raw_ostream::MAGENTA, true)
    << "marai — Maratine Package Manager\n";
  OS << "MABI 1.0.1  ·  Vyft Ltd  ·  2026\n\n";
}

static void printVersion(raw_ostream &OS) {
  printHeader(OS);
  OS << "marai     1.0.0\n";
  OS << "MABI      1.0.1\n";
  OS << "Registry  https://registry.maratine.dev\n\n";
}

//----------------------------------------------------------------------
// Commandes
//----------------------------------------------------------------------

static int cmdCheck(raw_ostream &OS, const std::string &Argv0) {
  if (CheckProjects.empty()) {
    WithColor::error() << "marai check : aucun projet specifie.\n";
    OS << "Usage: marai check <projet.marep|projet.slul>... [-O] [--json] [--show-ir]\n";
    return 1;
  }

  CheckOptions Opts;
  Opts.Optimize         = CheckOptimize;
  Opts.Verbose          = CheckVerbose;
  Opts.ShowIR           = CheckShowIR;
  Opts.JSONOutput       = CheckJSON;
  Opts.CompilerOverride = CheckCompiler;

  if (!CheckJSON) {
    WithColor(OS, raw_ostream::CYAN, true) << "marai check\n";
    OS << "  Analyse statique + verification IR LLVM\n";
    if (Opts.Optimize) OS << "  Mode : avec optimisation O2\n";
    OS << "\n";
  }

  Checker checker(Opts, Argv0);
  auto Results = checker.check(
      std::vector<std::string>(CheckProjects.begin(), CheckProjects.end()));

  bool hasError = false;
  for (const auto &R : Results) {
    if (Opts.JSONOutput)
      R.printJSON(OS);
    else
      R.print(OS, true);
    if (!R.ok()) hasError = true;
  }

  if (!CheckJSON) {
    OS << "\n";
    if (!hasError)
      WithColor(OS, raw_ostream::GREEN, true) << "  Audit termine : aucun probleme detecte.\n";
    else
      WithColor(OS, raw_ostream::RED, true) << "  Audit termine : des problemes ont ete detectes.\n";
    OS << "\n";
  }

  return hasError ? 1 : 0;
}

static int cmdNew(raw_ostream &OS, const std::string &Argv0) {
  if (NewProjectSpec.empty()) {
    WithColor::error() << "marai new : nom de projet requis.\n";
    OS << "Usage: marai new <NomProjet[.marep|.slul]> [--dir <chemin>] [--force]\n\n";
    OS << "Exemples :\n";
    OS << "  marai new MonApp.marep\n";
    OS << "  marai new MonDriver.slul\n";
    OS << "  marai new MonApp.marep --dir D:\\Projets\n";
    return 1;
  }

  NewOptions Opts;
  Opts.OutputDir = NewDir;
  Opts.Force     = NewForce;

  WithColor(OS, raw_ostream::CYAN, true) << "marai new\n\n";

  auto R = marai::createProject(NewProjectSpec, Opts, Argv0);
  R.print(OS, true);

  if (R.Success) {
    OS << "\n";
    WithColor(OS, raw_ostream::GREEN) << "  Projet cree avec succes.\n\n";
    OS << "  Prochaines etapes :\n";
    OS << "    cd " << R.ProjectPath << "\n";
    OS << "    marai build -O\n\n";
    OS << "  Fichiers generes :\n";
    OS << "    base/OEntry.mara      <- point d'entree\n";
    if (R.BundleType == "marep") {
      OS << "    base/LAPrevent.mara   <- cycle de vie\n";
      OS << "    base/HelloWorld.mara  <- composant exemple\n";
      OS << "    Maraset.yaml          <- manifeste du bundle\n";
      OS << "    RAbstractallowing.xml <- permissions\n";
      OS << "    *.slasset/            <- icone et layouts\n";
    } else {
      OS << "    base/APrevent.mara    <- cycle de vie driver\n";
      OS << "    Maraset.yaml          <- manifeste du driver\n";
      OS << "    RAbstractallowing.xml <- permissions\n";
    }
    return 0;
  }
  return 1;
}

static int cmdBuild(raw_ostream &OS, const std::string &Argv0) {
  // BuildSources peut etre vide — auto-detection depuis le dossier courant

  BuildOptions Opts;
  Opts.OutputFile       = BuildOutput;
  Opts.OutputDir        = BuildOutDir;
  Opts.Optimize         = BuildOptimize;
  Opts.Verbose          = BuildVerbose;
  Opts.CompilerOverride = BuildCompiler;

  WithColor(OS, raw_ostream::CYAN, true) << "marai build\n\n";

  Builder builder(Opts, Argv0);
  auto Results = builder.build(
      std::vector<std::string>(BuildSources.begin(), BuildSources.end()));

  bool hasError = false;
  for (const auto &R : Results) {
    R.print(OS, true);
    if (!R.Success) hasError = true;
  }

  if (!hasError)
    WithColor(OS, raw_ostream::GREEN) << "\nBuild réussi.\n";
  else
    WithColor(OS, raw_ostream::RED) << "\nBuild échoué.\n";

  return hasError ? 1 : 0;
}

static int cmdInstall(raw_ostream &OS) {
  if (InstallSpecs.empty()) {
    WithColor::error() << "marai install : aucun package spécifié.\n";
    OS << "Usage: marai install <pkg[@ver]>... [--force] [--dry-run]\n";
    return 1;
  }

  InstallOptions Opts;
  Opts.Force       = Force;
  Opts.DryRun      = DryRun;
  Opts.Verbose     = Verbose;
  Opts.GlobalScope = GlobalScope;
  Opts.RegistryURL = Registry;

  if (Force)
    WithColor(OS, raw_ostream::YELLOW)
      << "⚠  --force activé : les conflits ABI seront ignorés.\n\n";

  Installer installer(Opts);
  auto results = installer.install(
    std::vector<std::string>(InstallSpecs.begin(), InstallSpecs.end()));

  bool hasError = false;
  for (const auto &R : results) {
    R.print(OS, true);
    if (!R.success()) hasError = true;
  }
  return hasError ? 1 : 0;
}

static int cmdAudit(raw_ostream &OS) {
  bool color = true;

  if (AudeMode) {
    WithColor(OS, raw_ostream::MAGENTA, true)
      << "Démarrage de l'audit cybersec avancé --aude…\n\n";

    AudeReport report = AuditEngine::runAude(LockfilePath, ".", color);

    if (AuditJSON)
      report.printJSON(OS);
    else {
      report.printAude(OS, color);
      if (AuditSBOM) report.printAudeSBOM(OS);
    }

    return report.hasBlockers() ? 2 : 0;
  }

  // Audit standard
  AuditReport report = AuditEngine::run(LockfilePath, color);

  if (AuditJSON)
    report.printJSON(OS);
  else
    report.print(OS, color);

  return report.hasBlockers() ? 2 : 0;
}

static int cmdABI(raw_ostream &OS) {
  if (ABISubCmd == "check") {
    if (ABIPkg.empty()) {
      WithColor::error() << "marai abi check : package manquant.\n";
      return 1;
    }
    WithColor(OS, raw_ostream::CYAN, true)
      << "Vérification MABI 1.0.1 pour : " << ABIPkg << "\n\n";

    auto result = ABIChecker::check(
      "./" + ABIPkg + "/.mabi",
      "~/.marai/packages/" + ABIPkg + "/.mabi");
    result.print(OS);

    return result.isBlocking() ? 1 : 0;
  }

  WithColor::error() << "marai abi : sous-commande inconnue '" << ABISubCmd << "'.\n";
  OS << "Usage: marai abi check <package>\n";
  return 1;
}

static int cmdList(raw_ostream &OS) {
  InstallOptions Opts;
  Installer installer(Opts);
  installer.list(OS, ListDeps);
  return 0;
}

static int cmdRemove(raw_ostream &OS) {
  if (RemovePkg.empty()) {
    WithColor::error() << "marai remove : package manquant.\n";
    return 1;
  }
  InstallOptions Opts;
  Installer installer(Opts);
  return installer.remove(RemovePkg, OS) ? 0 : 1;
}

static int cmdUpdate(raw_ostream &OS) {
  InstallOptions Opts;
  Installer installer(Opts);
  auto results = installer.update("./Maralock.yaml");
  bool hasError = false;
  for (const auto &R : results) {
    R.print(OS, true);
    if (!R.success()) hasError = true;
  }
  return hasError ? 1 : 0;
}

static int cmdPublish(raw_ostream &OS) {
  if (PublishPath.empty()) {
    WithColor::error() << "marai publish : chemin manquant.\n";
    return 1;
  }
  InstallOptions Opts;
  Installer installer(Opts);
  return installer.publish(PublishPath, OS) ? 0 : 1;
}

//----------------------------------------------------------------------
// main
//----------------------------------------------------------------------

int main(int argc, char **argv) {
  InitLLVM X(argc, argv);

  std::string Argv0 = argc > 0 ? argv[0] : "";

  cl::SetVersionPrinter([](raw_ostream &OS) {
    OS << "marai v0.1 Naverta build 26160621 beta\n";
  });
  cl::ParseCommandLineOptions(argc, argv,
    "marai - Maratine Package Manager (MABI 1.0.1)\n");

  raw_ostream &OS = outs();

  if (VersionCmd) { printVersion(OS); return 0; }

  printHeader(OS);

  if (NewCmd)     return cmdNew(OS, Argv0);
  if (LSPCmd)     return marai::runLSP(Argv0);
  if (CheckCmd)   return cmdCheck(OS, Argv0);
  if (BuildCmd)   return cmdBuild(OS, Argv0);
  if (InstallCmd) return cmdInstall(OS);
  if (AuditCmd)   return cmdAudit(OS);
  if (ABICmd)     return cmdABI(OS);
  if (ListCmd)    return cmdList(OS);
  if (RemoveCmd)  return cmdRemove(OS);
  if (UpdateCmd)  return cmdUpdate(OS);
  if (PublishCmd) return cmdPublish(OS);

  OS << "Usage : marai <commande> [options]\n\n";
  OS << "Commandes :\n";
  OS << "  new      <NomProjet[.marep|.slul]>  Creer un nouveau projet depuis template\n";
  OS << "  lsp                            Serveur LSP (VSCode / stdio JSON-RPC)\n";
  OS << "  check    <projet.marep|.slul>  Auditer : IR, securite, entry-points\n";
  OS << "  build    [projet.marep|.slul]  Compiler et packager (auto-detecte si absent)\n";
  OS << "  install  <pkg[@ver]>...    Installer des packages\n";
  OS << "  update                     Mettre à jour tous les packages\n";
  OS << "  remove   <pkg>             Supprimer un package\n";
  OS << "  list     [--deps]          Lister les packages installés\n";
  OS << "  audit    [--aude] [--json] Analyser les failles (--aude = mode ingénieur)\n";
  OS << "  publish  <path>            Publier un package\n";
  OS << "  abi      check <pkg>       Vérifier la compatibilité MABI 1.0.1\n";
  OS << "  version                    Afficher la version\n\n";
  OS << "Exemples :\n";
  OS << "  marai new   MonApp.marep\n";
  OS << "  marai new   MonDriver.slul --dir D:\\Projets\n";
  OS << "  marai check MaratineProjectAppTemplate.marep\n";
  OS << "  marai check MaratineProjectAppTemplate.marep -O --json\n";
  OS << "  marai check MaratineProjectAppTemplate.slul --show-ir\n";
  OS << "  marai build                          (depuis le dossier du projet)\n";
  OS << "  marai build MonApp.marep -O\n";
  OS << "  marai install MGCPhysics@2.1.0\n";
  OS << "  marai install --force MaraNet@1.9.0\n";
  OS << "  marai audit\n";
  OS << "  marai audit --aude --sbom\n";
  OS << "  marai audit --aude --json > rapport.json\n";
  OS << "  marai abi check MaraNet\n\n";
  return 0;
}
