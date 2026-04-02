# TODO

## Charmap originale

- [ ] Charger la ROM charmap originale (`charmap.rom`) pour le rendu des caractères
- [ ] Remplacer le rendu texte ImGui par un rendu bitmap basé sur la charmap
- [ ] Gérer les caractères inversés (bit 6) et le clignotement (bit 5)
- [ ] Respecter la matrice 5x7 pixels de la charmap Apple 1

## Module Cassette (ACI — Apple Cassette Interface)

- [ ] Émuler l'ACI (Apple Cassette Interface) à l'adresse $C000-$C0FF
- [ ] Implémenter les registres I/O cassette ($C100 pour la ROM ACI)
- [ ] Supporter la commande de lecture (load depuis fichier audio/binaire)
- [ ] Supporter la commande d'écriture (save vers fichier)
- [ ] Émuler le format de données Kansas City Standard (300 baud)
- [ ] Ajouter les entrées menu File > Load Tape / Save Tape
- [ ] Supporter le format `.wav` ou un format binaire simplifié
