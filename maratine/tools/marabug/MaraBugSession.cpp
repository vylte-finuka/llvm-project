//===-- MaraBugSession.cpp – MaraBug Debug Session ------------------------===//
//
// MaraBug — Mara/Maratine Debugger
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
//===----------------------------------------------------------------------===//

#include "MaraBugSession.h"
#include "llvm/DebugInfo/DWARF/DWARFContext.h"
#include "llvm/Object/ObjectFile.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/WithColor.h"

using namespace llvm;
using namespace marabug;

DebugSession::~DebugSession() {
  if (CurrentState != State::Detached) detach();
}

//----------------------------------------------------------------------
// loadDebugInfo – charge les symboles DWARF pour la résolution source
//----------------------------------------------------------------------

bool DebugSession::loadDebugInfo(StringRef ExecutablePath) {
  // TODO: ouvrir l'objet ARM64 ELF/Mach-O, extraire les sections DWARF,
  // construire AddrToSource pour la résolution PC → File:Line Mara.
  //
  // Squelette :
  //   auto ObjOrErr = object::ObjectFile::createObjectFile(ExecutablePath);
  //   if (!ObjOrErr) return false;
  //   auto Ctx = DWARFContext::create(**ObjOrErr);
  //   for each compile unit :
  //     for each line table entry :
  //       AddrToSource[entry.Address] = entry.File + ":" + entry.Line;
  return true;
}

//----------------------------------------------------------------------
// launch
//----------------------------------------------------------------------

bool DebugSession::launch(StringRef ExecutablePath,
                           const std::vector<std::string> &Args,
                           raw_ostream &OS) {
  if (!loadDebugInfo(ExecutablePath)) {
    WithColor::warning() << "Symboles DWARF absents — débogage source désactivé.\n";
  }
  // TODO: démarrer le processus ARM64 via LLDB SBLauncher ou ptrace
  CurrentState = State::Stopped;
  OS << "  [MaraBug] Processus créé — en pause au point d'entrée.\n";
  return true;
}

//----------------------------------------------------------------------
// attach
//----------------------------------------------------------------------

bool DebugSession::attach(unsigned PID, raw_ostream &OS) {
  // TODO: ptrace PTRACE_ATTACH ou LLDB SBDebugger::Attach
  CurrentState = State::Stopped;
  OS << "  [MaraBug] Attaché au PID " << PID << " — processus suspendu.\n";
  return true;
}

//----------------------------------------------------------------------
// Contrôle d'exécution
//----------------------------------------------------------------------

bool DebugSession::continueExec(raw_ostream &OS) {
  CurrentState = State::Running;
  OS << "  [MaraBug] Exécution reprise…\n";
  // TODO: PTRACE_CONT ou LLDB SBProcess::Continue
  return true;
}

bool DebugSession::stepOver(raw_ostream &OS) {
  OS << "  [MaraBug] step over\n";
  // TODO: PTRACE_SINGLESTEP jusqu'à la prochaine ligne différente
  return true;
}

bool DebugSession::stepInto(raw_ostream &OS) {
  OS << "  [MaraBug] step into\n";
  return true;
}

bool DebugSession::stepOut(raw_ostream &OS) {
  OS << "  [MaraBug] step out\n";
  return true;
}

//----------------------------------------------------------------------
// Breakpoints
//----------------------------------------------------------------------

Breakpoint &DebugSession::addBreakpoint(StringRef File, unsigned Line) {
  Breakpoints.push_back({NextBPID++, File.str(), Line, "", true, 0, ""});
  // TODO: insérer instruction BRK ARM64 à l'adresse résolue
  return Breakpoints.back();
}

Breakpoint &DebugSession::addFunctionBreakpoint(StringRef FunctionName) {
  Breakpoints.push_back({NextBPID++, "", 0, FunctionName.str(), true, 0, ""});
  // TODO: résoudre le symbole manglé MABI → adresse → BRK
  return Breakpoints.back();
}

bool DebugSession::removeBreakpoint(unsigned ID) {
  for (auto it = Breakpoints.begin(); it != Breakpoints.end(); ++it) {
    if (it->ID == ID) {
      // TODO: restaurer l'instruction originale à l'adresse
      Breakpoints.erase(it);
      return true;
    }
  }
  return false;
}

bool DebugSession::setBreakpointEnabled(unsigned ID, bool Enabled) {
  for (auto &B : Breakpoints) {
    if (B.ID == ID) { B.Enabled = Enabled; return true; }
  }
  return false;
}

//----------------------------------------------------------------------
// Inspection
//----------------------------------------------------------------------

std::vector<StackFrame> DebugSession::backtrace() const {
  // TODO: unwind de la stack ARM64 via FP/LR chain + DWARF CFI
  return {};
}

std::vector<Variable> DebugSession::locals(unsigned FrameIndex) const {
  // TODO: lire les variables locales via DWARF DW_TAG_variable
  // et les traduire en types Mara (<i32>, <string>, <ptr>…)
  return {};
}

std::string DebugSession::evaluate(StringRef Expr) const {
  // TODO: évaluer l'expression dans le contexte Mara courant
  // via le parser Mara en mode expression + accès registres ARM64
  return "(non implémenté)";
}

void DebugSession::showSource(raw_ostream &OS, unsigned ContextLines) const {
  // TODO: résoudre PC courant → fichier .mara + numéro de ligne
  // et afficher les ContextLines lignes autour avec flèche →
  OS << "  (source non disponible — symboles DWARF requis)\n";
}

std::string DebugSession::resolveSource(uint64_t PC) const {
  auto it = AddrToSource.find(PC);
  if (it != AddrToSource.end()) return it->second;
  return "<inconnu>";
}

//----------------------------------------------------------------------
// detach
//----------------------------------------------------------------------

void DebugSession::detach() {
  if (CurrentState == State::Detached) return;
  // TODO: PTRACE_DETACH ou LLDB SBProcess::Detach
  CurrentState = State::Detached;
}
