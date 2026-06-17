# **Rapport d'Analyse Exhaustive : Architecture, Ingénierie et Interfaçage de l'Imprimante SWTPC PR-40 avec le Micro-Ordinateur Apple I**

## **Contexte Historique et Émergence de la Micro-Informatique en 1976**

L'année 1976 constitue une époque charnière dans l'évolution de l'ingénierie informatique, marquant la transition des ordinateurs centraux (mainframes) inaccessibles vers des systèmes micro-informatiques personnels conçus pour les ingénieurs, les passionnés et les petites entreprises.1 Au cœur de cette révolution, la question de l'extraction des données sous une forme tangible et pérenne représentait un défi technologique et financier considérable. À l'époque, le standard de facto pour l'interaction avec un système informatique et l'obtention d'une copie papier (hard copy) reposait sur l'utilisation de téléimprimeurs, le modèle le plus emblématique étant le Teletype Model 33 ASR.3

Bien que robustes, ces téléimprimeurs présentaient des inconvénients majeurs pour le marché naissant de la micro-informatique. Ils étaient excessivement coûteux (souvent plus chers que l'ordinateur lui-même), extrêmement bruyants, encombrants, et nécessitaient une interface de communication série complexe, généralement basée sur la norme RS-232C ou sur une boucle de courant de 20 milliampères (20 mA).3 Pour un utilisateur ayant investi dans un système tel que l'Altair 8800, le SWTPC 6800 ou l'Apple I, l'acquisition d'un tel équipement constituait une barrière à l'entrée insurmontable.3 C'est précisément pour combler cette lacune que la Southwest Technical Products Corporation (SWTPC), basée à San Antonio au Texas, a conceptualisé et commercialisé l'imprimante alphanumérique PR-40.6

Introduite au milieu de l'année 1976, la PR-40 a bouleversé le marché en offrant une solution d'impression matricielle à impact sous forme de kit à assembler, pour le prix exceptionnellement bas de 250 dollars.7 Ce positionnement tarifaire agressif a rendu la copie papier accessible à une vaste frange d'utilisateurs. Parallèlement, dans la Silicon Valley, Steve Wozniak et Steve Jobs présentaient l'Apple I au sein du Homebrew Computer Club.9 Contrairement aux systèmes concurrents qui s'appuyaient souvent sur des panneaux de commutateurs et de diodes pour l'interaction utilisateur, l'Apple I intégrait nativement un terminal vidéo.3 Cependant, la carte mère de l'Apple I était livrée nue, sans boîtier, sans alimentation, sans clavier et, surtout, sans port d'imprimante parallèle standard.1

L'absence de port dédié sur l'Apple I nécessitait une ingénierie créative pour connecter des périphériques externes. En octobre 1976, Steve Jobs a publié un article fondamental intitulé « Interfacing the Apple Computer » dans le magazine *Interface Age* (Volume 1, Numéro 11, page 65).5 Cet article détaillait une méthodologie matérielle astucieuse permettant de contourner les limitations de la carte mère de l'Apple I pour y greffer l'imprimante SWTPC PR-40. Cette solution reposait sur l'interception directe des signaux vidéo et sur la manipulation des niveaux logiques TTL (Transistor-Transistor Logic), évitant ainsi la nécessité de développer des cartes d'extension coûteuses ou de modifier le code résidant en mémoire morte (ROM).8 L'analyse de cette interface requiert une déconstruction minutieuse de l'architecture électromécanique de la PR-40, de la logique d'entrée/sortie de l'Apple I, et des contraintes temporelles régissant la communication asynchrone entre ces deux entités.

## **Ingénierie Électromécanique et Architecture de l'Imprimante SWTPC PR-40**

La SWTPC PR-40 n'est pas une simple imprimante ; elle représente un exercice d'optimisation visant à réduire la complexité mécanique tout en maintenant une compatibilité universelle avec les microprocesseurs 8 bits de l'époque.11

### **Principes Mécaniques de l'Impression Matricielle**

Le mécanisme d'impression de la PR-40 repose sur la technologie matricielle à impact. Plutôt que d'utiliser des caractères préformés (comme sur une machine à écrire ou une imprimante à marguerite), la PR-40 génère chaque glyphe en propulsant des aiguilles métalliques contre un ruban encreur, déposant ainsi des points sur le papier.5 La matrice de résolution est de 5 points de largeur sur 7 points de hauteur.5 Cela signifie que chaque caractère est synthétisé à partir d'un sous-ensemble de 35 points potentiels, une densité suffisante pour garantir la lisibilité tout en minimisant la charge de calcul et le nombre d'actionneurs requis.5

Le mécanisme est actionné par sept solénoïdes individuels. L'imprimante est conçue pour traiter un jeu restreint de 64 caractères ASCII standard, incluant 26 lettres de l'alphabet (exclusivement en majuscules), 10 chiffres arabes, ainsi que 28 caractères spéciaux et signes de ponctuation (tels que le signe égal, les parenthèses ou les signes monétaires).5 L'absence de caractères minuscules, bien qu'étant une limitation évidente par rapport aux standards modernes, était une pratique courante en 1976 pour économiser la mémoire morte (ROM) allouée au générateur de caractères, une philosophie partagée par l'Apple I lui-même.3

La mécanique de la tête d'impression se déplace le long d'un cylindre en plastique massif. Les manuels d'assemblage de SWTPC mettaient d'ailleurs en garde les constructeurs de kits contre la fragilité de cette architecture : une rotation manuelle du cylindre vers l'avant de l'imprimante risquait de plier irrémédiablement le bras du microrupteur (microswitch) à came situé sur le flanc droit du mécanisme.11 Ce microrupteur, câblé via un connecteur Molex à 6 broches (la broche 3 pour la cosse supérieure, la broche 1 pour la cosse centrale, et la broche 2 pour la cosse inférieure), était critique pour synchroniser le mouvement de la tête et détecter les fins de course.11

### **Dimensionnement, Châssis et Alimentation**

Contrairement aux imprimantes de bureau conventionnelles exploitant des feuilles de format A4 ou 8,5 x 11 pouces, la PR-40 a été pensée pour imprimer sur un support en rouleau continu.5 Le papier utilisé, d'une largeur standard de 3,875 pouces (soit 3 7/8 pouces), était identique à celui employé par les machines à calculer industrielles et les caisses enregistreuses de l'époque.5 Ce choix logistique garantissait aux utilisateurs un approvisionnement aisé et économique en fournitures dans n'importe quelle papeterie.11 Les rubans encreurs, qui disposaient d'un mécanisme d'inversion automatique de direction pour maximiser leur durée de vie, étaient commercialisés au prix de 4 dollars l'unité.6

L'ensemble des composants électromécaniques est solidement arrimé à un châssis en aluminium anodisé noir, mesurant 9 5/8 pouces de largeur, 10 1/2 pouces de profondeur et 8 3/4 pouces de hauteur.7 Le châssis fait également office de dissipateur thermique partiel.

| Spécification Matérielle | Détail Technique |
| :---- | :---- |
| **Méthode d'impression** | Matricielle à impact, résolution 5 x 7 points 5 |
| **Vitesse de frappe** | 75 lignes par minute 7 |
| **Format de ligne** | 40 caractères par ligne 7 |
| **Jeu typographique** | 64 caractères ASCII (majuscules) 5 |
| **Support d'impression** | Rouleau de papier pour calculatrice, 3,875 pouces 5 |
| **Mémoire tampon** | Registre FIFO interne de 40 caractères 7 |
| **Alimentation électrique** | Intégrée, 115/230 Volt AC, 50-60 Hz 7 |
| **Interface de communication** | Bus parallèle 8 bits, compatibilité TTL 7 |
| **Dimensions hors-tout** | 9,625" (L) x 10,5" (P) x 8,75" (H) 7 |
| **Prix d'introduction (1976)** | 250 $ (format kit) 7 |

### **Architecture de la Logique de Contrôle et Cartes Imprimées**

L'intelligence électronique de la PR-40 est répartie sur deux circuits imprimés principaux, la carte logique PR-40A et la carte de puissance PR-40B, conçues pour être assemblées par l'utilisateur final à l'aide d'un fer à souder de faible puissance.11

La carte PR-40A concentre le traitement des signaux et le pilotage des solénoïdes. Elle intègre quinze circuits intégrés standards de la famille logique TTL série 74 (référencés IC3 à IC17).11 Plus remarquable pour l'époque, elle abrite deux puces à semi-conducteurs à oxyde métallique (MOS), désignées IC1 et IC2, qui constituent le cœur du traitement asynchrone et de la mémoire tampon.11 La vulnérabilité inhérente de la technologie MOS aux décharges électrostatiques (ESD) était telle que le manuel de montage recommandait impérativement au constructeur de relier son corps et ses outils à la terre via une résistance de 1 Mégohm avant toute manipulation, sous peine de détruire le circuit de manière irréversible.11

L'étage d'amplification de puissance, chargé de convertir les signaux logiques de bas niveau en impulsions de courant suffisantes pour actionner les sept solénoïdes de frappe, est constitué d'une batterie de transistors de puissance (Q1 à Q7).11 Pour prévenir une surchauffe locale, ces composants devaient être montés verticalement, surélevés d'environ 3/16 de pouce par rapport à la surface de la carte.11 Une ligne de sept fusibles dédiés (F1 à F7) protège individuellement chaque solénoïde.11 Cette redondance de sécurité prévenait la destruction des bobines dans l'éventualité où une défaillance logique ou un plantage du système hôte maintiendrait un signal de commande à l'état haut de manière continue. L'accès aux signaux d'interface s'effectue via des connecteurs Molex femelles (J3 et J4), dont les détrompeurs devaient impérativement pointer vers le bord de la carte lors du soudage.11

La carte PR-40B gère la rectification et la régulation de l'alimentation électrique. Elle s'interface avec un lourd transformateur T1 fixé au châssis par des vis \#8-32, capable d'être configuré pour des réseaux 120 V ou 240 V.11 Sur les réseaux 120 V nord-américains, la documentation imposait de lier les fils noir et marron-blanc, garantissant le bon adressage des bobinages primaires.11 Le filtrage principal de l'ondulation résiduelle après le pont redresseur est confié à un condensateur électrolytique de forte capacité (C4), trop volumineux pour être monté sur le circuit imprimé. Il est maintenu sur la face interne du panneau frontal par des colliers de serrage, sa borne négative orientée vers le bas pour des raisons de routage des câbles.11 La régulation thermique est assurée par le transistor Q2, vissé directement et à plat contre la surface de la carte PR-40B pour maximiser le transfert thermique.11

### **Logique d'Interface, Handshaking et Tampon FIFO**

L'approche conceptuelle de la PR-40 repose sur une compatibilité universelle, justifiant l'absence d'une interface sérielle RS-232C coûteuse au profit d'une interface parallèle à niveaux logiques TTL.7 Pour s'adapter aux vitesses d'exécution disparates des microprocesseurs hôtes (le 6502 de MOS Technology, le 6800 de Motorola ou le 8080 d'Intel), la PR-40 incorpore sa propre mémoire tampon de ligne, organisée sous forme de file d'attente FIFO (First-In, First-Out) d'une capacité stricte de 40 caractères.7

Cette architecture tamponnée décharge l'ordinateur hôte de la gestion du timing de frappe de chaque aiguille. L'ordinateur peut déverser les caractères ASCII vers l'imprimante à une fréquence fulgurante, atteignant jusqu'à un million de caractères par seconde (1 MHz), jusqu'à saturation du tampon.7 Le cycle mécanique d'impression ne s'amorce que lorsque la logique interne de la PR-40 détecte l'une des deux conditions suivantes : la réception du quarantième caractère (remplissage complet du tampon), ou la réception explicite du code de contrôle ASCII correspondant au "Retour Chariot" (Carriage Return, valeur hexadécimale $0D).7

Pour orchestrer ce flux, la communication s'appuie sur deux signaux de contrôle fondamentaux, établissant un protocole de poignée de main (handshaking) matériel. Le connecteur principal d'entrée des données, le connecteur J4, a fait l'objet d'analyses minutieuses au sein des communautés de rétro-ingénierie, permettant d'établir le brochage suivant 11 :

| Broche du Connecteur J4 (Molex) | Correspondance Fonctionnelle | Type de Signal TTL |
| :---- | :---- | :---- |
| **J4-1 ou J4-4** | Masse (Ground) | Référence électrique 11 |
| **J4-2** | Data Accepted (Acquittement) | Sortie de l'imprimante (Handshake) 14 |
| **J4-3** | Data Ready (Strobe) | Entrée vers l'imprimante (Handshake) 14 |
| **J4-5** | Bit ASCII 5 | Entrée de donnée 11 |
| **J4-6** | Bit ASCII 6 (MSB du jeu 64 car.) | Entrée de donnée 11 |
| **J4-7** | Non Connecté | N/A 11 |
| **J4-8** | Bit ASCII 3 | Entrée de donnée 11 |
| **J4-9** | Bit ASCII 4 | Entrée de donnée 11 |
| **J4-10** | Bit ASCII 0 (LSB) | Entrée de donnée 11 |
| **J4-11** | Bit ASCII 1 | Entrée de donnée 11 |
| **J4-12** | Bit ASCII 2 | Entrée de donnée 11 |

*À noter : Bien que les manuels de SWTPC comportent parfois des erreurs typographiques répertoriant J4-2 à la fois comme la masse et comme le signal Data Accepted, l'ingénierie inversée confirme que J4-2 est la ligne de retour d'état, tandis que la masse se trouve sur J4-1 ou J4-4*.15

Le protocole exige que l'hôte présente le code ASCII stabilisé sur les sept lignes de données, puis initie le transfert en abaissant la tension de la ligne Data Ready (broche J4-3) au niveau logique bas (0) pendant une durée minimale d'une microseconde.6 La ligne J4-3 est protégée par une résistance de tirage (pull-up resistor) interne R12 reliée au \+5V, garantissant qu'elle ne soit pas déclenchée accidentellement par un signal flottant.16 Dès réception de ce strobe négatif, l'imprimante réagit en passant sa ligne de sortie Data Accepted (broche J4-2) à l'état bas.6 Cette ligne est conçue avec une grande capacité de sortance (fan-out), capable de piloter simultanément jusqu'à dix charges TTL standard.6

L'hôte doit alors ramener Data Ready à l'état haut. Le caractère est verrouillé dans le tampon, et l'imprimante maintient Data Accepted à l'état bas si elle effectue l'impression mécanique (qui dure près de 0,8 seconde pour une ligne complète à 75 lignes/minute). Le signal Data Accepted ne remonte à l'état haut (niveau 1\) que lorsque la mécanique est de nouveau prête à recevoir un flux de données dans son tampon.6 Ce système robuste garantit une transmission sans perte, condition sine qua non pour un périphérique sans mémoire de masse.

## **L'Architecture Matérielle d'Entrée/Sortie de l'Apple I**

Pour appréhender la brillance et les limitations du hack proposé par Steve Jobs, une compréhension exhaustive de l'architecture d'entrée/sortie de la carte mère de l'Apple I s'impose. La philosophie de conception de Steve Wozniak reposait sur l'économie absolue de composants, cherchant à maximiser les capacités d'un système avec le minimum de circuits intégrés.17

### **Le Rôle Central du Motorola MC6820 PIA**

L'Apple I est articulé autour du microprocesseur MOS Technology 6502\. Curieusement, la gestion des périphériques externes (clavier et affichage vidéo) n'est pas confiée à une puce MOS, mais à un composant Motorola : le MC6820 Peripheral Interface Adapter (PIA).18 Cette hybridation s'explique par la genèse du 6502, conçu par Chuck Peddle et Bill Mensch, des transfuges de Motorola ayant apporté avec eux la compatibilité avec les bus périphériques de la famille 6800\. Bill Mensch étant le concepteur original du PIA 6820 chez Motorola, la synergie matérielle était parfaite.21

Le PIA 6820, emballé dans un boîtier DIP (Dual In-line Package) à 40 broches, offre l'équivalent de 20 lignes d'entrée/sortie.18 Ces lignes sont structurées en deux ports bidirectionnels de 8 bits chacun (Port A et Port B), complétés par quatre lignes de contrôle matériel dédiées (CA1, CA2 pour le Port A, et CB1, CB2 pour le Port B) destinées à la gestion des interruptions et des protocoles de poignée de main.18

Le comportement de chaque broche est programmable logiciellement via un ensemble de six registres internes accessibles par le CPU 6502 :

1. **DDRA / DDRB (Data Direction Registers)** : Ils définissent la direction de chaque broche (bit à 0 pour une entrée, bit à 1 pour une sortie).19  
2. **CRA / CRB (Control Registers)** : Ils configurent les lignes de contrôle CA1/2 et CB1/2, gèrent les interruptions matérielles et sélectionnent l'accès entre les registres de direction et les registres de données.18  
3. **ORA / ORB (Output Registers) / Peripheral Data Registers** : Ce sont les tampons qui stockent les données écrites vers l'extérieur ou lisent l'état des signaux entrants.19

Sur la carte mère de l'Apple I, le PIA 6820 est positionné physiquement à l'emplacement identifié A4.8 Wozniak a assigné une architecture stricte à ce composant : le Port A (broches PA0 à PA7) est exclusivement dédié à l'acquisition des données en provenance du clavier ASCII matériel 18, tandis que le Port B (broches PB0 à PB7) est orienté vers la transmission des données à la section d'affichage vidéo.8 Le registre du PIA est mappé en mémoire à l'adresse de base $D010.19

### **La Sous-Section du Terminal Vidéo et ses Contraintes Temporelles**

La gestion de l'affichage vidéo sur l'Apple I constitue un chef-d'œuvre de minimalisme numérique, générant un signal vidéo composite monochrome directement exploitable par un moniteur ou un téléviseur modifié.26 En 1976, la mémoire vive statique (SRAM) offrant des temps d'accès suffisamment courts pour rafraîchir un balayage cathodique était extrêmement dispendieuse. Wozniak a donc contourné ce problème en n'utilisant pas de mémoire vidéo conventionnelle mappée dans l'espace d'adressage du CPU.28

À la place, l'architecture vidéo repose sur un arrangement séquentiel de six registres à décalage dynamiques (dynamic shift registers) Signetics de type 2504, capables de stocker chacun 1024 bits.28 Ces registres agissent comme des conduites à retard circulaires. L'écran étant configuré pour afficher 24 lignes de 40 colonnes, cela requiert 960 emplacements de mémoire.29 Les 64 emplacements résiduels (1024 \- 960\) correspondent au temps de retour de faisceau vertical (retrace) du tube cathodique et constituent une zone "invisible" astucieusement mise à profit pour exécuter le défilement matériel (scrolling) de l'écran ou son effacement.29

La génération des pixels est orchestrée par une puce ROM génératrice de caractères Signetics 2513, qui traduit les codes ASCII 7 bits circulant dans les registres à décalage en motifs matriciels à envoyer vers l'écran.26 Le caractère clignotant faisant office de curseur est intégralement géré par le matériel vidéo, sans aucune intervention logicielle du CPU.26

Cette topologie de registres à décalage engendre une contrainte fondamentale : le bus vidéo n'est pas adressable de manière aléatoire. Lorsqu'un programme souhaite afficher un nouveau caractère à l'écran, le matériel vidéo doit attendre précisément que le bit de marquage du "curseur" effectue son cycle complet dans les registres à décalage et se présente à l'entrée d'insertion.29 Sachant que le signal composite de l'Apple I est calqué sur le standard NTSC cadencé à 60 trames par seconde (60 Hz), le registre effectue un tour complet en un soixantième de seconde.28

La conséquence technique est inéluctable : le terminal de l'Apple I est physiquement incapable d'ingérer et d'afficher plus d'un seul caractère par trame vidéo.29 La vitesse de sortie d'affichage est donc strictement plafonnée à 60 caractères par seconde.28 Bien que dérisoire par rapport à la vitesse d'exécution du microprocesseur 6502 (cadencé à environ 1 MHz), ce débit équivaut à une communication série d'environ 480 bauds, ce qui offrait à l'utilisateur un confort de lecture optimal, les lettres apparaissant à une vitesse naturelle pour l'œil humain.3 L'absence totale de gestion logicielle du curseur (la touche Backspace n'avait aucun effet visuel natif) et le traitement du retour chariot (qui attendait simplement la fin de la ligne courante pour décaler l'insertion) étaient des effets secondaires directs de cette implémentation rudimentaire mais brillante.28

### **Protocoles d'Acquittement : Signaux DA et RDA**

L'asymétrie de vitesse entre le CPU 6502 (capable d'émettre des millions d'instructions par seconde) et le terminal vidéo (limité à 60 Hz) imposait un protocole de blocage strict pour éviter l'écrasement des données. Le PIA 6820 orchestre cette poignée de main matérielle (handshaking) avec la section vidéo via des signaux de contrôle dédiés.30

Lorsqu'un code ASCII est prêt à être affiché, le CPU l'écrit dans le registre du Port B du PIA (aux adresses PB0 à PB6).8 Le registre de contrôle du PIA (CRB) est configuré pour réagir automatiquement à cette écriture en modifiant l'état de la ligne de contrôle de sortie CB2 (broche 19).18

1. L'écriture provoque la chute du signal CB2 à l'état logique 0\.30  
2. Ce signal traverse une porte logique NAND (située en C15, puce 74LS04 ou équivalente), qui inverse le signal en niveau logique 1\.30  
3. Ce front montant constitue le signal DA (Data Available), qui est réinjecté dans la section terminale vidéo pour signifier qu'un caractère valide est présent sur les lignes de données PB0-PB6.30

La logique du terminal vidéo détecte le signal DA et commence son cycle de scrutation du curseur.29 Une fois que le caractère a été inséré avec succès dans le registre à décalage, le matériel vidéo doit libérer le CPU. Il génère alors une impulsion descendante d'une largeur exacte d'une période d'horloge de caractère sur la ligne nommée /RDA (Read Data Available).30 Ce signal est directement relié au bit de poids fort du Port B, le PB7 (broche 17 du PIA).8

Le registre de direction de données (DDRB) de l'Apple I est asymétriquement configuré : les sept premiers bits (0 à 6\) sont des sorties pour le code ASCII, mais le bit 7 est configuré en entrée.19 La broche PB7 agit ainsi comme un drapeau d'état (flag) en temps réel.

* Tant que PB7 est lu à l'état logique 1, le terminal est occupé à chercher le curseur (Not Ready / Busy).8  
* Lorsque PB7 passe à l'état logique 0 suite à l'impulsion /RDA, le terminal notifie au PIA qu'il est prêt à accepter un nouveau caractère (Ready).8

## **Le Moniteur Woz et l'Interaction Logicielle avec le PIA**

L'intégration matérielle est indissociable de sa gestion logicielle. Le système d'exploitation primitif de l'Apple I résidait dans une paire de puces PROM de 256 octets chacune, contenant un programme compact et optimisé baptisé le "Woz Monitor".3 L'ensemble du système de gestion des entrées/sorties ne reposait pas sur le mécanisme complexe des interruptions matérielles (IRQA/IRQB du PIA) 19, mais exploitait la technique du "polling" (scrutation asynchrone continue).

### **La Sous-Routine ECHO à l'Adresse $FFEF**

Le cœur du traitement de l'affichage vidéo dans le Woz Monitor est la sous-routine logicielle ECHO.28 Chaque fois qu'une touche du clavier est frappée, ou qu'un programme applicatif (tel que l'Apple BASIC) souhaite imprimer un texte à l'écran, le code ASCII du caractère est chargé dans le registre Accumulateur (A) du microprocesseur 6502, et le programme invoque un saut vers la sous-routine via l'instruction assembleur JSR ECHO (Jump to SubRoutine) à l'adresse mémoire absolue $FFEF.31

L'analyse du désassemblage du code hexadécimal et mnémonique de cette portion spécifique du moniteur révèle l'élégance de la gestion de l'attente active (busy-wait) par Steve Wozniak 28 :

| Adresse Mém. | Code Machine (Hex) | Mnémonique Assembleur 6502 | Explication Logique |
| :---- | :---- | :---- | :---- |
| **FFEF** | 2C 12 D0 | ECHO BIT DSP | Teste logiquement l'Accumulateur avec l'adresse $D012 (Registre de données du Port B du PIA).28 Modifie les drapeaux d'état du CPU sans altérer le contenu de l'Accumulateur. Copie le bit 7 du port dans le drapeau N (Négatif). |
| **FFF2** | 30 FB | BMI ECHO | "Branch on Minus" (Branchement si Négatif). Si le drapeau N est à 1 (ce qui signifie que PB7 est à l'état haut 1, signalant que la vidéo est "Occupée/Busy"), le programme recule de quelques octets (FB) et boucle indéfiniment sur l'adresse FFEF.28 |
| **FFF4** | 8D 12 D0 | STA DSP | "Store Accumulator". Le port B est désormais prêt. Le CPU écrit l'octet ASCII contenu dans l'Accumulateur vers l'adresse $D012 du PIA. L'écriture des bits 0-6 transmet le caractère. Le matériel du PIA abaisse automatiquement la ligne CB2, générant le signal stroboscopique DA.28 |
| **FFF7** | 60 | RTS | "Return from Subroutine". Le caractère a été transmis avec succès au PIA. Le contrôle est rendu au programme appelant.28 |

Cette implémentation est primordiale pour la compréhension du comportement système lors de l'adjonction d'une imprimante. L'instruction BMI bloque totalement l'exécution du processeur 6502 dans une boucle très serrée de quelques microsecondes.28 Ce blocage persiste jusqu'à ce que le signal /RDA de la section vidéo ramène la broche PB7 à zéro.28 La routine ECHO peut donc retenir l'exécution d'un programme utilisateur pendant une durée pouvant atteindre 16,7 millisecondes (1/60ème de seconde, le temps d'une trame) pour chaque caractère imprimé.31 De nombreuses autres fonctions fondamentales du Woz Monitor s'appuient sur cette routine comme goulot d'étranglement central, notamment $FFDC (PRBYTE, pour imprimer un octet en format hexadécimal à 2 chiffres) et $FF1F (GETLINE, le point d'entrée pour le retour chariot et l'invite de commande).31

## **La Solution d'Interfaçage de Steve Jobs (Article d'Octobre 1976\)**

Dans ce contexte d'une architecture vidéo matérielle très spécifique, fonctionnant à faible débit asynchrone et gérée par une boucle de blocage logiciel intransigeante, l'ajout d'une imprimante représentait un véritable défi. Modifier la PROM du Woz Monitor pour y inclure des pilotes d'imprimante (drivers) ou des routines d'interruption nécessitait l'effacement aux rayons ultraviolets et la reprogrammation coûteuse des mémoires mortes.3 Par ailleurs, l'ajout d'un second contrôleur PIA 6820 dédié nécessitait des compétences en ingénierie de décodage d'adresse mémoire qui échappaient à la majorité des amateurs.3

L'article rédigé par Steve Jobs dans l'édition d'octobre 1976 du magazine *Interface Age* apportait une solution matérielle brillante par sa simplicité. Intitulé "Interfacing the Apple Computer" 36, le tutoriel détaillait comment détourner l'architecture de sortie vidéo existante pour y greffer l'imprimante SWTPC PR-40.5

### **La Philosophie de l'Interception de Données**

Puisque la PR-40 et le terminal vidéo partagent une géométrie identique de 40 caractères par ligne 5, la stratégie proposée par Jobs repose sur la duplication des signaux physiques à la sortie du PIA. Au lieu d'avoir un port dédié, l'imprimante est placée en écoute (sniffing) sur le bus de données du terminal vidéo.3

L'imprimante PR-40 étant capable d'absorber des caractères à la cadence de 1 MHz dans son tampon interne FIFO 7, elle n'a aucune difficulté à suivre le rythme lent de 60 caractères par seconde imposé par la boucle ECHO de la vidéo de l'Apple I.3 Le seul véritable conflit temporel survient lorsque le tampon de la PR-40 se remplit ou reçoit un code de retour chariot ($0D) : l'imprimante déclenche alors ses moteurs et ses solénoïdes pour une frappe physique d'environ 0,8 seconde, durant laquelle elle n'est plus en mesure d'accepter le moindre bit de donnée.7 Le défi consistait donc à modifier le retour d'état (le drapeau matériel PB7) pour que le processeur 6502 soit forcé de bloquer son exécution (via l'instruction BMI de la routine ECHO) *à la fois* si le terminal vidéo est en phase de synchronisation, *et* si l'imprimante est en phase d'impression mécanique.8

