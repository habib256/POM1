## Signature Windows (Authenticode)

POM1 peut être signé automatiquement pendant le packaging Windows via `package_windows_release.bat`.

### Pourquoi

- Réduit les alertes SmartScreen (avec un certificat reconnu, idéalement EV)
- Permet à Windows d’afficher l’éditeur dans les propriétés du fichier
- Assure l’intégrité du binaire distribué

### Pré-requis

- `signtool.exe` (Windows SDK / Visual Studio)
- Un certificat de **code-signing** :
  - **PFX** (`.pfx`) + mot de passe, ou
  - Certificat dans le **Windows Certificate Store** (souvent le cas des EV tokens)

### Mode 1 — PFX

Dans un terminal (PowerShell ou cmd) avant de lancer le packaging :

```bat
set POM1_CODESIGN_PFX=C:\chemin\vers\certificat.pfx
set POM1_CODESIGN_PFX_PASSWORD=VotreMotDePasse
cmd /c package_windows_release.bat
```

### Mode 2 — Certificat dans le store (thumbprint SHA-1)

```bat
set POM1_CODESIGN_SHA1=THUMBPRINTSANSESPACES
cmd /c package_windows_release.bat
```

Astuce pour trouver le thumbprint (PowerShell) :

```powershell
Get-ChildItem Cert:\CurrentUser\My | Select-Object Subject, Thumbprint, NotAfter
```

### Options

Variables facultatives :

```bat
set POM1_CODESIGN_TIMESTAMP_URL=http://timestamp.digicert.com
set POM1_CODESIGN_DESC=POM1 - Apple 1 Emulator
set POM1_CODESIGN_URL=https://github.com/habib256/POM1
```

### Notes

- Le timestamp (`/tr`) est important : la signature reste valide même après expiration du certificat.
- SmartScreen repose aussi sur la “réputation” : même signé, un nouvel exécutable peut encore déclencher un avertissement au début (surtout sans EV).
