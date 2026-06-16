//===-- MaraBugSession.h – MaraBug Debug Session --------------------------===//
//
// MaraBug — Mara/Maratine Debugger
// Auteur : Vyft Ltd — 2026
// Licence : Proprietary
//
// Session de débogage Mara. Charge les symboles .mara depuis le DWARF
// généré par maratine-cc, traduit les adresses ARM64 en lignes source Mara.
// Construit au-dessus de LLDB SBDebugger.
//
//===----------------------------------------------------------------------===//

#pragma once

#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>
#include <map>

namespace marabug {

//----------------------------------------------------------------------
// Breakpoint Mara — identifié par fichier:ligne ou nom de fonction
//----------------------------------------------------------------------
struct Breakpoint {
  unsigned    ID;
  std::string File;      // fichier .mara
  unsigned    Line;
  std::string Function;  // ex. "HelloWorld.Create" ou "" si par ligne
  bool        Enabled = true;
  unsigned    HitCount = 0;
  std::string Condition; // expression Mara optionnelle
};

//----------------------------------------------------------------------
// Frame de la pile d'appel
//----------------------------------------------------------------------
struct StackFrame {
  unsigned    Index;
  std::string Function;
  std::string File;
  unsigned    Line;
  std::string Module;
  uint64_t    PC;       // Program Counter ARM64
};

//----------------------------------------------------------------------
// Variable locale visible dans le frame courant
//----------------------------------------------------------------------
struct Variable {
  std::string Name;
  std::string Type;    // type Mara : <i32>, <string>, <ptr>…
  std::string Value;
  bool        IsConst;
  bool        IsMutable;
};

//----------------------------------------------------------------------
// Session de débogage
//----------------------------------------------------------------------
class DebugSession {
public:
  enum class State {
    Detached,
    Launching,
    Running,
    Stopped,
    Exited,
  };

  DebugSession() = default;
  ~DebugSession();

  // Lance un exécutable .marep avec les arguments donnés
  bool launch(llvm::StringRef ExecutablePath,
              const std::vector<std::string> &Args,
              llvm::raw_ostream &OS);

  // Attache à un processus existant par PID
  bool attach(unsigned PID, llvm::raw_ostream &OS);

  // Continue l'exécution
  bool continueExec(llvm::raw_ostream &OS);

  // Pas-à-pas ligne (step over)
  bool stepOver(llvm::raw_ostream &OS);

  // Pas-à-pas dans la fonction (step into)
  bool stepInto(llvm::raw_ostream &OS);

  // Sortie de la fonction courante (step out)
  bool stepOut(llvm::raw_ostream &OS);

  // Ajoute un breakpoint fichier:ligne
  Breakpoint &addBreakpoint(llvm::StringRef File, unsigned Line);

  // Ajoute un breakpoint sur une fonction Mara
  Breakpoint &addFunctionBreakpoint(llvm::StringRef FunctionName);

  // Supprime un breakpoint par ID
  bool removeBreakpoint(unsigned ID);

  // Active/désactive un breakpoint
  bool setBreakpointEnabled(unsigned ID, bool Enabled);

  // Liste des breakpoints
  const std::vector<Breakpoint> &breakpoints() const { return Breakpoints; }

  // Backtrace de la pile d'appel courante
  std::vector<StackFrame> backtrace() const;

  // Variables locales du frame courant
  std::vector<Variable> locals(unsigned FrameIndex = 0) const;

  // Évalue une expression Mara dans le contexte courant
  std::string evaluate(llvm::StringRef Expr) const;

  // Affiche le source Mara autour de la position courante
  void showSource(llvm::raw_ostream &OS, unsigned ContextLines = 5) const;

  // Détache et arrête le processus
  void detach();

  State state() const { return CurrentState; }
  unsigned exitCode() const { return ExitCode; }

private:
  State    CurrentState = State::Detached;
  unsigned ExitCode     = 0;
  unsigned NextBPID     = 1;

  std::vector<Breakpoint>          Breakpoints;
  std::map<uint64_t, std::string>  AddrToSource; // PC → "File:Line"

  // Traduit une adresse ARM64 en position source Mara via DWARF
  std::string resolveSource(uint64_t PC) const;

  // Charge les symboles DWARF depuis l'exécutable
  bool loadDebugInfo(llvm::StringRef ExecutablePath);
};

} // namespace marabug