### **Mécanique du Commutateur et Inversion Logique**

La mise en œuvre pratique nécessitait peu de composants, exploitant la zone de prototypage (breadboard area) prévue par Wozniak sur la moitié droite de la carte mère de l'Apple I.3 Le dispositif s'articulait autour d'un connecteur d'interface de type DIP raccordé à l'imprimante par un câble en nappe arc-en-ciel, et d'un commutateur inverseur bipolaire à deux directions, couramment appelé DPDT (Double Pole Double Throw), fabriqué par des marques comme Grayhill.3 Le commutateur DPDT permet de diriger simultanément deux lignes de signaux distinctes vers des chemins différents.38

L'article d'Interface Age instruisait l'utilisateur de souder un harnais de fils directement sur les pattes du PIA A4.8 Les fils recommandés étaient du fil isolant en Kynar de jauge 28 AWG, fréquemment utilisé pour la technique de wrapping.3

1. **Duplication du Bus de Données :** Les sept lignes de données ASCII (PB0 à PB6, correspondant aux broches 10 à 16 du PIA 6820\) devaient être physiquement raccordées aux sept broches d'entrée correspondantes du connecteur J4 de la PR-40 (J4-10, J4-11, J4-12, J4-8, J4-9, J4-5, J4-6 respectivement).8 Cette connexion garantissait que chaque code ASCII présenté au terminal vidéo par la routine STA DSP était simultanément "écouté" par la PR-40.28 La masse (broche 1 du PIA) était connectée à la broche J4-1 ou J4-4 de la PR-40 pour assurer un référentiel TTL commun.8  
2. **Gestion du Strobe (Data Ready) :** L'imprimante SWTPC PR-40 requiert une impulsion de validation négative pour verrouiller la donnée dans son tampon.8 Le PIA de l'Apple I génère naturellement ce comportement lors de l'écriture sur le Port B en activant la ligne de sortie CB2.8 Jobs indique de capturer ce signal (qui est transformé en signal DA par l'architecture vidéo) sur la broche 19 du PIA, à l'aide d'un fil rouge.8 Ce signal est routé vers un pôle du commutateur DPDT.8 Lorsque le commutateur est en position "Imprimante", le signal strobe est acheminé vers la broche J4-3 (Data Ready) de la PR-40.8  
3. **Inversion et Routage du Signal d'Acquittement (Data Accepted) :** C'est ici que réside la complexité logique de la modification. La PR-40 accuse réception des données en abaissant sa ligne Data Accepted (J4-2) à l'état bas logique (0).6 À l'inverse, l'architecture du Woz Monitor et de la broche PB7 s'attend à lire un état haut (1) pour identifier que le périphérique est "occupé" (Busy) et qu'il faut patienter dans la boucle BMI.8 Présenter directement le signal de l'imprimante au CPU aboutirait à une désynchronisation catastrophique et à un écrasement immédiat des données du tampon.  
   Pour pallier cette incompatibilité de polarité logique, Jobs exploite habilement les portes logiques libres intégrées au design de la carte mère de l'Apple I.8 L'article prescrit d'utiliser la porte logique NAND inverseuse désignée IC 15 (qui peut être un circuit intégré 74LS04 ou 74LS10).8 Le signal Data Accepted en provenance de l'imprimante est branché sur les entrées de la porte NAND (broches 1 et 2 de IC 15).8 La sortie inversée sur la broche 3 de IC 15 (câblée avec un fil marron) garantit que l'état bas d'occupation mécanique de l'imprimante est transformé en un état haut propre.8 Cette inversion apporte également un délai de propagation infime, suffisant pour stabiliser électriquement le front TTL avant qu'il ne soit échantillonné par le processeur.8  
   Ce signal corrigé est conduit vers le second pôle du commutateur DPDT. Le commutateur sélectionne ainsi la source du signal d'occupation (soit la section vidéo via son signal natif, soit la section vidéo combinée avec le signal corrigé de l'imprimante) et l'injecte dans la broche 17 du PIA (PB7).8

