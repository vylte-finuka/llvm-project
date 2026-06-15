# 🚀 Maratine - Langage de Programmation Bas Niveau pour Slura

**Maratine** est un langage de programmation moderne inspiré de Swift pour développer des applications sur **Slura**, votre OS hybride mobile et PC.

## 📋 Caractéristiques

### Syntaxe Principale

```maratine
// Extension source: *.mart
// Extension compilée: *.ovc (Vyft Compiled Output)

std***maratine_sdk***MaratineUi;          // Imports std
std***maratine_sdk***{View, Text};

rel op HelloFunction[                      // rel = fonction, op = public
    let message: "Hello World!"            // let = variable
    
    if sendit [                            // if = condition
        log: message                       // log = affichage
    ]
]

#[MaratineUi]                             // Décorateur

<View><Text>Hello World!</Text></View>    // Composants UI
```

### Modèles de Visibilité

| Mot-clé | Signification | Linkage LLVM |
|---------|---------------|--------------|
| `op` | Public/Opération | `ExternalLinkage` |
| `cl` | Privé/Classe | `InternalLinkage` |
| `rel` | Fonction/Relation | Spécifié par `op`/`cl` |

### Structures de Données

- **Variables**: `let name: type = value`
- **Fonctions**: `rel [op/cl] functionName[ body ]`
- **Conditions**: `if condition [ body ]`
- **Affichage**: `log: value`
- **UI**: `<ComponentName>...</ComponentName>`

## � Architecture LLVM

```
Maratine Source (.mart)
    ↓
┌─────────────────┐
│ Lexer           │  → Tokens
│ MaratineLexer   │
└─────────────────┘
    ↓
┌─────────────────┐
│ Parser          │  → AST
│ MaratineParser  │
└─────────────────┘
    ↓
┌─────────────────┐
│ Code Generator  │  → LLVM IR
│ MaratineCodeGen │
└─────────────────┘
    ↓
┌──────────────────┐
│ OVC Builder      │  → .ovc (Vyft Compiled)
│ MaratineOVC      │
└──────────────────┘
    ↓
LLVM Backend (clang/llc)
    ↓
Binary (iOS/Android/PC/Slura)
```

## 📦 Structure du Projet

```
maratine/
├── include/
│   ├── MaratineLexer.h       # Lexer tokens
│   ├── MaratineAST.h         # Abstract Syntax Tree
│   ├── MaratineParser.h      # Parser
│   └── MaratineCodeGen.h     # Code Generator LLVM
├── lib/
│   ├── MaratineLexer.cpp
│   ├── MaratineParser.cpp
│   └── MaratineCodeGen.cpp
├── tools/
│   └── maratine-cc.cpp       # Compiler Driver
├── test/
│   ├── fixtures/
│   │   ├── hello.mart
│   │   ├── lexer_test.mart
│   │   └── parser_test.mart
│   └── CMakeLists.txt
└── CMakeLists.txt
```

## 🔨 Installation & Compilation

### Dépendances
- LLVM 15+ (avec clang)
- CMake 3.20+
- C++17

### Build

```bash
cd maratine
mkdir build
cd build
cmake -DLLVM_DIR=$(llvm-config --cmakedir) ..
cmake --build . --config Release
```

### Utilisation

```bash
# Compiler un fichier Maratine vers .ovc (défaut)
./maratine-cc hello.mart -o hello.ovc

# Compiler vers LLVM IR
./maratine-cc hello.mart -emit llvm -o hello.ll

# Avec optimisations
./maratine-cc hello.mart -O -o hello_opt.ovc

# Dump tokens
./maratine-cc hello.mart -dump-tokens

# Dump AST
./maratine-cc hello.mart -dump-ast

# Générer objet (TODO)
./maratine-cc hello.mart -emit obj -o hello.o

# Générer exécutable depuis .ovc
llc -filetype=obj hello.ovc -o hello.o
clang hello.o -o hello
./hello
```

## 📚 Exemples

### Hello World Simple

```maratine
// hello.mart
rel op main[
    let msg: "Hello, Maratine!"
    if sendit [
        log: msg
    ]
]
```

### Application avec UI

```maratine
std***maratine_sdk***{View, Text, Button};

#[MaratineApp]
rel op App[
    <View>
        <Text>Bienvenue sur Slura</Text>
        <Button>Cliquer ici</Button>
    </View>
]
```

### Module avec fonctions

```maratine
// Fonction publique
rel op add(a: i32, b: i32) -> i32[
    ret a + b
]

// Fonction privée
rel cl helper(x: i32)[
    let result: x * 2
    log: result
]
```

## 🎯 Plateforme Cible: Slura

Maratine génère du LLVM IR optimisé pour:

- **iOS** (ARM64)
- **Android** (ARM64/x86)
- **macOS** (ARM64/x86_64)
- **Windows** (x64)
- **Slura OS** (ARM64 hybride + Extensions)

### Configuration Triple LLVM

```cpp
// Pour Slura (exemple)
"arm64-slura-hybrid" 
"arm64-apple-ios15.0"
"aarch64-linux-android29"
```

## 🧪 Tests

```bash
ctest --verbose
```

## 📖 Génération LLVM IR

### Entrée Maratine
```maratine
rel op fibonacci(n: i32) -> i32[
    let result: 0
    if n [
        log: result
    ]
]
```

### Sortie LLVM IR
```llvm
; ModuleID = 'maratine_module'
target triple = "arm64-apple-darwin"

declare i32 @printf(i8*, ...)

define external i32 @fibonacci(i32 %n) {
entry:
  %result = alloca i32, align 4
  store i32 0, i32* %result, align 4
  br i1 %n, label %then, label %ifcont

then:
  %0 = load i32, i32* %result, align 4
  call i32 (i8*, ...) @printf(i8* getelementptr inbounds ([%s], ...))
  br label %ifcont

ifcont:
  ret i32 0
}
```

## 🚀 Roadmap

- [x] Lexer complet
- [x] Parser AST
- [x] Code generation LLVM IR
- [ ] Optimisations LLVM
- [ ] Génération Assembly
- [ ] Linking objet
- [ ] Runtime Maratine (stdlib)
- [ ] Support plein UI
- [ ] Async/Await
- [ ] Generics
- [ ] Pattern Matching
- [ ] Modules & Namespaces avancés

## 📄 Licence

Vyft Ltd - 2026

## 👥 Contributeurs

- **Vyft Ltd** - Maratine Language Design & Implementation

---

**Status**: Pre-Alpha Development 🔧

Pour des questions ou contributions: support@vyft.io
