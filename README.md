# WAG One

Portez vos GIFs. WAG One est un pendentif / boucle d'oreille / pin's / bague connecté qui affiche des GIFs en boucle sur un petit écran rond, basé sur un ESP32-C3.

![WAG One](docs/hero.gif)

## C'est quoi ?

WAG One transforme un mini écran (160x160, GC9D01) en accessoire portable capable d'afficher tes GIFs préférés. Upload depuis ton téléphone via une interface web embarquée, choix de la rotation, et lecture en boucle 100% autonome (pas besoin de Wi-Fi une fois les GIFs chargés).

- Formats : pendentif, boucle d'oreille, pin's, bague (structure imprimée en 3D, fichiers à venir)
- Base actuelle : carte Waveshare ESP32-C3-LCD-0.71
- v2 : PCB custom dédié en cours de dev (non disponible pour l'instant, seulement code + visuels)

## Fonctionnement

1. Au démarrage, l'écran affiche "WAG One"
2. Le module tente de se connecter au hotspot Wi-Fi configuré (2 min de tentative)
3. Si connecté : l'IP s'affiche à l'écran, un mini serveur web se lance
   - Depuis ton téléphone : upload de plusieurs GIFs, choix de la rotation d'écran, barre de mémoire flash disponible, bouton "Effacer tout", bouton "Couper le Wi-Fi"
4. Si pas de connexion après 2 min (ou clic sur "Couper le Wi-Fi") : le Wi-Fi est coupé et les GIFs stockés sont lus en boucle depuis la mémoire flash
5. Un appui sur le bouton BOOT physique pendant la lecture relance le Wi-Fi/serveur pour recharger de nouveaux GIFs

## Matériel

| Élément | Détail |
|---|---|
| Carte | Waveshare ESP32-C3-LCD-0.71 |
| Écran | GC9D01, 160x160, rond |
| MCU | ESP32-C3 (RISC-V, Wi-Fi) |
| Stockage | Flash interne via LittleFS (~4 Mo, réservé partiel pour le firmware) |
| Boîtier | Impression 3D (STL disponibles dans `/hardware`) |

La v2 embarquera un PCB custom plus compact pensé pour le portable (batterie LiPo, charge intégrée). Les fichiers de conception seront ajoutés au dépôt dès qu'ils seront stabilisés.

## Installation

1. Cloner le dépôt
2. Ouvrir `firmware/wag_one.ino` dans l'IDE Arduino ou PlatformIO
3. Installer les librairies nécessaires : `TFT_eSPI`, `lvgl`, `LittleFS`, `Preferences`
4. Configurer `TFT_eSPI` pour le driver GC9D01 (voir `/firmware/User_Setup` fourni)
5. Modifier `WIFI_SSID` / `WIFI_PASS` dans le code selon ton réseau
6. Flasher sur la carte ESP32-C3

## Utilisation

Une fois flashé, connecte le module à ton réseau Wi-Fi configuré. L'IP s'affiche à l'écran : ouvre-la dans un navigateur mobile pour uploader tes GIFs, choisir l'orientation et gérer la mémoire. Coupe le Wi-Fi une fois tes GIFs en place pour une autonomie maximale en lecture continue.

## Licence

Ce projet utilise une double licence, une pour le logiciel, une pour le matériel.

- **Code / firmware** (`/firmware`) : [MIT](LICENSE) — libre d'utilisation, modification et redistribution, y compris commerciale.
- **Design matériel** (`/hardware` : schémas, CAD, STL, visuels du produit) : [CC BY-NC-SA 4.0](LICENSE-HARDWARE) — libre de fabriquer, modifier et partager pour un usage personnel ou non lucratif, à condition de citer la source et de partager tes modifs sous la même licence. **La revente commerciale des designs ou de produits dérivés n'est pas autorisée sans accord préalable.**

Les PCB assemblés et les kits officiels restent vendus par moi. Si tu veux fabriquer le tien pour toi-même ou tes potes, vas-y, c'est fait pour ça.

## Roadmap

- [ ] Fichiers STL du boîtier (pendentif, pin's, bague)
- [ ] PCB custom v2 (schémas + gerbers)
- [ ] Gestion batterie LiPo intégrée
- [ ] App mobile dédiée (au lieu du portail web)

## Contribuer

Les PR sur le firmware sont bienvenues (MIT oblige). Pour le hardware, ouvre une issue avant de proposer une modif majeure histoire de coordonner avec la v2 en cours.