Afin de permettre au commutateur de piloter la broche 17, l'utilisateur devait isoler celle-ci du reste du circuit vidéo d'origine. L'article proposait deux méthodes invasives : soit utiliser un scalpel pour sectionner physiquement la délicate piste en cuivre sur la carte mère adjacente à la pastille du PIA, soit extraire la puce PIA 6820 de son support DIP à 40 broches, tordre délicatement la patte 17 vers l'extérieur pour qu'elle ne s'insère plus dans le support, puis la remettre en place et souder les fils de dérivation (fil orange et fil jaune) directement sur la patte aérienne.8

### **Le Prix de la Simplicité : Ralentissement et Effets Visuels**

Ce couplage matériel élégant impliquait un compromis opérationnel significatif. Lorsque l'interrupteur était basculé en position d'impression, chaque caractère envoyé vers l'écran était inexorablement copié vers l'imprimante.12 Dès qu'une ligne de 40 caractères était complétée ou qu'un code de retour chariot était intercepté, la tête d'impression mécanique matricielle entamait sa translation bruyante d'une durée de près d'une seconde.7

Durant ce cycle mécanique, la ligne Data Accepted maintenait le bit PB7 du PIA de l'Apple I à l'état haut (1) de manière continue.8 Conséquence immédiate : le processeur 6502 demeurait bloqué dans l'exécution de la boucle d'attente active de la sous-routine ECHO.28 Cet arrêt brutal de l'exécution du logiciel système figeait littéralement le déroulement de l'affichage vidéo, engendrant des saccades majeures lors du listage de longs programmes en BASIC, le texte défilant à l'écran par saccades brutales rythmées par le va-et-vient de la tête d'impression de la PR-40.12 Bien que la qualité d'impression de l'encre noire produite par la matrice 5x7 fût jugée "légère mais pas mauvaise" pour l'époque, cette latence globale constituait la rançon du très faible investissement financier.12

