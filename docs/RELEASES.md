# Uitgaven van het spel en wat de port ervan speelt

Het spel heet in Denemarken *Magnus og Myggen*, in Noorwegen *Magnus & Myggen*, in Zweden *Magnus och Myggan*, in
Finland *Manu ja Matti* en in Nederland *Skipper & Skeeto in Pretpark*. Onderzocht met images van archive.org
(oktober 2026), uitgepakt met [tools/discx.py](../tools/discx.py) (ISO, BIN, CloneCD IMG, Joliet) en `unshield`
(InstallShield), vergeleken met [tools/reldiff.py](../tools/reldiff.py) en de Lingo-decompiler.

| uitgave | archive.org | image (SHA-1) | Director | port |
|---|---|---|---|---|
| Nederlands, Transposia 1997 (volume `SKIPPER_1`) | `skipper-1` | `SKIPPER_1.BIN` `4d0ca5bc…` | 5 (1217) | ja (de referentie) |
| Nederlands, herpersing 2003 (`SKIPPER_1`, Joliet) | `skipper1` | `SKIPPER_1.bin` `58600eaf…` | 5 | ja |
| Scandinavisch, Ivanoff, "Magnus 1.5" (`MAGNUS15S`), vier talen | `magnus-mygg-1` | `MagnusMygg1.img` `40fb3afe…` (CloneCD) | 5 (1217) | ja |
| Deens, eerste uitgave 1996 (`MAGNUS11`) | `magnus-myggen-leg-og-laer` | `Magnus-Myggen-Leg-og-Laer.iso` `27f69c23…` | 4 (1117) | nee |
| Deens, *Superstarter* 2006 (`M122DK`) | `leg-og-laer-superstarter-version` | `Leg og Lær - Superstarter Version.iso` `e3e639b2…` | 8.5 (1850) | nee |
| Zweeds, *Lek och Lär*, heruitgave (`M_M_1`) | `m-m-1` | `M&M 1.ISO` `b89c078d…` | 10 (1860) | nee |

Ook gevonden maar niet onderzocht: Duitse (*Max & Mario*), Finse en Noorse uitgaven van de latere delen; van dit
eerste deel geen Duitse of Engelse cd.

## 1. De Nederlandse herpersing van 2003
Alle 64 spelbestanden zijn byte-gelijk aan de cd van 1997; alleen de autorun verschilt (`Skipper.exe`, `Launcher.exe`,
een handleiding als PDF, geen catalogus van Transposia meer). `SETUP.EXE` is dezelfde Wise-installer, dus `start.dxr`
komt er op dezelfde manier uit; de `START32.EXE` op deze cd is een lader zonder film. De cd heeft Joliet-namen; de
ISO9660-namen eronder zijn hoofdletters (`MAGNUSNL.CXT`), wat de port toch al hoofdletterongevoelig opzoekt.

## 2. De Scandinavische cd (Deens, Noors, Zweeds, Fins)
Dezelfde versie 1.5 van het spel als de Nederlandse (Director 5, dezelfde 20 films), maar elke film is opnieuw
opgeslagen en de tekst- en spraakcasts zijn er vier keer: `MAGNUSDK.CXT`, `MAGNUSN.CXT`, `MAGNUSS.CXT`,
`MAGNUSSF.CXT`, en zo ook `MMB01xx`, `MMB04xx`, `MMB09xx`, `MMB10xx` (Nederlands: `MagnusNL.cxt`, `mmb01nl.cxt`, ...).
`Magnus.dxr` kiest de cast met `gCurDlgLib = "Magnus" & string(gCurDlgLanguage)`.

- **Taalkeuze**: `start.dxr` leest `MAGNUS.INI` [Language] `Speak=` en `Text=` (`DK`, `N`, `S`, `SF`; leeg = Deens).
  Spraak en tekst zijn apart te kiezen. De Nederlandse `start.dxr` zet beide vast op `#NL`. Het
  instellingenscherm (F7, `MMDLG1.DXR`) heeft voor elk een rij vlaggen en schrijft de keuze terug naar `MAGNUS.INI`.
  De port schrijft bij de eerste start de taal van het systeem (`da`, `nb`/`nn`/`no`, `sv`, `fi`) in `MAGNUS.INI`,
  anders Deens; `--lang` zet hem altijd.
