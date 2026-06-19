// Vyft Ltd — marai lsp — Serveur LSP Maratine — 2026

#pragma once

#include "llvm/Support/raw_ostream.h"
#include <string>

namespace marai {

// Lance le serveur LSP sur stdin/stdout (JSON-RPC).
// Bloque jusqu'a reception de "exit".
int runLSP(const std::string &MaraiExePath);

} // namespace marai