## **Évolutions Avancées et Alternatives d'Ingénierie**

La publication de ce "mod" (modification) matériel n'a constitué que le point de départ de l'innovation communautaire. Les ingénieurs amateurs, réunis au sein de structures telles que l'Apple I Owners Club ou le Homebrew Computer Club 3, ont rapidement identifié et résolu les limitations inhérentes à l'architecture partagée entre la vidéo et l'impression.

### **Le Schéma à Trois Positions pour l'Impression Haute Vitesse**

Le premier perfectionnement documenté de l'interface de Steve Jobs consistait à remplacer le commutateur DPDT à deux états par une implémentation plus complexe offrant un troisième état d'opération exclusif.44

Les schémas électriques modifiés introduisaient un état appelé "Print Only" (Impression Seule).44 L'objectif était de découpler complètement la logique de synchronisation de l'imprimante des limitations sclérosantes du matériel vidéo de l'Apple I.44 Comme démontré précédemment, le signal vidéo ne pouvait absorber qu'un caractère tous les 1/60ème de seconde en raison de l'architecture des registres à décalage Signetics.28 Cependant, la PR-40 possédait un tampon mémoire FIFO capable d'accepter des données à une vitesse d'un million de caractères par seconde.7

En isolant la ligne d'état PB7 (broche 17 du PIA) du signal /RDA de la vidéo, et en l'asservissant exclusivement au signal inversé Data Accepted de l'imprimante (provenant de la porte NAND IC 15\) 8, le processeur 6502 n'avait plus à attendre le rafraîchissement du tube cathodique.44 L'ordinateur pouvait littéralement inonder la PR-40 de données. Une ligne entière de 40 caractères était déversée dans le tampon de l'imprimante en une fraction de milliseconde, limitée uniquement par le nombre de cycles d'horloge nécessaires à l'exécution de l'instruction STA de la boucle ECHO.35 Le résultat était une décharge de données ultra-rapide suivie de l'exécution de l'impression mécanique, augmentant drastiquement le débit moyen par rapport au mode mixte.44 En mode "Print Only", le moniteur vidéo de l'Apple I n'affichait temporairement plus rien de ce qui était imprimé, une concession mineure au regard du gain de performance exceptionnel.44