- **Opstartfilm**: `START32.EXE` staat los op de cd (de Director 5-projector met `start` erin); `SETUP.EXE` is hier
  een kleine Wise-installer zonder projector. De port haalt `start.dxr` eerst uit `START32.EXE` en bewaart hem als
  `start_nordic.dxr` in de opslagmap, zodat hij niet botst met die van de Nederlandse cd.
- **Video**: 62 ABC-video's met Deense namen (`AEBLE.AVI`, `ISBJOERN.AVI`, `FLAGSVEN.AVI`, `FINSKAA.AVI`, ...) voor
  de woorden van alle vier talen; de Nederlandse cd heeft er 26, waarvan 7 alleen daar (`GIRAF.AVI`, `UNIVERS.AVI`, ...).
- **Alfabet**: het ABC-spel eindigt op Æ Ø Å (Deens en Noors) of Å Ä Ö (Zweeds en Fins).
- Het image is een CloneCD-dump (`.img` + `.ccd` + `.sub`) met alleen de datatrack; de port leest de `.img` (ruwe
  2352-byte sectoren) of de `.ccd` ernaast.

## 3. Tekenreeksen in Mac Roman
Elk bestand is als Windows-bestand opgeslagen (platform 2 in VWCF), en de teksten in de casts (STXT) zijn
Windows-1252 (`ø` = `0xF8`). Maar de **tekenreeksen in de Lingo-bytecode** zijn Mac Roman: de scripts zijn op een
Mac gecompileerd en daarna niet opnieuw. Dat geldt voor beide cd's:
- Scandinavisch: de teksten van de dialogen (`"Taustaäänet"`, `"Hämta Spel"`, `"Klicka på bilden ..."`) en het
  alfabet van het ABC-spel (`"Ææ"`, `"Øø"`, `"Åå"`);
- Nederlands: de hoofdletter-/kleineletter-tabel van het spellingspel (MMB10) en `"Markus und Mücki"` in MMB11.

Het ABC-spel geeft `charToNum` van zijn letters aan Windows' `VkKeyScan` om te zien of ze op het toetsenbord staan, en
vergelijkt ze met `the key`: dat werkt alleen als de Windows-projector de tekenreeksen naar Windows-1252 omzette.
De port doet dat bij het laden van een script (`mac_string` in `src/dfile.c`); daarvoor stond er "TaustaŠŠnet".
Buiten Windows telt `VkKeyScan` nu ook de letters met accent als typbaar (`src/xobj.c`).

## 4. Wat er nodig is voor de andere uitgaven
- **Deens 1996 (Director 4)**: een eerdere versie van het spel, voor Windows 3.1. De hoofdfilm zit in de projector
  `MAGNUS.EXE` (28 MB), er zijn `MAGNUS0.DXR` en `MAGNUS1.DXR` in plaats van `Magnus.dxr`, geen externe taalcast, geen
  intro- en dialoogfilms, plaatjes voor de kleurplaten als losse `PIC\PICn`. Nodig: Director 4-bestanden (andere
  cast-, score- en scriptindeling) en de projector als film. Het Deens is ook op de Scandinavische cd te spelen.
- **Superstarter-heruitgaven (Director 8.5 en 10)**: installer-cd's (InstallShield, `DATA1.CAB`); erin dezelfde
  versie 1.5 met dezelfde bestandsnamen (`Magnus.dxr`, `MAGNUSDK.CXT` of `MAGNUSS.cxt`, ...), opgeslagen in een
  nieuwere Director (`DRCF`, `LctX`, Xtras voor tekst, lettertypen en geluid). Nodig: InstallShield uitpakken en de
  Director 8.5/10-indeling (score, tekst als Text Xtra, nieuwe opcodes). Deens en Zweeds zijn ook op de
  Scandinavische cd te spelen.
