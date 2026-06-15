// Vyft Ltd - Maratine Compiler Driver
// Compilateur principal pour le langage Maratine

#include "MaratineLexer.h"
#include "MaratineParser.h"
#include "MaratineCodeGen.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/IR/IRPrintingPasses.h"
#include "llvm/IR/PassManager.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/TargetParser/Host.h"
#include "llvm/Target/TargetMachine.h"
#include <iostream>

using namespace llvm;
using namespace llvm::maratine;

static cl::opt<std::string> InputFile(
    cl::Positional,
    cl::desc("<input maratine file (.mart)>"),
    cl::Required);

static cl::opt<std::string> OutputFile(
    "o",
    cl::desc("Output filename (*.ovc, *.ll, *.o)"),
    cl::value_desc("filename"));

static cl::opt<std::string> OutputFormat(
    "emit",
    cl::init("ovc"),
    cl::desc("Output format: ovc (app), llvm, obj, asm"),
    cl::value_desc("format"));

static cl::opt<bool> DumpAST(
    "dump-ast",
    cl::desc("Dump abstract syntax tree"));

static cl::opt<bool> DumpTokens(
    "dump-tokens",
    cl::desc("Dump lexer tokens"));

static cl::opt<bool> Optimize(
    "O",
    cl::desc("Enable optimizations"));

int main(int argc, char **argv) {
  cl::ParseCommandLineOptions(argc, argv, "Maratine Compiler\n");

  // Lire le fichier source (.mart)
  auto BufferOrError = MemoryBuffer::getFile(InputFile);
  if (!BufferOrError) {
    errs() << "Error reading .mart file: " << BufferOrError.getError().message()
           << "\n";
    return 1;
  }

  StringRef SourceCode = BufferOrError.get()->getBuffer();
  errs() << "=== Maratine Compiler (*.mart) ===\n";
  errs() << "Source file: " << InputFile << "\n";
  errs() << "Lines: " << SourceCode.count('\n') << "\n\n";

  // --- LEXICAL ANALYSIS ---
  errs() << "[1/4] Lexical Analysis...\n";
  Lexer L(SourceCode);
  auto Tokens = L.tokenize();

  if (DumpTokens) {
    errs() << "Tokens: " << Tokens.size() << "\n";
    for (const auto &T : Tokens) {
      errs() << "  Token: " << (int)T.Kind << " = " << T.Value << "\n";
    }
  }

  // --- PARSING ---
  errs() << "[2/4] Parsing...\n";
  Parser P(Tokens);
  auto ModuleOrError = P.parseModule();

  if (!ModuleOrError) {
    errs() << "Parse error: " << toString(ModuleOrError.takeError()) << "\n";
    return 1;
  }

  auto Module = std::move(*ModuleOrError);

  if (DumpAST) {
    errs() << "AST:\n";
    P.printAST(*Module);
  }

  // --- CODE GENERATION ---
  errs() << "[3/4] Code Generation...\n";
  CodeGenerator CG("maratine_module");

  auto ModuleOrGenError = CG.codegenModule(Module.get());
  if (!ModuleOrGenError) {
    errs() << "Code generation error: "
           << toString(ModuleOrGenError.takeError()) << "\n";
    return 1;
  }

  auto LLVMModule = std::move(*ModuleOrGenError);

  // --- OPTIMIZATION ---
  if (Optimize) {
    errs() << "[4/4] Optimization...\n";
    CG.optimize();
  } else {
    errs() << "[4/4] Skipping optimization (use -O to enable)\n";
  }

  // --- OUTPUT ---
  errs() << "Generating output...\n";

  std::string OutFile = OutputFile;
  if (OutFile.empty()) {
    size_t DotPos = InputFile.rfind('.');
    if (OutputFormat == "ovc") {
      OutFile = InputFile.substr(0, DotPos) + ".ovc";
    } else if (OutputFormat == "llvm") {
      OutFile = InputFile.substr(0, DotPos) + ".ll";
    } else {
      OutFile = InputFile.substr(0, DotPos) + ".out";
    }
  }

  std::error_code EC;
  tool_output_file OutStream(OutFile, EC, sys::fs::OF_None);

  if (EC) {
    errs() << "Cannot open output file: " << EC.message() << "\n";
    return 1;
  }

  if (OutputFormat == "ovc") {
    // Générer fichier .ovc (Vyft Compiled Output)
    OutStream.os() << "# Vyft OVC Format v1.0\n";
    OutStream.os() << "# Maratine Compiled Application\n";
    OutStream.os() << "# Target: Slura OS\n\n";
    LLVMModule->print(OutStream.os(), nullptr);
  } else if (OutputFormat == "llvm") {
    LLVMModule->print(OutStream.os(), nullptr);
  } else if (OutputFormat == "asm") {
    // TODO: Générer assembly
    errs() << "ASM output not yet implemented\n";
    return 1;
  } else if (OutputFormat == "obj") {
    // TODO: Générer objet
    errs() << "Object output not yet implemented\n";
    return 1;
  }

  OutStream.keep();

  errs() << "✓ Compilation successful!\n";
  errs() << "Output: " << OutFile << "\n";

  return 0;
}