### **Le Hack VMA et l'Approche Sérialisée**

Pour les utilisateurs professionnels cherchant à s'affranchir de la dépendance à la matrice 40 colonnes de la SWTPC PR-40 et désirant utiliser des imprimantes sophistiquées (telles qu'un Teletype ASR 33 lourd, ou des modèles IBM Selectric modifiés exigeant le standard RS-232C) 3, une solution radicalement différente a été conçue par l'ingénieur Wendell Sander.3

Sander a mis au point une véritable carte d'extension série (la "Apple 1 Serial Board") qui illustre une forme spectaculaire d'usurpation matérielle.3 L'Apple I étant dépourvu de zone mémoire réservée pour de multiples périphériques d'E/S, il était impossible d'ajouter un second contrôleur PIA de manière native sans engendrer de collisions d'adressage.25 La carte série de Sander résout ce problème en exploitant un signal matériel hybride baptisé "VMA" (Valid Memory Address).

Le signal VMA n'est pas une caractéristique native du microprocesseur MOS 6502 utilisé par Apple. C'est une spécification propre à la famille des processeurs Motorola 6800\.25 Néanmoins, pour garantir la compatibilité de l'Apple I avec les puces périphériques Motorola (comme le PIA 6820 central au design), Wozniak avait inclus une piste physique sur la carte mère permettant d'émuler grossièrement ce comportement, en la court-circuitant au \+5V près du CPU.25

