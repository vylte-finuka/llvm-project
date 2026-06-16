# Mara / Maratine — Compilateur & Runtime

**Mara** est le langage de programmation natif de **Slura OS**, compilé via un fork LLVM
ciblant ARM64. Ce dépôt contient le compilateur, la stdlib `/base/`, le moteur **MGC**
(Maratine Graphical Compute) et les templates de bundles applicatifs (`.marep`) et drivers (`.slul`).

Fabricant : **Vyft Ltd** — Projet Shixeixei ShiWear To1

---

## Présentation

| Élément | Valeur |
| --- | --- |
| Extension source | `.mara` |
| Bundle application | `.marep` |
| Bundle driver | `.slul` |
| Cible compilateur | ARM64 |
| OS cible | Slura OS (noyau Lunée) |
| Backend | Fork LLVM (ce dépôt, branche `main`) |
| Moteur 3D | MGC — Maratine Graphical Compute |

---

## Syntaxe Mara — Référence rapide

### Déclarations

```mara
var nomVar: <string> = "valeur";   // mutable
let nomConst: <i32> = 42;          // constante
```

### Types primitifs

```mara
<string>   // chaîne — toujours minuscule
<i32>      // entier 32 bits
<i64>      // entier 64 bits
<u64>      // entier non signé 64 bits
<bool>     // booléen
<ptr>      // pointeur opaque
<array>    // tableau
```

### Types composés nommés

```mara
<[string NomType]>
<[string NomType***[ TypeA, TypeB ]]>
```

### Fonctions

```mara
// rel cl = privé (InternalLinkage)
rel cl nomFonction: [arg1 string, arg2 i32] [
    ret arg2;
];

// rel op = public (ExternalLinkage)
rel op nomFonction: [arg1 string] [
    ret arg1;
];

// Héritage
rel op NomClasse: <[string TypeParent]>t [
];
```

> Les arguments de `rel` sont toujours sans `< >` : `[name string]`

### Conditionnelles et boucles

```mara
if (condition) [
    // corps
];

if (condition) [
    // vrai
] else [
    // faux
];

loop condition [
    i = i + 1;
];
```

### Imports — membre unique et multi-membres

```mara
// Module entier
#base <MaratineKit>;

// Un seul membre
#base <MaratineKit***MGC***MGCCore>;

// Plusieurs membres depuis le même chemin ([ ] avec virgules)
#base <std***core***self***[ ComponentView, RenderContext, TextLabel ]>;
#base <std***core***self***RegistrationRoot***[ KeyReg, SRIDAtt, PAccessAuth ]>;
#base <MaratineKit***MGC***[ MGC, MGCScene, MGCPhysics, MGCAudio ]>;
#base <std***[ MaraMem, MaraFS, MaraNet ]>;

// En-têtes C++ (pour les modules hybrides Mara/LLVM)
#include "llvm/Support/raw_ostream.h"
```

### Appels drivers / FFI

```mara
let result: <i32> = <DrvAPIInterCon***GpuFlushRenderContext***>(ctx);
let _: <i32> = <MaratineKit***RenderContext***Attach>(ctx, label);

log: "message " + variable;
```

---

## Exemple complet — HelloWorld.mara

```mara
#base <std***core***self***[ ComponentView, RenderContext, TextLabel ]>;
#base <MaratineKit>;

rel op HelloWorld: <[string ComponentView]>t [

    var message: <string> = "Bonjour Slura !";
    var bgColor: <string> = "#000000";
    var fgColor: <string> = "#FFFFFF";

    rel op Create: [ctx string] [
        let label: <[string TextLabel]> = <MaratineKit***UI***TextLabel***New>(
            message, fgColor, bgColor
        );
        let _: <i32> = <MaratineKit***UI***TextLabel***SetAlign>(label, "center");
        let _: <i32> = <MaratineKit***RenderContext***Attach>(ctx, label);
        ret label;
    ];

    rel op Render: [view string, ctx string] [
        let gpuResult: <i32> = <DrvAPIInterCon***GpuFlushRenderContext***>(ctx);
        if (gpuResult != 0) [
            log: "erreur rendu – " + gpuResult;
            ret gpuResult;
        ];
        ret 0;
    ];

    rel cl Destroy: [view string, ctx string] [
        let _: <i32> = <MaratineKit***RenderContext***Detach>(ctx, view);
        ret 0;
    ];
];
```

