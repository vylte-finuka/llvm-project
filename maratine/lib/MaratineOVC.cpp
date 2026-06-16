// Vyft Ltd — Mara/Maratine OVC format implementation — Proprietary — 2026

#include <cstdint>
#include <vector>
#include <memory>
#include <ctime>

#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Error.h"

namespace llvm {
namespace maratine {

static constexpr uint32_t OVC_MAGIC = 0x4F564354; // 'OVCT'
static constexpr uint32_t OVC_VERSION = 1;

enum class OVCArchitecture : uint16_t {
  X86_64 = 1,
  ARM64  = 2
};

enum class OVCPlatform : uint16_t {
  SluraOS = 1,
  Linux   = 2
};

enum class OVCSectionType : uint8_t {
  LLVM_IR = 1,
  Metadata = 2,
  Binary = 3
};

enum class OVCFlags : uint32_t {
  NONE = 0,
  POSITION_INDEPENDENT = 1 << 0
};

struct OVCHeader {
  uint32_t magic;
  uint32_t version;
  uint16_t architecture;
  uint16_t platform;
  uint32_t section_count;
  uint32_t entry_point;
  uint32_t total_size;
  uint32_t flags;
  uint64_t timestamp;
};

struct OVCSection {
  uint8_t type;
  uint32_t offset;
  uint32_t size;
  uint32_t alignment;
};

struct OVCMetadata {
  char reserved[64];
};

class OVCFile {
public:
  OVCFile();

  void setArchitecture(OVCArchitecture arch);
  void setPlatform(OVCPlatform plat);
  void setEntryPoint(uint32_t ep);
  void addFlags(OVCFlags flag);

  void addSection(const OVCSection &section);
  OVCSection *getSection(OVCSectionType type);

  bool write(llvm::StringRef filename);
  bool read(llvm::StringRef filename);

  bool validateHeader() const;
  bool validateCRC32() const;

  void dump() const;

  std::unique_ptr<std::vector<uint8_t>> data;

private:
  OVCHeader header;
  std::vector<OVCSection> sections;
};

class OVCBuilder {
public:
  OVCBuilder();

  bool buildFromLLVMModule(const llvm::Module &M);
  bool emitToFile(llvm::StringRef filename);

  void setMetadata(const OVCMetadata &meta);

private:
  OVCFile ovc;
  std::vector<uint8_t> buffer;
};

class OVCParser {
public:
  OVCParser();

  bool parse(llvm::StringRef filename);
  llvm::Module *extractModule(llvm::LLVMContext &ctx);
  OVCMetadata getMetadata() const;

private:
  OVCFile ovc;
};

} // namespace maratine
} // namespace llvm
