//===--- Maralang.cpp - Maratine Language Driver --------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the Maratine language driver, which is a fork of the
// Clang driver tailored for the Maratine language. It supports compiling
// Maratine source files into SLUL (shared library) or MAREP (bundle) formats.
//
//===----------------------------------------------------------------------===//

#include "clang/Driver/Driver.h"
#include "clang/Driver/Compilation.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "clang/Driver/ToolChain.h"
#include "clang/Driver/Job.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/OptTable.h"
#include "llvm/Option/Arg.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/InitLLVM.h"
#include "llvm/Support/Signals.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/WithColor.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Errc.h"
#include "llvm/ADT/StringRef.h"
#include <string>
#include <vector>

using namespace clang;
using namespace clang::driver;
using namespace llvm;
using namespace llvm::opt;
using namespace llvm::sys;

// Command line options for Maralang.
static cl::opt<bool>
    SLUL("fslul", cl::desc("Generate SLUL (shared library) output"), cl::init(true));
static cl::opt<bool>
    Marep("fmarep", cl::desc("Generate MAREP (bundle) output"), cl::init(false));
static cl::opt<std::string>
    OutputName("o", cl::desc("Output filename"), cl::value_desc("filename"), cl::init(""));
static cl::opt<std::string>
    ManifestFile("manifest", cl::desc("Manifest file for MAREP bundle"), cl::value_desc("file"));
static cl::opt<std::string>
    ResourceDir("resource-dir", cl::desc("Directory containing resources for MAREP bundle"), cl::value_desc("dir"));