---

## Exemple jeu 3D — MGC (Maratine Graphical Compute)

```mara
#base <MaratineKit***MGC***[ MGC, MGCScene, MGCMesh, MGCPhysics, MGCParticle, MGCAudio ]>;

rel op GameLoop: <[string CycleActivity]>t [

    rel op Start: [] [
        let _: <i32> = <MGC***Init***>(-10, 0);
        let _: <i32> = <MGCScene***SetSkybox***>("skybox_desert");
        let _: <i32> = <MGCMesh***Load***>("car", "C:\Assets\car.mgcm");
        let _: <i32> = <MGCPhysics***AddVehicle***>("car", chassisHandle, 4, 30);
        let _: <i32> = <MGCParticle***CreateFromPreset***>("exhaust", PRESET_SMOKE, carPos);
        let _: <i32> = <MGCAudio***LoadStream***>("music", "C:\Audio\race.aac");
        let _: <i32> = <MGCAudio***Play2D***>("music", 1);
        ret 0;
    ];

    rel op Tick: [deltaMs i32] [
        ret <MGC***Tick***>(deltaMs);
    ];

    rel op Stop: [] [
        ret <MGC***Shutdown***()>;
    ];
];
```

---

## Structure du dépôt

```text
maratinec/
├── base/                                      ← stdlib Mara
│   ├── std/
│   │   ├── core/
│   │   │   ├── self/RegistrationRoot/         ← KeyReg, SRIDAtt, PAccessAuth
│   │   │   └── types/MaraTypes.mara           ← conversions, clamp, maths
│   │   ├── ComTpe/
│   │   │   ├── SlulFrmt/                      ← DrvAPIInterCon, DrvManSpec, MathSafety
│   │   │   └── MarepFrmt/                     ← MaraGrphclCpeAPI, DrvInstance, MathSafety
│   │   ├── mem/      MaraMem.mara             ← zones HEAP/STACK/SHARED/DMA/SECURE
│   │   ├── fs/       MaraFS.mara              ← SDC C:/D:/T:, accès sécurisé
│   │   ├── io/       MaraIO.mara              ← tactile, stylet, capteurs, caméra, torche
│   │   ├── net/      MaraNet.mara             ← LTE/WiFi/LoRa, HTTP, fallback auto
│   │   ├── crypto/   MaraCrypto.mara          ← AES-256-GCM, SHA-256, HMAC, TRNG
│   │   └── proc/     MaraProc.mara            ← Spawn/Kill/Pause/Resume, IPC
│   └── MaratineKit/                           ← runtime core
│       ├── RenRootUI.mara                     ← rendu UI (MAREP)
│       ├── RenRoot.mara                       ← rendu driver (SLUL)
│       ├── AuthARoot.mara
│       ├── CycleActivity.mara
│       ├── DrvInstance.mara
│       ├── PIDActivity/ObtInfo.mara
│       ├── LCom/                              ← Text, Font, LRenStyle, String
│       ├── NotifMgr.mara                      ← notifications 64 slots, alerte SOS
│       ├── StylusKit.mara                     ← stylet EMR, 4096 niveaux, modes
│       ├── HealthKit.mara                     ← pas, altitude, activité, objectifs
│       ├── WatchFaceKit.mara                  ← cadrans, complications, ambient
│       ├── BattPwrKit.mara                    ← batterie, solaire, thermoélec, LoRa
│       └── MGC/                               ← Maratine Graphical Compute
│           ├── MGC.mara                       ← façade unifiée (Init/Tick/Shutdown)
│           ├── MGCCore.mara                   ← pipeline GPU, resource manager, frame loop
│           ├── MGCShader.mara                 ← GLSL→SPIR-V, hot-reload, cache
│           ├── MGCTexture.mara                ← ASTC, HDR, cubemap, LRU VRAM 256 Mo
│           ├── MGCMesh.mara                   ← VBO/IBO, skinning 64 bones, LOD×4
│           ├── MGCRenderer.mara               ← PBR deferred+forward, SSAO, bloom ACES
│           ├── MGCCompute.mara                ← GPU compute : particules, culling, terrain
│           ├── MGCScene.mara                  ← scene graph 4096 entités, 64 lumières
│           ├── MGCPhysics.mara                ← corps rigides, véhicules, raycast
│           ├── MGCParticle.mara               ← 1M particules GPU, 7 presets
│           └── MGCAudio.mara                  ← HRTF 3D, 64 sources, streaming AAC
├── MaratineProjectAppTemplate.marep/          ← template application GUI
│   ├── base/
│   │   ├── OEntry.mara                        ← entrée privée (RenRootUI)
│   │   ├── LAPrevent.mara                     ← cycle de vie (AppLifecycle)
│   │   ├── TemplateView.mara                  ← vue racine GUI
│   │   └── HelloWorld.mara                    ← composant démo
│   ├── MaratineProjectAppTemplate.slasset/
│   │   └── ResLayout.xml
│   ├── Maraset.yaml
│   └── RAbstractallowing.xml
├── MaratineProjectAppTemplate.slul/           ← template driver
│   ├── base/
│   │   ├── OEntry.mara                        ← entrée privée (RenRoot)
│   │   └── APrevent.mara                      ← cycle de vie (CycleActivity)
│   ├── Maraset.yaml
│   └── RAbstractallowing.xml
└── maratine/                                  ← compilateur C++ (fork LLVM)
    ├── include/
    │   ├── MaratineAST.h
    │   ├── MaratineParser.h
    │   ├── MaratineSema.h
    │   └── MaratineCodeGenAction.h
    ├── lib/
    │   ├── MaratineLexer.cpp
    │   ├── MaratineParser.cpp
    │   ├── MaratineSema.cpp
    │   ├── MaratineAST.cpp
    │   └── MaratineCodeGenAction.cpp
    ├── tools/maratine-cc.cpp
    └── CMakeLists.txt
```

