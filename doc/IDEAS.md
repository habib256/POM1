# Idées différées et décisions de périmètre

Ce document conserve les pistes qui ne constituent **pas** du travail ouvert. Elles
ne doivent revenir dans `TODO.md` qu’avec un besoin mesuré, un logiciel utilisateur
ou un oracle matériel permettant de définir un résultat vérifiable.

## Performance graphique

- **Décodage NTSC sur GPU** — le chemin CPU 280×192 n’est pas un goulot mesuré.
  Réévaluer seulement si un profil le montre ou si un post-traitement plein écran
  lourd est ajouté. Une réalisation devrait épingler la parité GLSL/MSL/WebGL.

## BASIC natif

- **Format numérique compact** — binary16 ou virgule fixe n’a pas encore de cas
  produit démontrant un bénéfice face au binary32. Si ce besoin apparaît, isoler
  format, runtime et symboles derrière `FpMode`.

## Matériel

- **Woz Machine floppy** — différée tant qu’aucun logiciel Apple-1 réel et aucune
  image exploitable ne justifient l’émulation GCR, des soft switches `$C0Ex` et de
  l’horloge asynchrone.
- **Joystick/paddle analogique** — hors périmètre depuis le 16 juin 2026 : le timer
  558 et les paddles sont du matériel Apple II, aucune carte Apple-1 équivalente ni
  aucun logiciel cible ne sont connus. La télémétrie numérique couvre le besoin
  actuel. Réexaminer uniquement si une carte Apple-1 réelle apparaît.