La modification imposée par Sander consistait à supprimer ce court-circuit (localisé près du repère A8) et à insérer un circuit de rappel composé d'une résistance de 2200 ohms et d'un condensateur de 100 picofarads relié au rail \+5V.25 Ce montage de surface méticuleux permettait à la carte d'extension série, équipée d'un circuit de communication ACIA 6551, de manipuler directement la ligne VMA.25 Lorsqu'une donnée devait être imprimée sur le port série, la carte forçait la ligne VMA à l'état bas.25 Cette action désactivait physiquement les décodeurs d'adresse de la carte mère, neutralisant aveuglément le 6820 PIA d'origine, la RAM et même la PROM système, sur une base cycle par cycle d'horloge.25

L'ACIA 6551 usurpait alors frauduleusement les espaces d'adressage du PIA vidéo ($D011 et $D012), répondant de manière asynchrone aux interrogations de la routine ECHO et gérant de manière transparente la conversion parallèle-série nécessaire pour piloter une imprimante RS-232C standard.25 Le logiciel système de l'Apple I était complètement dupe, croyant continuer à communiquer avec le terminal vidéo, alors qu'il alimentait en réalité un périphérique d'impression industriel, sans nécessiter la moindre altération d'un seul octet du code source original.3

## **Conséquences et Héritage d'Ingénierie**

L'étude détaillée de l'imprimante SWTPC PR-40 et de son interface matérielle avec l'Apple I transcende la simple anecdote technique. Cet interfaçage documenté en 1976 par Steve Jobs illustre une philosophie d'ingénierie systémique dominée par l'optimisation extrême des coûts, la débrouillardise matérielle et la contrainte de la capacité asynchrone.

En s'appuyant sur l'architecture robuste mais primitive du Motorola MC6820 PIA et sur les particularités de conception des registres à décalage de la section vidéo, l'Apple I a pu surmonter ses lacunes natives sans surcoût d'infrastructure.3 L'utilisation d'une porte NAND inverseuse (IC 15\) pour aligner les conventions de polarité TTL divergentes entre les signaux d'acquittement d'imprimantes industrielles (Data Accepted actif à l'état bas) et la logique d'état occupée de l'ordinateur personnel (actif à l'état haut) démontre une profonde maîtrise des contraintes d'interfaçage de bas niveau.6