---

## Architecture du compilateur

```text
Source .mara
    ↓
[1/4] MaratineLexer     → tokens
    ↓
[2/4] MaratineParser    → AST
    ↓
[3/4] MaratineSema      → analyse sémantique (types, portées, arité)
    ↓
[4/4] MaratineCodeGen   → LLVM IR → objet ARM64
    ↓
Bundle .marep / .slul
```

---

## MGC — Maratine Graphical Compute

Moteur 3D GPU-first intégré à MaratineKit. Un seul `.marep` tourne en **montre 454×454**
et en **desktop 1080p** (dock HDMI) — MGC détecte le mode automatiquement.

| Sous-système | Capacité |
| --- | --- |
| Rendu | PBR deferred+forward, IBL, SSAO, bloom, tone-mapping ACES |
| Particules | 1 000 000 simultanées, GPU-driven (SSBO), 7 presets |
| Physique | Corps rigides, véhicules, tissu, BVH broadphase, substeps |
| Audio | HRTF binaurale 3D, 64 sources, streaming AAC, réverb DSP |
| Shaders | GLSL → SPIR-V, cache persistant, hot-reload sans stopper |
| Textures | ASTC auto, LRU VRAM 256 Mo, cubemap, HDR |
| Scène | 4096 entités, 64 lumières, frustum culling GPU |

---

## Build

### Dépendances

- CMake 3.20+
- C++17
- LLVM (ce fork — branche `main`)

### Compiler le compilateur Mara

```bash
cmake -S llvm -B build -G Ninja \
  -DLLVM_ENABLE_PROJECTS="maratine" \
  -DCMAKE_BUILD_TYPE=Release \
  -DLLVM_TARGETS_TO_BUILD=AArch64
cmake --build build --target maratine-cc
```

### Compiler un fichier `.mara`

```bash
# Vers LLVM IR
./build/bin/maratine-cc MonApp.mara -emit llvm -o MonApp.ll

# Vers objet ARM64
./build/bin/maratine-cc MonApp.mara -emit obj -o MonApp.o

# Dump tokens
./build/bin/maratine-cc MonApp.mara -dump-tokens

# Dump AST
./build/bin/maratine-cc MonApp.mara -dump-ast
```

---

## Règles de code Mara

- `let` / `var` prennent **toujours** `<type>` entre chevrons
- Arguments de `rel` **sans** chevrons : `[name string]`
- Héritage avec `t` : `<[string TypeParent]>t`
- `if` avec `[ ]` — jamais `then`
- `loop` avec `[ ]`
- `string` toujours minuscule
- `ret` — pas `return`
- Import multi-membres : `#base <chemin***[ A, B, C ]>;`
- Ne jamais mettre de référence hardware dans les commentaires

---

## Licence

Proprietary — Vyft Ltd — 2026

Pour toute question : `support@vyft.io`
