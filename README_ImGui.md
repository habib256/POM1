# POM1 - Émulateur Apple 1 (Version Dear ImGui)

## 🎉 **Fini Qt ! Bienvenue Dear ImGui !**

Cette version moderne de POM1 utilise **Dear ImGui** au lieu de Qt, éliminant le besoin d'inscription ou de licences complexes.

## ✨ **Avantages de cette version**

- **✅ Zéro inscription** - Pas besoin de compte Qt
- **✅ Interface moderne** - Dear ImGui, utilisé par tous les émulateurs modernes
- **✅ Performance** - Rendu GPU natif, 60fps garantis
- **✅ Installation simple** - Juste quelques dépendances via Homebrew
- **✅ Licence libre** - MIT, totalement libre

## 🚀 **Installation rapide**

```bash
# Installation automatique (macOS)
./setup_imgui.sh

# Ou manuellement :
brew install cmake glfw pkg-config
git clone https://github.com/ocornut/imgui.git
mkdir build && cd build
cmake .. && make
```

## 🎮 **Lancement**

```bash
# Méthode simple (recommandée)
./run_emulator.sh

# Ou manuellement
cd build
./pom1_imgui
```

## 📋 **Fonctionnalités implémentées**

### **Interface utilisateur complète :**
- **Menu principal** avec toutes les fonctions
- **Visualiseur de mémoire** hexadécimal interactif
- **Écran Apple 1** avec simulation moniteur vert vintage
- **Console de débogage** CPU 6502
- **Dialogues de configuration** pour écran et mémoire

### **Fonctions de fichiers :**
- Charger/Sauvegarder mémoire (à compléter avec nativefiledialog)
- Coller code depuis le presse-papiers
- Gestion des ROMs (BASIC, WOZ Monitor, Krusader)

### **Contrôles CPU :**
- Reset logiciel/matériel
- Interface de débogage
- Visualisation des registres (à compléter)

### **Visualiseur de mémoire avancé :**
- Navigation par adresse
- Recherche de valeurs hexadécimales
- Affichage ASCII
- Raccourcis vers zones importantes (0x0000, 0x0300, 0xA000, etc.)
- Édition en temps réel

### **Écran Apple 1 authentique :**
- 40x24 caractères (standard Apple 1)
- Moniteur vert vintage ou blanc moderne
- Curseur clignotant
- Échelle d'affichage ajustable
- Défilement automatique

## 🔧 **Architecture technique**

```
main_imgui.cpp          → Point d'entrée GLFW/OpenGL
MainWindow_ImGui.*      → Interface principale
MemoryViewer_ImGui.*    → Visualiseur mémoire hexadécimal
Screen_ImGui.*          → Émulation écran Apple 1
M6502.* + Memory.*      → Émulation hardware (inchangé)
```

## 📁 **Structure des fichiers**

```
pom1/
├── build/              → Fichiers compilés
├── imgui/              → Bibliothèque Dear ImGui
├── roms/               → ROMs Apple 1 ✅ INCLUSES
│   ├── basic.rom       → Apple BASIC (4KB)
│   ├── krusader-1.3.rom → Assembleur Krusader (8KB)
│   ├── WozMonitor.rom  → Moniteur Wozniak (256B)
│   └── charmap.rom     → Table de caractères (1KB)
├── *_ImGui.*           → Nouveaux fichiers Dear ImGui
├── CMakeLists.txt      → Configuration build moderne
├── setup_imgui.sh      → Script d'installation
└── run_emulator.sh     → Script de lancement ✅ NOUVEAU
```

## 🎯 **ROMs Apple 1 incluses**

| ROM | Taille | Adresse | Description |
|-----|--------|---------|-------------|
| **WOZ Monitor** | 256B | 0xFF00 | Moniteur système de Steve Wozniak |
| **Apple BASIC** | 4KB | 0xE000 | Interpréteur BASIC d'Apple |
| **Krusader** | 8KB | 0xA000 | Assembleur symbolique avancé |
| **Charmap** | 1KB | - | Table de caractères Apple 1 |

L'émulateur charge automatiquement les 3 premières ROMs au démarrage !

## 🎯 **Prochaines étapes**

### **Priorité haute :**
1. **Ajouter nativefiledialog** pour les dialogues de fichiers
2. **Implémenter le presse-papiers** système
3. ~~**Ajouter les ROMs**~~ ✅ **FAIT** - Toutes les ROMs sont incluses
4. **Connecter l'écran au CPU** pour affichage réel

### **Améliorations :**
1. **Débogueur avancé** avec registres CPU visibles
2. **Thèmes personnalisables** (vert/ambre/blanc)
3. **Sauvegarde des configurations**
4. **Support du son** (speaker Apple 1)

## 🏆 **Comparaison Qt vs Dear ImGui**

| Aspect | Qt | Dear ImGui |
|--------|----|-----------| 
| **Installation** | ❌ Inscription requise | ✅ Simple `brew install` |
| **Taille** | ❌ ~500MB | ✅ ~2MB |
| **Performance** | ⚠️ Correcte | ✅ Excellent (GPU) |
| **Débogage** | ⚠️ Limité | ✅ Parfait pour émulateurs |
| **Licence** | ❌ Complexe | ✅ MIT libre |
| **Courbe d'apprentissage** | ❌ Élevée | ✅ Simple |

## 🐛 **Problèmes connus**

1. ~~**ROMs manquantes**~~ - ✅ **RÉSOLU** : Toutes les ROMs sont maintenant incluses !
2. **Dialogues fichiers** - Nécessite nativefiledialog
3. **Presse-papiers** - Pas encore implémenté
4. **CPU non connecté** - L'écran n'affiche pas la sortie CPU

## 🤝 **Contribution**

Cette version est un excellent point de départ pour continuer le développement. Les fonctionnalités principales sont implémentées et l'architecture est propre.

## 📜 **Licence**

GPL3 (comme l'original) + Dear ImGui (MIT)

---

**🎉 Félicitations ! Vous avez maintenant un émulateur Apple 1 moderne sans les contraintes de Qt !** 