//===-- MaraiABI.h – MABI 1.0.1 ABI Compatibility Checker ---------------===//
//
// Maratine Package Manager — marai
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
// MABI = Mara Application Binary Interface — version 1.0.1
//
// Architectures supportées :
//   ARM64  — AAPCS64 (cible principale : Slura OS / ShiWear To1)
//             args : x0–x7, retour : x0, FP : d0–d7
//             mangling : _Mara_<Module>_<Symbol>_<Hash6>
//
//   X64    — Microsoft x64 ABI (développement Windows AMD64)
//             args : RCX, RDX, R8, R9, retour : RAX
//             mangling : _Mara_x64_<Module>_<Symbol>_<Hash6>
//
// Format du manifeste .mabi (YAML) :
//   mabi: "1.0.1"
//   arch: "arm64"        # ou "x64"
//   package: "MaraMem"
//   entries:
//     - symbol: "_Mara_MaraMem_Alloc_A1B2C3"
//       module: "MaraMem"
//       signature: "(i32,i32,i32)->ptr"
//       public: true
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/raw_ostream.h"
#include <map>
#include <string>
#include <vector>

namespace marai {

//----------------------------------------------------------------------
// Architecture cible MABI
//----------------------------------------------------------------------
enum class TargetArch {
  ARM64,   // AAPCS64 — Slura OS / ShiWear To1
  X64,     // Microsoft x64 ABI — Windows AMD64 (dev + ShiUI desktop)
  Unknown,
};

TargetArch parseArch(llvm::StringRef S);
llvm::StringRef archName(TargetArch A);

//----------------------------------------------------------------------
// Convention d'appel par architecture
//----------------------------------------------------------------------
struct CallingConvention {
  TargetArch Arch;
  std::string Name;          // "AAPCS64" ou "Microsoft x64"
  std::string ArgRegs;       // "x0-x7" ou "RCX,RDX,R8,R9"
  std::string ReturnReg;     // "x0" ou "RAX"
  std::string FloatArgRegs;  // "d0-d7" ou "XMM0-XMM3"
  unsigned    MaxRegArgs;    // 8 (ARM64) ou 4 (X64)

  static CallingConvention forArch(TargetArch A);
  void print(llvm::raw_ostream &OS) const;
};

//----------------------------------------------------------------------
// Version MABI — Major.Minor.Patch
//----------------------------------------------------------------------
struct MABIVersion {
  unsigned Major = 1;
  unsigned Minor = 0;
  unsigned Patch = 1;

  static MABIVersion current() { return {1, 0, 1}; }
  static MABIVersion parse(llvm::StringRef S);

  bool isCompatibleWith(const MABIVersion &Other) const {
    return Major == Other.Major && Minor >= Other.Minor;
  }

  bool operator==(const MABIVersion &O) const {
    return Major == O.Major && Minor == O.Minor && Patch == O.Patch;
  }
  bool operator<(const MABIVersion &O) const {
    if (Major != O.Major) return Major < O.Major;
    if (Minor != O.Minor) return Minor < O.Minor;
    return Patch < O.Patch;
  }

  std::string str() const {
    return "MABI " + std::to_string(Major) + "." +
           std::to_string(Minor) + "." + std::to_string(Patch);
  }
};

//----------------------------------------------------------------------
// Entrée du manifeste ABI (.mabi)
//----------------------------------------------------------------------
struct ABIEntry {
  std::string Symbol;     // nom manglé
  std::string Module;
  std::string Signature;  // "(i32,ptr)->i32"
  bool        IsPublic;   // rel op = true, rel cl = false
  TargetArch  Arch = TargetArch::ARM64;
};

//----------------------------------------------------------------------
// Résultat de comparaison ABI
//----------------------------------------------------------------------
enum class ABICompatLevel {
  Compatible,      // aucun changement
  BackwardCompat,  // ajout d'API — rétrocompatible
  BreakingChange,  // symbole supprimé ou signature modifiée — bloquant
  ArchMismatch,    // architectures incompatibles
  VersionMismatch, // Major MABI différent
};

struct ABICheckResult {
  ABICompatLevel           Level = ABICompatLevel::Compatible;
  std::vector<std::string> Breaking;
  std::vector<std::string> Added;
  std::vector<std::string> Warnings;
  MABIVersion              InstalledVersion;
  MABIVersion              RequiredVersion;
  TargetArch               InstalledArch = TargetArch::Unknown;
  TargetArch               RequiredArch  = TargetArch::Unknown;

  bool isBlocking() const {
    return Level == ABICompatLevel::BreakingChange ||
           Level == ABICompatLevel::ArchMismatch   ||
           Level == ABICompatLevel::VersionMismatch;
  }
  void print(llvm::raw_ostream &OS) const;
};

//----------------------------------------------------------------------
// Vérificateur ABI MABI 1.0.1
//----------------------------------------------------------------------
class ABIChecker {
public:
  // Charge les entrées depuis un manifeste .mabi YAML
  static std::vector<ABIEntry> loadManifest(llvm::StringRef ManifestPath);

  // Compare deux manifestes (installé vs requis)
  static ABICheckResult check(llvm::StringRef InstalledManifestPath,
                               llvm::StringRef RequiredManifestPath);

  // Génère le nom manglé MABI pour un symbole Mara
  // ARM64 : _Mara_<Module>_<Symbol>_<Hash6>
  // X64   : _Mara_x64_<Module>_<Symbol>_<Hash6>
  static std::string mangle(llvm::StringRef Module, llvm::StringRef Symbol,
                              TargetArch Arch = TargetArch::ARM64);

  // Valide qu'un nom manglé respecte la convention MABI 1.0.1
  static bool validateMangling(llvm::StringRef MangledName,
                                TargetArch Arch = TargetArch::ARM64);

  // Démanglage : retourne Module et Symbol depuis un nom manglé
  static bool demangle(llvm::StringRef MangledName,
                        std::string &OutModule, std::string &OutSymbol,
                        TargetArch &OutArch);

  // Génère un manifeste .mabi vide pour un package donné
  static void generateManifest(llvm::StringRef PackageName, TargetArch Arch,
                                 llvm::raw_ostream &OS);
};

} // namespace marai