int main(int argc, const char **argv) {
  // Initialize LLVM.
  InitLLVM X(argc, argv);

  // Enable crash recovery.
  sys::SetHandleSignals();

  // Register targets.
  InitializeAllTargets();
  InitializeAllTargetMCs();
  InitializeAllAsmParsers();
  InitializeAllAsmPrinters();

  // Create a driver instance.
  Driver D("maralang", "Maratine Language Driver", llvm::errs());

  // Parse command line arguments.
  void *Allocator = D.getAllocator();
  llvm::opt::InputArgList Args =
      D.ParseArgs(argc, argv, Allocator);

  // Handle driver diagnostics.
  DriverDiagnostic Diags(D.getDiagnostics());

  // Check if any input file is a Maratine source (.mara) to remind the user about the plugin.
  bool HasMaraFile = false;
  for (unsigned i = 0; i < Args.size(); ++i) {
    const Arg *A = Args[i];
    // We consider an argument as an input if it is not an option (does not start with '-')
    // and ends with .mara. This is a simple heuristic.
    StringRef ArgStr = A->getAsString(Allocator);
    if (!ArgStr.empty() && ArgStr[0] != '-' && ArgStr.ends_with(".mara")) {
      HasMaraFile = true;
      break;
    }
  }
  if (HasMaraFile) {
    WithColor::note() << "Note: To use the Maratine frontend, pass '-plugin MaratineFrontend' (and possibly '-plugin-arg MaratineFrontend').\n";
  }

  // Determine output type: default to SLUL if neither specified.
  bool WantSLUL = SLUL || (!Marep && !SLUL); // default SLUL
  bool WantMarep = Marep;

  // If user explicitly set both, error.
  if (SLUL && Marep) {
    Diags.report(diag::err_drv_invalid_argument)
        << "cannot specify both -fslul and -fmarep";
    return 1;
  }

  // Perform the compilation.
  std::unique_ptr<Compilation> C(D.BuildCompilation(Args));
  if (!C) {
    Diags.report(diag::err_drv_failed_to_create_compilation);
    return 1;
  }

  // Execute the compilation to get the list of jobs.
  // We will run the compilation ourselves to intercept the link step.
  SmallVector<std::unique_ptr<Job>, 16> Jobs;
  bool Failed = false;
  for (const auto &Job : C->getJobs()) {
    Jobs.push_back(Job.clone());
  }

  // If we want to produce a shared library (SLUL), ensure the link job produces .slul.
  // We'll add -shared if not present and set the output file to our desired LibPath (with .slul extension).
  if (WantSLUL) {
    // Determine the shared library output file.
    SmallString<128> LibPath;
    if (!OutputName.empty()) {
      LibPath = OutputName.str();
    } else {
      // Derive from first input? For simplicity, use "liboutput".
      LibPath = "liboutput";
    }
    // Set extension to .slul for the Maratine shared library.
    std::string Ext = ".slul";
    if (!llvm::sys::path::extension(LibPath).equals_lower(Ext))
      LibPath += Ext;

    // Find the link job and adjust its arguments to output to LibPath and add -shared if needed.
    for (auto &JobPtr : Jobs) {
      if (JobPtr->getKind() == JobKind::Link) {
        // Add -shared flag if not already present.
        bool HasShared = false;
        for (const auto &Arg : JobPtr->getArguments()) {
          if (Arg.equals("-shared")) {
            HasShared = true;
            break;
          }
        }
        if (!HasShared) {
          JobPtr->getArguments().push_back(Args.MakeArgString(Allocator, "-shared"));
        }

        // Set the output file using -o.
        bool FoundO = false;
        for (size_t i = 0; i < JobPtr->getArguments().size(); ++i) {
          if (JobPtr->getArguments()[i].equals("-o")) {
            // Replace the next argument with our LibPath.
            JobPtr->getArguments()[i+1] = Args.MakeArgString(Allocator, LibPath.str());
            FoundO = true;
            break;
          }
        }
        if (!FoundO) {
          // Add -o and LibPath at the end.
          JobPtr->getArguments().push_back(Args.MakeArgString(Allocator, "-o"));
          JobPtr->getArguments().push_back(Args.MakeArgString(Allocator, LibPath.str()));
        }
        break;
      }
    }
  }

  // Execute the jobs to get the output file(s).
  // We'll use the driver's ExecuteCompilation but we need to capture the output.
  // Simpler: run the compilation and then, if MAREP, package.
  int Result = D.ExecuteCompilation(*C, Diags);
  if (Result != 0)
    return Result;

  // If MAREP requested, create the bundle.
  if (WantMarep) {
  // Determine the shared library output file.
  SmallString<128> LibPath;
  if (!OutputName.empty()) {
    LibPath = OutputName.str();
  } else {
    // Derive from first input? For simplicity, use "liboutput".
    LibPath = "liboutput";
  }
  // Set extension to .slul for the Maratine shared library.
  std::string Ext = ".slul";
  if (!llvm::sys::path::extension(LibPath).equals_lower(Ext))
    LibPath += Ext;    // Create MAREP file name: replace extension with .marep or append.
    SmallString<128> MarepPath = LibPath;
    llvm::sys::path::replace_extension(MarepPath, ".marep");

    // Gather files to include: the shared library, manifest, and resources.
    SmallVector<std::string, 8> FilesToZip;
    FilesToZip.push_back(LibPath.str());

    if (!ManifestFile.empty()) {
      if (!llvm::sys::fs::exists(ManifestFile)) {
        Diags.report(diag::err_drv_invalid_argument)
            << "manifest file not found: " << ManifestFile;
        return 1;
      }
      FilesToZip.push_back(ManifestFile);
    } else {
      // Default manifest name.
      StringRef DefaultManifest = "Maraset.yaml";
      if (llvm::sys::fs::exists(DefaultManifest))
        FilesToZip.push_back(DefaultManifest.str());
    }

    if (!ResourceDir.empty()) {
      if (!llvm::sys::fs::is_directory(ResourceDir)) {
        Diags.report(diag::err_drv_invalid_argument)
            << "resource directory not found: " << ResourceDir;
        return 1;
      }
      // Recursively add files from resource directory.
      llvm::sys::fs::directory_iterator It(ResourceDir), End;
      while (It != End) {
        if (llvm::sys::fs::is_regular_file(It->status()))
          FilesToZip.push_back(It->path());
        ++It;
      }
    } else {
      // Default resource dir: MaratineProjectAppTemplate.slasset
      StringRef DefaultResDir = "MaratineProjectAppTemplate.slasset";
      if (llvm::sys::fs::is_directory(DefaultResDir)) {
        llvm::sys::fs::directory_iterator It(DefaultResDir), End;
        while (It != End) {
          if (llvm::sys::fs::is_regular_file(It->status()))
            FilesToZip.push_back(It->path());
          ++It;
        }
      }
    }

    // Locate zip executable.
    std::string ZipPath;
    if (auto Err = llvm::sys::FindProgramByName("zip", ZipPath)) {
      Diags.report(diag::err_drv_invalid_argument)
          << "zip executable not found in PATH";
      return 1;
    }

    // Build zip command: zip -j <marep> <files>...
    SmallVector<const char *, 16> ZipArgs;
    ZipArgs.push_back(ZipPath.c_str());
    ZipArgs.push_back("-j"); // junk paths, store only file names.
    ZipArgs.push_back(MarepPath.c_str());
    for (const auto &F : FilesToZip)
      ZipArgs.push_back(F.c_str());
    ZipArgs.push_back(nullptr);

    // Execute zip.
    std::string ErrMsg;
    int ZipResult = llvm::sys::ExecuteAndWait(ZipPath, ZipArgs.data(), nullptr, nullptr, 0, nullptr, &ErrMsg);
    if (ZipResult != 0) {
      Diags.report(diag::err_drv_invalid_argument)
          << "failed to create MAREP bundle: " << ErrMsg;
      return 1;
    }

    // Optionally, inform the user.
    WithColor::note() << "MAREP bundle created: " << MarepPath << "\n";
  }

  return Result;
}