De plus, l'adoption précoce de l'imprimante PR-40, grâce à son architecture de tampon FIFO capable d'absorber les disparités de bande passante 7, a mis en évidence le besoin impératif de dissocier la logique de rendu vidéo de la logique de sortie périphérique. Les saccades visuelles induites par l'attente active (busy loop) du processeur 6502 dans la routine ECHO lors des phases d'impression physique ont dicté d'importantes leçons conceptuelles.12

Cette période d'expérimentation matérielle intense, allant du simple commutateur DPDT de Jobs 3 à l'usurpation du bus VMA par Sander 25, a profondément influencé l'évolution de la micro-informatique. Les concepteurs, conscients des goulets d'étranglement générés par une gestion partagée des ressources d'entrées-sorties, intégreront dans les systèmes ultérieurs (dont l'Apple II, sorti dès 1977\) des ports d'extension standardisés dotés de puces d'adressage indépendantes, et adopteront le mécanisme sophistiqué des interruptions matérielles (IRQA/IRQB) pour éviter la paralysie logicielle.21 Le mod PR-40 demeure ainsi un fossile technologique d'une importance capitale, cristallisant l'ingéniosité d'une époque où l'interopérabilité des systèmes exigeait autant de virtuosité en soudure qu'en conception logique matérielle.

#### **Sources des citations**

