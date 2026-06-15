// Vyft Ltd - OVC Format Implementation
// Lecture/écriture de fichiers .ovc

#include "MaratineOVC.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/Endian.h"
#include <cstring>
#include <fstream>

using namespace llvm;
using namespace llvm::maratine;

OVCFile::OVCFile() {
  std::memset(&header, 0, sizeof(OVCHeader));
  header.magic = OVC_MAGIC;
  header.version = OVC_VERSION;
  header.architecture = (uint16_t)OVCArchitecture::ARM64;
  header.platform = (uint16_t)OVCPlatform::SluraOS;
  header.timestamp = std::time(nullptr);
  data = std::make_unique<std::vector<uint8_t>>();
}

void OVCFile::setArchitecture(OVCArchitecture arch) {
  header.architecture = (uint16_t)arch;
}

void OVCFile::setPlatform(OVCPlatform plat) {
  header.platform = (uint16_t)plat;
}

void OVCFile::setEntryPoint(uint32_t ep) {
  header.entry_point = ep;
}

void OVCFile::addFlags(OVCFlags flag) {
  header.flags |= (uint32_t)flag;
}

void OVCFile::addSection(const OVCSection &section) {
  sections.push_back(section);
  header.section_count++;
}

OVCSection *OVCFile::getSection(OVCSectionType type) {
  for (auto &sec : sections) {
    if (sec.type == (uint8_t)type)
      return &sec;
  }
  return nullptr;
}

bool OVCFile::validateHeader() const {
  if (header.magic != OVC_MAGIC)
    return false;
  if (header.version != OVC_VERSION)
    return false;
  return true;
}

bool OVCFile::validateCRC32() const {
  // TODO: Implémenter CRC32 check
  return true;
}

bool OVCFile::write(StringRef filename) {
  std::ofstream out(filename.str(), std::ios::binary);
  
  if (!out.is_open())
    return false;
  
  // Écrire le header
  out.write(reinterpret_cast<const char*>(&header), sizeof(OVCHeader));
  
  // Écrire les sections
  for (const auto &sec : sections) {
    out.write(reinterpret_cast<const char*>(&sec), sizeof(OVCSection));
  }
  
  // Écrire les données
  if (!data->empty()) {
    out.write(reinterpret_cast<const char*>(data->data()), data->size());
  }
  
  out.close();
  return true;
}

bool OVCFile::read(StringRef filename) {
  std::ifstream in(filename.str(), std::ios::binary);
  
  if (!in.is_open())
    return false;
  
  // Lire le header
  in.read(reinterpret_cast<char*>(&header), sizeof(OVCHeader));
  
  if (!validateHeader())
    return false;
  
  // Lire les sections
  for (uint32_t i = 0; i < header.section_count; i++) {
    OVCSection sec;
    in.read(reinterpret_cast<char*>(&sec), sizeof(OVCSection));
    sections.push_back(sec);
  }
  
  // Lire les données
  in.seekg(0, std::ios::end);
  size_t file_size = in.tellg();
  size_t data_size = file_size - sizeof(OVCHeader) - 
                     (sections.size() * sizeof(OVCSection));
  
  if (data_size > 0) {
    in.seekg(sizeof(OVCHeader) + (sections.size() * sizeof(OVCSection)));
    data->resize(data_size);
    in.read(reinterpret_cast<char*>(data->data()), data_size);
  }
  
  in.close();
  return validateCRC32();
}

void OVCFile::dump() const {
  errs() << "=== OVC File ===\n";
  errs() << "Magic: 0x" << llvm::format_hex(header.magic, 8) << "\n";
  errs() << "Version: " << llvm::format_hex(header.version, 8) << "\n";
  errs() << "Architecture: " << header.architecture << "\n";
  errs() << "Platform: " << header.platform << "\n";
  errs() << "Sections: " << header.section_count << "\n";
  errs() << "Entry Point: 0x" << llvm::format_hex(header.entry_point, 8) << "\n";
  errs() << "Total Size: " << header.total_size << " bytes\n";
  
  for (size_t i = 0; i < sections.size(); i++) {
    errs() << "  Section " << i << ": Type=" << (int)sections[i].type
           << " Offset=" << sections[i].offset
           << " Size=" << sections[i].size << "\n";
  }
}

// OVCBuilder implementation
OVCBuilder::OVCBuilder() {
  ovc.setPlatform(OVCPlatform::SluraOS);
  ovc.setArchitecture(OVCArchitecture::ARM64);
  ovc.addFlags(OVCFlags::POSITION_INDEPENDENT);
}

bool OVCBuilder::buildFromLLVMModule(const llvm::Module &M) {
  // Sérialiser le module LLVM en bytecode
  SmallVector<char, 1024> buffer;
  raw_svector_ostream os(buffer);
  
  M.print(os, nullptr);
  
  // Ajouter section LLVM_IR
  OVCSection ir_section;
  ir_section.type = (uint8_t)OVCSectionType::LLVM_IR;
  ir_section.offset = sizeof(OVCHeader);
  ir_section.size = buffer.size();
  ir_section.alignment = 8;
  
  ovc.addSection(ir_section);
  
  this->buffer.assign(buffer.begin(), buffer.end());
  
  return true;
}

void OVCBuilder::setMetadata(const OVCMetadata &meta) {
  // TODO: Implémenter stockage métadonnées
}

bool OVCBuilder::emitToFile(StringRef filename) {
  return ovc.write(filename);
}

// OVCParser implementation
OVCParser::OVCParser() {}

bool OVCParser::parse(StringRef filename) {
  return ovc.read(filename);
}

llvm::Module *OVCParser::extractModule(llvm::LLVMContext &ctx) {
  // TODO: Reconvertir depuis OVC format vers LLVM Module
  return nullptr;
}

OVCMetadata OVCParser::getMetadata() const {
  OVCMetadata meta;
  std::memset(&meta, 0, sizeof(OVCMetadata));
  return meta;
}
