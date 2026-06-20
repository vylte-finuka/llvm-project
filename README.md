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
// rel cl = closed = prive
rel cl nomFonction: [arg1 string, arg2 i32] [
    ret arg2;
];

// rel op = open = public
rel op nomFonction: [arg1 string] [
    ret arg1;
];

// Heritage de type
rel op NomClasse: <[string TypeParent]>t [
];
```

> Les arguments de `rel` sont toujours sans `< >` : `[name string]`

### Visibilite rel cl / rel op

| Qui appelle | `rel cl` | `rel op` |
| --- | --- | --- |
| Meme fichier `.mara` | oui | oui |
| Autre fichier du meme bundle | non | oui |
| Autre bundle `.marep` / `.slul` | non | oui |
| Runtime Slura OS (entry points) | oui | oui |

> `rel cl` au **niveau module** (ex. `OEntry`, `APrevent`) recoit `ExternalLinkage` LLVM
> meme si declare prive, afin que le runtime Slura OS puisse l'atteindre.
> `rel cl` **imbrique** dans une classe reste `InternalLinkage` (veritablement prive).

### Litteraux hexadecimaux

```mara
let color: <i32> = 0xFFFFFF;
let mask:  <i32> = 0xFF00FF;
```

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
    |
[1/5] MaratineLexer    -> tokens
    |
[2/5] MaratineParser   -> AST
    |
[3/5] MaratineSema     -> analyse semantique (types, portees, arite)
    |
[4/5] MaratineCodeGen  -> LLVM IR  (fonctions top-level : ExternalLinkage)
    |
[5/5] Optimizer O2     -> LLVM IR optimise (.ovc interne)
    |
marai build            -> bundle .marep / .slul (archive ZIP)
```

> Les fonctions `rel cl` au niveau module recoivent `ExternalLinkage` pour survivre
> au dead-code elimination de l'optimiseur O2 (entry points atteints par Slura OS).

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

## Installation de la toolchain

### Methode recommandee — `build-llvm-win.ps1`

```powershell
# Windows — build + install + variables d'environnement
.\build-llvm-win.ps1

# Options
.\build-llvm-win.ps1 -Prefix D:\mara   # repertoire d'installation
.\build-llvm-win.ps1 -Jobs 8            # parallelisme
.\build-llvm-win.ps1 -Clean             # nettoyage avant build
```

Apres installation dans `D:\maratine-install\` :

| Fichier | Role |
| --- | --- |
| `bin\maratine-cc.exe` | Compilateur Mara -> LLVM IR |
| `bin\marai.exe` | Outil principal (build, new, check, lsp, audit...) |
| `bin\marabug.exe` | Debogueur |
| `lib\maratine\base\` | Stdlib (44+ fichiers `.mara`) |
| `lib\maratine\templates\` | Templates projet `.marep` / `.slul` |
| `include\maratine\` | Headers C++ |

Variables d'environnement positionnes automatiquement :
- `MARATINE_HOME`, `MARATINE_STDLIB`, `MARATINE_TEMPLATES`, `PATH`

---

## marai — Outil principal

### Creer un projet

```bash
marai new MonApp.marep                  # application .marep depuis template
marai new MonDriver.slul                # driver .slul depuis template
marai new MonApp.marep --dir D:\Projets # dans un dossier specifique
marai new MonApp.marep --force          # ecraser si existant
```

Structure generee pour `MonApp.marep` :
```
MonApp.marep/
  base/
    OEntry.mara        <- point d'entree (rel cl)
    LAPrevent.mara     <- cycle de vie (rel op)
    HelloWorld.mara    <- composant exemple
    TemplateView.mara  <- vue principale
  MonApp.slasset/
    ResLayout.xml
    Slura_launcher icon.png
  Maraset.yaml         <- name: base***MonApp
  RAbstractallowing.xml
```

### Construire un projet

`marai build` lit `Maraset.yaml`, compile tous les `.mara` de `base/`,
copie les assets et produit le bundle final `.marep` ou `.slul`.

```bash
# Depuis le dossier du projet (auto-detection)
cd MonApp.marep  &&  marai build -O

# Depuis le dossier parent (detecte tous les .marep et .slul)
marai build -O

# Chemin explicite
marai build MonApp.marep -O --out-dir dist/

# Choix de l'architecture (arm64 = defaut Slura OS, x64 = dev/test)
marai build MonApp.marep -O --arch arm64
marai build MonApp.marep -O --arch x64
```

> Le format de sortie est **toujours** `.marep` ou `.slul`.
> Les `.ovc` sont des intermediaires internes empaquetes dans le bundle — jamais visibles.

Architecture cible :

| Valeur | Triple LLVM | Usage |
| --- | --- | --- |
| `arm64` *(defaut)* | `aarch64-unknown-none-elf` | Slura OS / Exynos W1000 (production) |
| `x64` | `x86_64-pc-windows-msvc` | Dev / tests sur PC Windows |

### Auditer un projet

```bash
marai check MonApp.marep -O        # verifie IR + entry-points + securite
marai check MonApp.marep -O --json # sortie JSON (CI)
marai check MonApp.marep --show-ir # affiche le LLVM IR de chaque fichier
```

### Serveur LSP (VSCode)

```bash
marai lsp   # demarre le serveur LSP sur stdin/stdout (JSON-RPC)
```

L'extension VSCode `maratine-language` lance automatiquement `marai lsp`.
Fonctionnalites : coloration syntaxique, diagnostics, completion hierarchique,
signature help, hover avec type et module d'origine, inlay hints.

### Autres commandes

```text
marai new      <NomProjet[.marep|.slul]>    Creer un projet depuis template
marai build    [projet...]  [-O] [--arch]   Compiler et packager
marai check    [projet...]  [-O] [--json]   Audit IR + securite
marai lsp                                    Serveur LSP (VSCode)
marai install  <pkg[@ver]>...               Installer des packages
marai update                                 Mettre a jour les packages
marai remove   <pkg>                         Supprimer un package
marai list     [--deps]                      Lister les packages
marai audit    [--aude] [--json]             Audit securite (Maralock.yaml)
marai abi      check <pkg>                   Verifier compatibilite MABI
marai version                                Afficher la version
```

### Diagnostics bas niveau (`maratine-cc`)

```bash
maratine-cc MonFichier.mara -dump-tokens          # tokens du lexer
maratine-cc MonFichier.mara -dump-ast             # AST complet
maratine-cc MonFichier.mara -emit llvm -o out.ll  # LLVM IR brut
maratine-cc MonFichier.mara -emit llvm -O         # LLVM IR optimise O2
maratine-cc MonFichier.mara -arch x64 -emit llvm  # cibler x64
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
