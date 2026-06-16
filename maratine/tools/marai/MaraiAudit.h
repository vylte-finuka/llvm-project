//===-- MaraiAudit.h – Vulnerability Audit Engine ------------------------===//
//
// Maratine Package Manager — marai
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
// Audit de sécurité des packages installés.
// Niveaux : CRITICAL / HIGH / MEDIUM / LOW / INFO (CVSS v3.1)
// Mode --aude : audit cybersec avancé niveau ingénieur (SBOM, crypto,
//               permissions, supply chain, exposition réseau, MABI).
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

namespace marai {

//----------------------------------------------------------------------
// Niveau de sévérité CVE (CVSS v3.1)
//----------------------------------------------------------------------
enum class Severity {
  INFO     = 0,   // CVSS 0.0
  LOW      = 1,   // CVSS 0.1 – 3.9
  MEDIUM   = 2,   // CVSS 4.0 – 6.9
  HIGH     = 3,   // CVSS 7.0 – 8.9
  CRITICAL = 4,   // CVSS 9.0 – 10.0
};

llvm::StringRef severityLabel(Severity S);
llvm::StringRef severityColor(Severity S);  // code ANSI

//----------------------------------------------------------------------
// Faille CVE sur un package
//----------------------------------------------------------------------
struct Vulnerability {
  std::string CVEID;         // ex. CVE-2026-00142
  std::string PackageName;
  std::string AffectedVersion;
  std::string FixedVersion;
  Severity    Level;
  float       CVSSScore;
  std::string Description;
  std::string Vector;        // ex. NETWORK / LOCAL / PHYSICAL
  std::string Remediation;
};

//----------------------------------------------------------------------
// Rapport d'audit standard
//----------------------------------------------------------------------
struct AuditReport {
  std::vector<Vulnerability> Findings;
  unsigned TotalPackages  = 0;
  unsigned CriticalCount  = 0;
  unsigned HighCount      = 0;
  unsigned MediumCount    = 0;
  unsigned LowCount       = 0;
  unsigned InfoCount      = 0;

  bool hasBlockers() const { return CriticalCount > 0 || HighCount > 0; }
  void print(llvm::raw_ostream &OS, bool Color) const;
  void printJSON(llvm::raw_ostream &OS) const;
  void addFinding(const Vulnerability &V);
};

//----------------------------------------------------------------------
// Rapport d'audit avancé (--aude)
//----------------------------------------------------------------------
struct AudeReport : public AuditReport {

  // ABI compatibility matrix
  struct ABIEntry {
    std::string Package;
    std::string Status;  // "OK" | "BREAKING" | "UNKNOWN"
    std::string Detail;
  };
  std::vector<ABIEntry> ABIMatrix;

  // Audit cryptographique
  struct CryptoIssue {
    std::string Package;
    std::string Algorithm;  // ex. "MD5", "SHA-1", "RSA-1024"
    std::string Reason;     // ex. "algorithme faible", "taille clé insuffisante"
    Severity    Level;
  };
  std::vector<CryptoIssue> CryptoIssues;

  // Analyse permissions (RAbstractallowing.xml)
  struct PermIssue {
    std::string Package;
    std::string Permission;  // ex. "NET_RAW", "CAMERA", "FS_ROOT"
    std::string Reason;
    Severity    Level;
  };
  std::vector<PermIssue> PermIssues;

  // Exposition réseau
  struct NetExposure {
    std::string Package;
    std::string Endpoint;   // ex. "0.0.0.0:8080"
    std::string Protocol;
    bool        Encrypted;
  };
  std::vector<NetExposure> NetExposures;

  // SBOM — Software Bill of Materials
  struct SBOMEntry {
    std::string Name;
    std::string Version;
    std::string License;
    std::string SHA256;     // hash d'intégrité du package
    std::string Supplier;
  };
  std::vector<SBOMEntry> SBOM;

  // Supply chain integrity
  struct ChainIssue {
    std::string Package;
    std::string Issue;   // ex. "hash mismatch", "signature absente"
    Severity    Level;
  };
  std::vector<ChainIssue> ChainIssues;

  void printAude(llvm::raw_ostream &OS, bool Color) const;
  void printAudeSBOM(llvm::raw_ostream &OS) const;
};

//----------------------------------------------------------------------
// Moteur d'audit
//----------------------------------------------------------------------
class AuditEngine {
public:
  // Audit standard : CVE par niveau de sévérité
  static AuditReport run(llvm::StringRef LockfilePath, bool Color);

  // Audit avancé --aude : CVE + ABI + crypto + perms + réseau + SBOM
  static AudeReport runAude(llvm::StringRef LockfilePath,
                             llvm::StringRef ProjectRoot, bool Color);

private:
  static void queryCVEDatabase(const std::string &PackageName,
                                const std::string &Version,
                                AuditReport &Report);

  static void auditCrypto(llvm::StringRef PackagePath, AudeReport &R);
  static void auditPermissions(llvm::StringRef PackagePath, AudeReport &R);
  static void auditNetwork(llvm::StringRef PackagePath, AudeReport &R);
  static void buildSBOM(llvm::StringRef LockfilePath, AudeReport &R);
  static void checkSupplyChain(llvm::StringRef LockfilePath, AudeReport &R);
};

} // namespace marai
