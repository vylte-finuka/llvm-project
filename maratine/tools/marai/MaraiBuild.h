// Vyft Ltd — marai build — Proprietary — 2026

#pragma once

#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace llvm;

namespace marai {

// Parsed Maraset.yaml fields
struct ManifestInfo {
  std::string PackageName;  // e.g. "base***HelloWorld"
  std::string ShortName;    // e.g. "HelloWorld"
  std::string Version;
  std::string BundleType;   // "marep" or "slul"
  bool        Valid = false;
};

struct BuildOptions {
  std::string OutputDir;         // --out-dir (default: cwd)
  std::string OutputFile;        // -o (override output path)
  bool        Optimize  = false; // -O
  bool        Verbose   = false; // --verbose
  std::string CompilerOverride;  // --compiler
};

struct BuildResult {
  std::string ProjectPath;
  std::string OutputPackage;
  bool        Success  = false;
  std::string ErrorMsg;

  void print(raw_ostream &OS, bool color) const;
};

class Builder {
public:
  Builder(const BuildOptions &Opts, const std::string &MaraiExePath);

  // Each entry must be a .marep or .slul project directory.
  std::vector<BuildResult> build(const std::vector<std::string> &Projects);

  // Accessible to other tools (e.g. marai check) for manifest parsing.
  ManifestInfo readManifest(const std::string &ProjectDir) const;

private:
  BuildOptions Opts;
  std::string  MaraiExePath;

  std::string  resolveCompiler() const;

  BuildResult buildProject(const std::string &ProjectDir,
                           const ManifestInfo &Info,
                           const std::string &Compiler);

  bool compileBase(const std::string &SrcBase,
                   const std::string &DstBase,
                   const std::string &Compiler,
                   std::string &ErrMsg);

  bool copyAssets(const std::string &ProjectDir,
                  const std::string &BundleDir,
                  std::string &ErrMsg);

  bool createPackage(const std::string &BundleDir,
                     const std::string &OutputPath,
                     std::string &ErrMsg);
};

} // namespace marai