1. The first Apple computer, Steven Jobs & Stephen Wozniak. 1976 | Christie's, consulté le avril 21, 2026, [https://onlineonly.christies.com/s/shoulders-giants-making-modern-world/first-apple-computer-50/70080](https://onlineonly.christies.com/s/shoulders-giants-making-modern-world/first-apple-computer-50/70080)  
2. Steve Jobs: Make Something Wonderful, consulté le avril 21, 2026, [https://book.stevejobsarchive.com/](https://book.stevejobsarchive.com/)  
3. Printing from an Apple-1? | Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/content/printing-apple-1](https://www.applefritter.com/content/printing-apple-1)  
4. Apple-1 \#29 'Sicilian', consulté le avril 21, 2026, [https://www.apple1registry.com/en/29.html](https://www.apple1registry.com/en/29.html)  
5. The PR-40 Alphanumeric Printer. \- EarlyComputers, consulté le avril 21, 2026, [https://www.earlycomputers.com/cgi-bin/item-report-main.cgi?99999651](https://www.earlycomputers.com/cgi-bin/item-report-main.cgi?99999651)  
6. Computer Products Catalogue 1978 (?), consulté le avril 21, 2026, [https://vtda.org/docs/computing/SWTPC/SWTPC\_ComputerProductsCatalogue.pdf](https://vtda.org/docs/computing/SWTPC/SWTPC_ComputerProductsCatalogue.pdf)  
7. SWTPC PR-40 Printer, consulté le avril 21, 2026, [https://www.swtpc.com/mholley/pr\_40/pr\_40\_index.html](https://www.swtpc.com/mholley/pr_40/pr_40_index.html)  
8. Untitled \- Atari Compendium, consulté le avril 21, 2026, [https://www.ataricompendium.com/archives/magazines/interface\_age/interface\_age\_oct76.pdf](https://www.ataricompendium.com/archives/magazines/interface_age/interface_age_oct76.pdf)  
9. Software and Documents \- The Apple-1 Registry, consulté le avril 21, 2026, [https://www.apple1registry.com/en/soft.html](https://www.apple1registry.com/en/soft.html)  
10. Lot \#6107 Steve Jobs: Apple-1 Ad and Article in Interface Age Magazine from October 1976 \- CGC .5 \- RR Auction, consulté le avril 21, 2026, [https://www.rrauction.com/auctions/lot-detail/350442607346107-steve-jobs-apple-1-ad-and-article-in-interface-age-magazine-from-october-1976-cgc-5/](https://www.rrauction.com/auctions/lot-detail/350442607346107-steve-jobs-apple-1-ad-and-article-in-interface-age-magazine-from-october-1976-cgc-5/)  
11. SWTPC PR-40 Alphanumeric Printer Assembly Instructions \- DeRamp, consulté le avril 21, 2026, [https://deramp.com/downloads/swtpc/hardware/PR\_40%20Printer/PR40\_AsemblyInstructions.pdf](https://deramp.com/downloads/swtpc/hardware/PR_40%20Printer/PR40_AsemblyInstructions.pdf)  
12. SWTPC PR-40 Printer | Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/node/2828](https://www.applefritter.com/node/2828)  
13. Byte Mar 1977 \- Vintage Apple, consulté le avril 21, 2026, [https://vintageapple.org/byte/pdf/197703\_Byte\_Magazine\_Vol\_02-03\_Buying\_Computers.pdf](https://vintageapple.org/byte/pdf/197703_Byte_Magazine_Vol_02-03_Buying_Computers.pdf)  
14. Swtpc pr40 \- Vintage Computer Federation Forums, consulté le avril 21, 2026, [https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/](https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/)  
15. Swtpc pr40 | Page 2 \- Vintage Computer Federation Forums, consulté le avril 21, 2026, [https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/page-2](https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/page-2)  
16. Swtpc pr40 | Page 3 \- Vintage Computer Federation Forums, consulté le avril 21, 2026, [https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/page-3](https://forum.vcfed.org/index.php?threads/swtpc-pr40.1223664/page-3)  
17. Unprecedented early technical notes and diagrams by Steve Jobs \- Issuu, consulté le avril 21, 2026, [https://issuu.com/rrauction/docs/rr\_steve\_jobs\_apple\_computer\_revolution/s/19117030](https://issuu.com/rrauction/docs/rr_steve_jobs_apple_computer_revolution/s/19117030)  
18. Peripheral Interface Adapter \- Wikipedia, consulté le avril 21, 2026, [https://en.wikipedia.org/wiki/Peripheral\_Interface\_Adapter](https://en.wikipedia.org/wiki/Peripheral_Interface_Adapter)  
19. MC6820 PIA operation on the Apple 1 \- Electronics Stack Exchange, consulté le avril 21, 2026, [https://electronics.stackexchange.com/questions/678427/mc6820-pia-operation-on-the-apple-1](https://electronics.stackexchange.com/questions/678427/mc6820-pia-operation-on-the-apple-1)  
20. MC6820 \- Peripheral Interface Adapter (PIA) \- DeRamp.com, consulté le avril 21, 2026, [https://deramp.com/downloads/mfe\_archive/050-Component%20Specifications/Motorola/Motorola%20-%20MC6820%20-%20Peripheral%20Interface%20Adapter%20(PIA).pdf](https://deramp.com/downloads/mfe_archive/050-Component%20Specifications/Motorola/Motorola%20-%20MC6820%20-%20Peripheral%20Interface%20Adapter%20\(PIA\).pdf)  
21. MOS Technology 6502 \- Wikipedia, consulté le avril 21, 2026, [https://en.wikipedia.org/wiki/MOS\_Technology\_6502](https://en.wikipedia.org/wiki/MOS_Technology_6502)  
22. 6820 PIA Chip \- Action Pinball, consulté le avril 21, 2026, [https://www.actionpinball.com/parts.php?item=6820](https://www.actionpinball.com/parts.php?item=6820)  
23. Hardware Interfacing With The Apple II Plus 1983.pdf, consulté le avril 21, 2026, [https://vintageapple.org/apple\_ii/pdf/Hardware\_Interfacing\_With\_The\_Apple\_II\_Plus\_1983.pdf](https://vintageapple.org/apple_ii/pdf/Hardware_Interfacing_With_The_Apple_II_Plus_1983.pdf)  
24. Questions about PIA | Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/content/questions-about-pia](https://www.applefritter.com/content/questions-about-pia)  
25. Apple 1 Serial Board Documentation20150529, consulté le avril 21, 2026, [https://apple1notes.com/wp-content/uploads/2020/07/Apple-1-Serial-Board-Documentation.pdf](https://apple1notes.com/wp-content/uploads/2020/07/Apple-1-Serial-Board-Documentation.pdf)  
26. A deep dive into the Apple 1 video terminal circuit (details in comment) : r/beneater \- Reddit, consulté le avril 21, 2026, [https://www.reddit.com/r/beneater/comments/11pqcgh/a\_deep\_dive\_into\_the\_apple\_1\_video\_terminal/](https://www.reddit.com/r/beneater/comments/11pqcgh/a_deep_dive_into_the_apple_1_video_terminal/)  
27. The legendary Apple-1 explained, consulté le avril 21, 2026, [https://www.apple1registry.com/interactive/apple-1.html](https://www.apple1registry.com/interactive/apple-1.html)  
28. Could suitably-written Apple-I software force bytes into the display shift register, and do modern recreations emulate that? \- Retrocomputing Stack Exchange, consulté le avril 21, 2026, [https://retrocomputing.stackexchange.com/questions/8772/could-suitably-written-apple-i-software-force-bytes-into-the-display-shift-regis](https://retrocomputing.stackexchange.com/questions/8772/could-suitably-written-apple-i-software-force-bytes-into-the-display-shift-regis)  
29. How did the Apple 1 video circuit work? \- Retrocomputing Stack Exchange, consulté le avril 21, 2026, [https://retrocomputing.stackexchange.com/questions/13228/how-did-the-apple-1-video-circuit-work](https://retrocomputing.stackexchange.com/questions/13228/how-did-the-apple-1-video-circuit-work)  
30. looking for video display parameters | Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/content/looking-video-display-parameters](https://www.applefritter.com/content/looking-video-display-parameters)  
31. manual \- Retro Computing, consulté le avril 21, 2026, [http://retro.hansotten.nl/uploads/apple1/A\_ONE%20manual%2011.pdf](http://retro.hansotten.nl/uploads/apple1/A_ONE%20manual%2011.pdf)  
32. Manuel Replica 1 Computer (ENG) \- Calaméo, consulté le avril 21, 2026, [https://www.calameo.com/books/005163808ee2c1381beb3](https://www.calameo.com/books/005163808ee2c1381beb3)  
33. apple 1 ROM disassembly \- GitHub Gist, consulté le avril 21, 2026, [https://gist.github.com/robey/1bb6a99cd19e95c81979b1828ad70612](https://gist.github.com/robey/1bb6a99cd19e95c81979b1828ad70612)  
34. Apple I Replica Creation \-- Chapter 6: Programming in Assembly \- Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/replica/chapter6](https://www.applefritter.com/replica/chapter6)  
35. Error in Apple I manual's Wozmon listing or Hardware description?, consulté le avril 21, 2026, [https://retrocomputing.stackexchange.com/questions/27265/error-in-apple-i-manuals-wozmon-listing-or-hardware-description](https://retrocomputing.stackexchange.com/questions/27265/error-in-apple-i-manuals-wozmon-listing-or-hardware-description)  
36. Introductory Apple 1 Computer advertisement published in the October 1976 issue of Interface Age magazine \- History of Information, consulté le avril 21, 2026, [https://www.historyofinformation.com/image.php?id=5368](https://www.historyofinformation.com/image.php?id=5368)  
37. Apple \- 1 operation manual, 1976, consulté le avril 21, 2026, [https://s3data.computerhistory.org/brochures/apple.applei.1976.102646518.pdf](https://s3data.computerhistory.org/brochures/apple.applei.1976.102646518.pdf)  
38. DPDT Switch Working, Structure, Wiring Guide and Safety Tips \- IC Components, consulté le avril 21, 2026, [https://www.ic-components.com/blog/DPDT-Switch-Working,Structure,Wiring-Guide-and-Safety-Tips.jsp](https://www.ic-components.com/blog/DPDT-Switch-Working,Structure,Wiring-Guide-and-Safety-Tips.jsp)  
39. Reverse Polarity Switching DPDT Switch \- VogMan, consulté le avril 21, 2026, [https://www.vegoilguy.co.uk/reverse\_polarity\_switching.php](https://www.vegoilguy.co.uk/reverse_polarity_switching.php)  
40. Tandy's Little Wonder The Color Computer 1979-1991, consulté le avril 21, 2026, [https://colorcomputerarchive.com/coco/Documents/Books/Tandy's%20Little%20Wonder%20Second%20Edition%20(Farna%20Systems).pdf](https://colorcomputerarchive.com/coco/Documents/Books/Tandy's%20Little%20Wonder%20Second%20Edition%20\(Farna%20Systems\).pdf)  
41. ETI AUS 1981-03.pdf, consulté le avril 21, 2026, [https://www.rsp-italy.it/Electronics/Magazines/Electronics%20Today%20International%20-%20Aus/\_contents/ETI%20AUS%201981-03.pdf](https://www.rsp-italy.it/Electronics/Magazines/Electronics%20Today%20International%20-%20Aus/_contents/ETI%20AUS%201981-03.pdf)  
42. Apple-1 schematic diagram \- Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/content/apple-1-schematic-diagram](https://www.applefritter.com/content/apple-1-schematic-diagram)  
43. Apple I Replica Creation \-- Chapter 1: Apple I History \- Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/replica/chapter1](https://www.applefritter.com/replica/chapter1)  
44. SWTP PR-40 Printer Interface Schematics \- Applefritter, consulté le avril 21, 2026, [https://www.applefritter.com/node/2832](https://www.applefritter.com/node/2832)  
45. NO 4 $ 1 .5 0 \- 6502.org, consulté le avril 21, 2026, [https://6502.org/documents/periodicals/micro/micro\_04\_apr\_1978.pdf](https://6502.org/documents/periodicals/micro/micro_04_apr_1978.pdf)