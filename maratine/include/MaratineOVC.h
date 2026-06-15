// Vyft Ltd - OVC Format Specification
// Vyft Compiled Output (.ovc) - Maratine Bytecode/IR Container
// Version: 1.0

#ifndef LLVM_MARATINE_OVC_FORMAT_H
#define LLVM_MARATINE_OVC_FORMAT_H

#include "llvm/Support/DataStream.h"
#include "llvm/ADT/StringRef.h"
#include <cstdint>
#include <vector>
#include <memory>

namespace llvm {
namespace maratine {

// Magic number pour fichiers .ovc
constexpr uint32_t OVC_MAGIC = 0x4F564320; // "OVC "
constexpr uint32_t OVC_VERSION = 0x00010000; // v1.0

// Flags du format OVC
enum class OVCFlags : uint32_t {
  DEBUG_INFO = 1 << 0,        // Inclut les informations de debug
  OPTIMIZED = 1 << 1,         // Code optimisé
  POSITION_INDEPENDENT = 1 << 2,
  SHARED_LIBRARY = 1 << 3,
  EXECUTABLE = 1 << 4
};

// Types d'architectures cibles
enum class OVCArchitecture : uint16_t {
  ARM64 = 0x0100,       // aarch64
  X86_64 = 0x0200,
  ARM32 = 0x0300,
  WASM = 0x0400,        // WebAssembly
  SLURA_HYBRID = 0x8000 // Architecture Slura hybride
};

// Plateformes supportées
enum class OVCPlatform : uint16_t {
  iOS = 1,
  Android = 2,
  macOS = 3,
  Windows = 4,
  Linux = 5,
  WebBrowser = 6,
  SluraOS = 0xFF // Système Slura
};

// Section types
enum class OVCSectionType : uint8_t {
  HEADER = 0x00,
  METADATA = 0x01,
  LLVM_IR = 0x02,
  BYTECODE = 0x03,
  DEBUG_INFO = 0x04,
  SYMBOL_TABLE = 0x05,
  RELOCATION = 0x06,
  STRING_TABLE = 0x07,
  IMPORT_TABLE = 0x08,
  EXPORT_TABLE = 0x09
};

// Structure du header OVC
struct OVCHeader {
  uint32_t magic;           // 0x4F564320 ("OVC ")
  uint32_t version;         // Version du format
  uint16_t architecture;    // Architecture cible (OVCArchitecture)
  uint16_t platform;        // Plateforme (OVCPlatform)
  uint32_t flags;           // Flags (OVCFlags)
  uint32_t section_count;   // Nombre de sections
  uint32_t entry_point;     // Offset du point d'entrée
  uint64_t total_size;      // Taille totale du fichier
  uint64_t timestamp;       // Timestamp de compilation
  uint32_t crc32;           // Checksum CRC32
};

// Structure de section OVC
struct OVCSection {
  uint8_t type;             // Type de section (OVCSectionType)
  uint8_t reserved;
  uint16_t flags;
  uint32_t offset;          // Offset dans le fichier
  uint32_t size;            // Taille de la section
  uint32_t alignment;       // Alignement en mémoire
};

// Structure de métadonnées
struct OVCMetadata {
  char name[256];           // Nom de l'application
  char version[32];         // Version de l'app
  char author[256];         // Auteur
  char target_sdk[32];      // SDK cible (ex: "Slura 2.0")
  uint32_t min_sdk_version;
  uint32_t compile_timestamp;
};

// Classe pour lire/écrire fichiers .ovc
class OVCFile {
private:
  OVCHeader header;
  std::vector<OVCSection> sections;
  std::unique_ptr<std::vector<uint8_t>> data;

public:
  OVCFile();
  
  // Getters
  const OVCHeader &getHeader() const { return header; }
  const std::vector<OVCSection> &getSections() const { return sections; }
  
  // Setters
  void setArchitecture(OVCArchitecture arch);
  void setPlatform(OVCPlatform plat);
  void setEntryPoint(uint32_t ep);
  void addFlags(OVCFlags flag);
  
  // Gestion des sections
  void addSection(const OVCSection &section);
  OVCSection *getSection(OVCSectionType type);
  
  // I/O
  bool read(StringRef filename);
  bool write(StringRef filename);
  
  // Validation
  bool validateHeader() const;
  bool validateCRC32() const;
  
  // Diagnostic
  void dump() const;
};

// Builder pour créer fichiers .ovc
class OVCBuilder {
private:
  OVCFile ovc;
  std::vector<uint8_t> buffer;
  
public:
  OVCBuilder();
  
  // Build depuis LLVM IR
  bool buildFromLLVMModule(const llvm::Module &M);
  
  // Ajouter métadonnées
  void setMetadata(const OVCMetadata &meta);
  
  // Générer fichier
  bool emitToFile(StringRef filename);
};

// Parser pour lire fichiers .ovc
class OVCParser {
private:
  OVCFile ovc;
  
public:
  OVCParser();
  
  // Parser un fichier .ovc
  bool parse(StringRef filename);
  
  // Extraire la section IR
  llvm::Module *extractModule(llvm::LLVMContext &ctx);
  
  // Récupérer métadonnées
  OVCMetadata getMetadata() const;
};

} // namespace maratine
} // namespace llvm

#endif // LLVM_MARATINE_OVC_FORMAT_H
