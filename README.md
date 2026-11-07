# Sharing Energy — ECU-C APsystems

## ACC à deux maisons : APsystems, ESP32 et Shelly

Ce dépôt regroupe le guide public et les versions des outils utilisées pour obtenir la lecture d’un Shelly par un ECU APsystems, puis vérifier son effet sur le bridage des micro-onduleurs.

L’objectif ACC est de transmettre à l’ECU le **bilan électrique cumulé de deux maisons** :

**Puissance réseau maison 1 + puissance réseau maison 2**, avec le soutirage positif et l’export négatif.

La liaison avec **un Shelly** et la réaction du bridage ont été vérifiées sur l’installation de référence. **L’intégration du deuxième Shelly reste à réaliser ; elle ne présente a priori pas de difficulté technique particulière.** Le fonctionnement sur la somme des deux maisons devra ensuite être validé. Home Assistant, MQTT et les autres solutions domotiques sont des possibilités d’adaptation, pas des interfaces déjà intégrées au sketch.

**[Lire le guide complet (PDF)](Guide_public_APsystems_ESP32_Domotique.pdf)** · **[Télécharger tous les fichiers (ZIP)](https://github.com/guillaumeferraro-gif/Sharing-Energy-ECU-C-APSystems/archive/refs/heads/main.zip)**

## Contenu et ordre d’utilisation

| Élément | Usage | Statut |
| --- | --- | --- |
| `Guide_public_APsystems_ESP32_Domotique.pdf` | Objectif ACC, installation, preuves, extraction et analyse | Guide public, 13 pages |
| `01_ESP32/` | Programme `ESP32_v088` avec ses quatre fichiers `.h` | Version employée lors des essais réussis, paramètres anonymisés |
| `02_Test_signature/` | Comparaison Shelly / ESP32 / ECU avec un décalage temporaire de +1 000 W | Zéro export **désactivé** |
| `03_Test_bridage/` | Vérification de la réaction de production au même décalage | Zéro export **activé à 0 W**, soleil et réserve de production suffisants |
| `04_Recuperation_ECU/` | Collecte v3.3 du code applicatif de l’ECU | Outil avancé, facultatif pour utiliser le montage déjà validé |
| `05_Analyse_REA/` | Lanceur et mission d’analyse hors ligne de sa propre extraction | À adapter à l’empreinte, au manifeste et à la version de l’archive analysée |
| `Personnaliser.py` | Création d’une copie locale avec les paramètres du destinataire | Outil de préparation ajouté pour ce paquet public ; aucun accès réseau |

Le profil de référence est **ECU-C-Z C1.2.27A / Shelly Pro EM 50 / ESP32 v0.8.8**, avec un modèle de mesure monophasé. Une autre génération d’ECU, de Shelly ou un modèle triphasé demande une adaptation et de nouveaux essais.

## Commencer

1. Télécharger puis extraire tout le ZIP du dépôt, ou cloner le dépôt. Lire la première page du PDF, puis les pages 2 à 6.
2. Conserver ce dossier comme **modèle public**. Copier `configuration.exemple.json` en `configuration.locale.json` et remplir cette copie avec un éditeur de texte.
3. Installer Python 3.8 ou plus. Sous Windows, lancer `Personnaliser.cmd`. Sous macOS ou Linux, ouvrir un terminal dans le dossier et lancer :

   ```sh
   python3 -B Personnaliser.py
   ```

4. Le préparateur crée `installation_locale/`. Il n’appelle aucun appareil et ne téléverse rien. Les sources publiques restent inchangées. Utiliser ensuite **les fichiers de cette copie locale**.
5. Ouvrir `installation_locale/01_ESP32/ESP32_v088/ESP32_v088.ino` dans Arduino IDE et suivre les pages 3 à 5 du guide. Le programme doit être compilé puis téléversé sur la carte adaptée.
6. Vérifier les réglages de retour aux tores, sélectionner l’ESP32, puis exécuter les deux tests **successivement**, chacun avec son état de départ. Ne pas lancer les collectes simultanément.

### Si l’adresse ou l’identifiant de l’ESP32 n’est pas encore connu

Pour le premier repérage seulement, laisser `ip_esp32` et `id_compteur_esp32` vides. Le préparateur accepte ce cas ; la sélection du compteur et les lecteurs ne sont pas prêts.

Compiler cette première copie du sketch, relever son adresse dans le moniteur série, puis relever **`meter_id` dans `http://IP_ESP/status`**. Copier cet identifiant exact, sans le reconstruire à partir de l’adresse MAC du routeur. Compléter les deux champs et créer une seconde copie :

```sh
python3 -B Personnaliser.py --sortie installation_locale_2
```

Sous Windows, la même option est acceptée par `Personnaliser.cmd`. Le préparateur refuse d’écraser un dossier déjà existant. Recompiler et téléverser le sketch de la copie complète, puis utiliser ses lecteurs.

## Paramètres à renseigner

| Champ | Origine |
| --- | --- |
| `wifi_ssid`, `wifi_mot_de_passe` | Réseau auquel l’ESP32 doit se connecter |
| `ip_ecu`, `uid_ecu` | Adresse locale et UID exact de 12 chiffres de l’ECU |
| `version_ecu` | Chaîne modèle/version ; profil fourni : `ECU-C-Z_C1.2.27A` |
| `ip_shelly`, `mac_shelly` | Adresse locale et champ `sys.mac` de `Shelly.GetStatus` |
| `canal_shelly` | 0 ou 1 : canal qui mesure réellement l’échange maison/réseau |
| `ip_esp32`, `id_compteur_esp32` | Adresse et champ `meter_id` de la carte utilisée |

Le préparateur remplace également les adresses codées dans les appels `IPAddress(...)` du sketch, les identités des lecteurs, l’UID contrôlé dans les deux `lecture.sh` et leurs empreintes `TEMPLATE_HASH`. Il prépare une nouvelle référence temporaire et l’empreinte cohérente du paquet de récupération. Il préserve les contrôles, la durée maximale de 90 secondes et la logique de régulation.

**Les paramètres de retour aux tores restent à vérifier manuellement.** `CT_GRID_BACKUP` et `switchCtRoleMatches()` contiennent le profil d’exemple du prototype. Relire la configuration de l’ECU cible et adapter les deux ensemble, comme indiqué page 5. La préparation d’un dossier ne vaut pas validation de ces réglages.

Les fichiers `lecture.sh` sont liés à leur propre lecteur. Si un script est modifié manuellement, son empreinte doit être recalculée et reportée dans le `TEMPLATE_HASH` correspondant. La préparation automatique prend en charge le changement d’UID ; ne pas mélanger les deux kits.

## Vérifier le résultat

Le test de signature prouve que l’ECU reprend les watts envoyés par l’ESP32 : pendant l’impulsion, il doit voir **Shelly + 1 000 W**, puis la valeur réelle après son retrait.

Le test de bridage vérifie la réaction physique : avec la consigne zéro export active et une réserve solaire suffisante, l’ECU vise environ 0 W sur sa mesure décalée tandis que le Shelly réel tend vers **−1 000 W**, soit environ 1 kW d’export. Les conditions de départ et l’analyse sont détaillées dans le guide et les notices des deux kits.

Un verdict automatique « indéterminé » n’équivaut pas à une preuve d’échec ou de réussite. Examiner les mesures, leur fraîcheur, les confirmations et la production. La version employée sur le terrain a montré la réception et l’effet sur la production totale ; elle ne prouve pas le délai de réaction individuel de chaque micro-onduleur.

## Environnement

- **Windows** : Arduino IDE pour la carte, Python pour les lecteurs ; PowerShell 5.1 pour la récupération v3.3 ; Ubuntu/WSL pour le lanceur d’analyse fourni.
- **macOS / Linux** : Arduino IDE et Python pour les outils portables. La collecte PowerShell et le lanceur Windows demandent leur environnement prévu ou un portage vérifié.
- **Android / iPhone** : consultation des pages locales et des applications. Ils ne remplacent pas le poste de compilation et d’analyse de cette procédure.
- **Home Assistant et autre domotique** : voir page 12. L’interface et l’agrégation doivent être développées puis validées.

## Portée de la version publique

Les outils fonctionnels sont ceux des versions retenues après les essais, avec retrait des paramètres du site. Le préparateur de configuration et le conditionnement public ont été vérifiés hors matériel ; le détail figure dans `VALIDATION.txt`. Aucun nouveau test sur l’ECU ou téléversement sur l’ESP32 n’a été réalisé lors de cette préparation.

Ce dépôt contient les **outils de collecte et d’analyse**, pas une copie du firmware extrait d’un particulier, ses relevés, ses photos ou ses identifiants. L’analyse REA avancée demande la propre extraction du destinataire. Les versions anciennes et les diagnostics intermédiaires ne sont pas nécessaires à la marche à suivre finale.

Pour toute modification publiée sur GitHub, conserver les modèles publics. `configuration.locale.json`, les dossiers `installation_locale*`, les relevés et les extractions sont des fichiers privés. Les exclusions Git facilitent cette séparation ; elles ne nettoient pas un fichier ajouté manuellement ou déjà enregistré dans un historique.

## Empreintes

`SHA256SUMS.txt` décrit les fichiers de cette version publique. Les empreintes des fichiers d’une copie locale personnalisée sont naturellement différentes. Dans cette copie, les deux lecteurs et le paquet de récupération disposent de leurs propres contrôles mis à jour par le préparateur.
