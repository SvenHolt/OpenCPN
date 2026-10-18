# OpenCPN Qt6 – neue Oberfläche

Dieses Verzeichnis enthält das neue **Qt6-Frontend** für OpenCPN als Teil der
Migration von wxWidgets nach Qt6. Gestaltet ist es nach dem Vorbild aktueller
Infotainmentsysteme von Tesla, NIO, Xpeng oder BYD:

* **Vollbildkarte** als zentrales Element, alles andere schwebt darüber
* **dunkle Glas-Panels** mit großen Radien und weichen Schatten
* **großer, leichter Tacho** (SOG) mit COG/HDG und Position
* **Navigationskarte** im „Turn-by-Turn“-Stil: Richtungspfeil, Distanz,
  ETA, Restweg und XTE
* **Dock am unteren Rand** mit den App-Bereichen und einer roten MOB-Taste
* **Seitenpanel**, das von links hereingleitet
* **Player-Karte** für Spotify und YouTube Music (Plugin)
* **runde Kartenbuttons**: Zoom, North-up/Course-up und Schiff folgen
* **Nacht-, Tag- und Automatikmodus** (Wechsel um 07:00 und 19:00 Uhr),
  der sich auf die Seekarten auswirkt
* flüssige Bedienung: kinetisches Schwenken, animiertes Zoomen,
  Pinch-Gesten, interpoliert bewegtes Eigenschiff; die Karten werden in
  einem Hintergrund-Thread gerendert

| Cockpit (Startbildschirm) · Segeln | Cockpit · Motorsegeln mit Motordaten |
| --- | --- |
| ![Cockpit](doc/screenshot-cockpit.png) | ![Cockpit Motor](doc/screenshot-cockpit-engine.png) |
| **S-57-ENC · Nacht · Spotify** | **Wetter (GRIB)** |
| ![ENC](doc/screenshot-enc.png) | ![Wetter](doc/screenshot-weather.png) |
| **Gezeiten · Tag** | **Aktive Route · Nacht** |
| ![Gezeiten](doc/screenshot-tides.png) | ![Route](doc/screenshot-night.png) |
| **Routenplanung von Hafen zu Hafen** | **Törnplanung: Kreuzen, Zeitfenster** |
| ![Routenplanung](doc/screenshot-route-planning.png) | ![Törn](doc/screenshot-passage.png) |
| **Bordsysteme · Nachtfahrt** | |
| ![Boot](doc/screenshot-boat.png) | |
| **Wettermodelle × Abfahrtszeit** | **Polardaten** |
| ![Modelle](doc/screenshot-models.png) | ![Polare](doc/screenshot-polar.png) |
| **Kartenausschnitt für Offline-Daten wählen** | **Offline-Gebiete · lokale Overpass-Instanz** |
| ![Auswahl](doc/screenshot-offline-select.png) | ![Offline](doc/screenshot-offline.png) |

## Funktionsumfang

| Bereich | Status |
| --- | --- |
| **Cockpit-Startbildschirm**: animiertes Boot in der Kompassrose | ✅ |
| **Segelkonfiguration** (Groß/Reffs, Vorsegel/Rollung, Leichtwindsegel, Antrieb) | ✅ |
| **Wind, Fahrt durchs Wasser, Tiefe, Motordaten** (NMEA MWV/MWD/VHW/DPT/DBT/RPM/XDR) | ✅ |
| Vektor-Basiskarte (OpenCPN `data/basemap_shp`, 2 Detailstufen) | ✅ |
| **S-57-ENC** (`*.000` + Updates), eigene S-52-nahe Darstellung | ✅ |
| **Rasterkarten BSB/KAP** (NOAA RNC u. a.) mit REF-Georeferenzierung | ✅ |
| Kartenordner, Hintergrund-Scan, Überlagerung nach Maßstab, Umrisse | ✅ |
| **Gezeiten und Strömungen** (3159 Stationen aus `data/tcdata`) | ✅ |
| **GRIB1/GRIB2**: Wind, Böen, Luftdruck, Wellen; Zeitleiste und Animation | ✅ |
| **NOAA-GFS-Download** (0,25°) für den sichtbaren Bereich | ✅ |
| **Track-Aufzeichnung** mit Ausdünnung, GPX-Import/-Export | ✅ |
| **Offline-Gebiete**: Kartenausschnitt wählen, OSM-Daten laden, **lokale Overpass-Instanz** (osm-3s) | ✅ |
| Häfen, Marinas, Tankstellen, Slipanlagen und Seezeichen aus OSM auf der Karte, Offline-Suche | ✅ |
| **Plugin-API** (Qt-Plugins) mit **Media-Plugin** (Spotify, YouTube Music) | ✅ |
| NMEA 0183: RMC, GGA, GLL, VTG, HDT, THS inkl. Checksumme | ✅ |
| AIS: Typen 1/2/3, 5, 18, 19, 24; CPA/TCPA und Kollisionsalarm | ✅ |
| Datenquellen: UDP, TCP, seriell, Simulator; Datenmonitor | ✅ |
| **Routenplanung von Hafen zu Hafen** (automatisch über Wasser, Land und Molen werden umfahren) | ✅ |
| **Törnplanung**: Zeitfenster, Segel-/Motoranteil, Wenden/Halsen, Dauer, Ankunft, Diesel, Tageslicht (GRIB-Wind) | ✅ |
| **Häfen online nachladen** (Overpass-Kacheln mit Cache, Nominatim-Suche) | ✅ |
| **Polardaten**: Import (.pol/.csv/Expedition), Tabelleneditor, Diagramm, optimale Kreuz-/Vorwindwinkel | ✅ |
| **Wettermodelle vergleichen** (ICON, GFS, ECMWF, Météo-France, GRIB, manuell) × Abfahrtszeiten | ✅ |
| **Bordsysteme**: Kabinen-Layout, Temperaturen, Tanks, Victron, Webasto, Licht-Szenen, Verbraucher (simuliert, Signal K folgt) | ✅ |
| **Reisen offline speichern** (Route, Planung, Modelle, GRIB, Häfen und Küste entlang der Route) | ✅ |
| Routen zeichnen und abfahren, Markierungen, Mann über Bord | ✅ |
| GPX-Import/-Export (auch OpenCPN `navobj.xml`) | ✅ |
| S-63 (verschlüsselte ENC), CM93 | ❌ siehe Grenzen |

### Cockpit

Startbildschirm im Stil der Fahrzeugansicht moderner E-Autos. Über das Dock
wechselt man zwischen **Cockpit** und **Karte**, mit der Taste `C` ebenso;
in den Einstellungen lässt sich die Karte als Startansicht wählen.

* **Kompassrose (Heading-up)** mit Steuerstrich. Am Ring markiert:
  * Kurs über Grund
  * Peilung zum nächsten Wegpunkt
  * wahrer Wind als Pfeil, farbig nach Stärke und mit Knotenangabe
  * scheinbarer Wind
* **Animiertes Boot** von oben:
  * Groß, Vorsegel und Leichtwindsegel nach der eingestellten
    Konfiguration; Reffs werden markiert, die Genua ist gerollt
  * die Segel stehen auf der Leeseite und werden mit dem scheinbaren
    Windwinkel getrimmt; am Wind killen sie
  * Krängungsschatten und Kielwasser, abhängig von der Fahrt
  * Schraubenwasser bei laufendem Motor
  * Wind- und Wasserpartikel
* **Datenkarten:**
  * Fahrt über Grund mit Fahrt durchs Wasser und Tiefe; Flachwasserwarnung
    unter der Sicherheitstiefe
  * Kurs über Grund mit Position
  * wahrer Wind (Stärke, Richtung, Beaufort) und scheinbarer Wind; fehlt
    ein Sensor für den wahren Wind, wird er aus dem scheinbaren Wind und
    der Bootsgeschwindigkeit berechnet
  * nächster Wegpunkt mit DTW, BTW, ETA und XTE
* **Motorkarte**, sobald der Motor läuft (RPM > 100) oder „Motor“ bzw.
  „Motorsegeln“ eingestellt ist:
  * Drehzahlbogen
  * Kühlwasser, Öldruck und Batterie mit Warnfarben
  * Tankstand in Prozent und Litern
  * Reichweite aus Tankinhalt, Verbrauch und SOG
  * Betriebsstunden
* **Segel-Seite** (Tippen auf die Segelanzeige oder Dock „Segel“):
  * Antrieb: Segeln, Motorsegeln, Motor
  * Großsegel: geborgen, voll, 1.–3. Reff
  * Vorsegel: Genua mit Rollung in %, Fock, Sturmfock
  * Leichtwindsegel: Gennaker, Spinnaker, Code 0
  * Tank: Inhalt, Verbrauch bei Marschdrehzahl, Füllstand ohne Sensor
* **Motordaten per NMEA 0183:**
  * `RPM` (Quelle E)
  * `XDR` mit Kühlwasser (`C`, Name `ENGINE…`), Öldruck (`P`, `…OIL…`),
    Tank (`V`, `FUEL…`), Batterie (`U`, `BAT…`), Betriebsstunden (`G`,
    `…HOUR…`) und Verbrauch (`E`, `FUELRATE…`)
* **Simulator:**
  * segelt nach einer einfachen Polare
  * fällt am Wind auf 40° ab
  * motort passend zur Konfiguration
  * liefert Wind, Fahrt durchs Wasser, Tiefe und Motordaten

### Seekarten

* **S-57:** Gelesen wird mit OpenCPNs vorhandenen, wx-freien Bibliotheken
  `libs/iso8211`, `libs/gdal` und `libs/s57-charts`. Updates (`.001` …)
  werden automatisch angewendet. Die Darstellung folgt S-52 in vereinfachter
  Form und passt sich dem Nacht-/Tag-Theme an:
  * Tiefenzonen nach Flach-, Sicherheits- und Tiefwassertiefe (einstellbar)
  * hervorgehobene Sicherheitskontur
  * Lotungen (flache Werte kräftiger)
  * Tonnen nach Form und Farbe, Baken
  * Feuer mit Sektoren und Kennung (z. B. „Fl(2) G 6s“)
  * Wracks, Felsen und Hindernisse mit Gefahrenkreis
  * Kabel, Rohrleitungen, Sperr- und Ankergebiete
  * Bauwerke und Gebietsnamen
* **BSB/KAP:** eigener Leser für BSB 1–3 (RLE, Palette, REF/PLY). Bei
  Mercator-Karten ist die Georeferenzierung exakt. Nachts werden die Karten
  gedimmt.
* Unter **Karten** fügt man Ordner hinzu; das Scannen läuft im Hintergrund.
  Grobe Karten werden zuerst gezeichnet, detaillierte darüber. Ist eine Karte
  für die aktuelle Zoomstufe zu detailliert, erscheint nur ihr Umriss.

### Gezeiten

Harmonische Vorhersage nach demselben Verfahren wie OpenCPN/XTide mit
`HARMONICS_NO_US`/`.IDX`; Unterstationen werden mit Zeit- und Höhenversatz
berechnet.

* **Auf der Karte:** Pegel-Plaketten mit Steigt/Fällt-Pfeil, Strömungen
  in Knoten.
* **Seitenpanel:** Tidenkurve über 24 Stunden mit Jetzt-Marke und dem
  nächsten Hoch- bzw. Niedrigwasser.
* **Geprüft** gegen die offizielle Vorhersage des Canadian Hydrographic
  Service für Halifax: Zeiten ±1–11 min, Höhen ±0,12 m.

### Wetter (GRIB)

* **Formate:**
  * GRIB1 mit Simple Packing und Bitmap
  * GRIB2 mit Simple Packing (5.0), Complex Packing (5.2) und Complex
    Packing mit räumlicher Differenzierung (5.3); das ist das Format von
    NOAA GFS
* **Geprüft:** Die Decoder liefern dieselben Werte wie ECMWF eccodes, über
  alle Gitterpunkte.
* **Darstellung:**
  * Windgeschwindigkeit als Farbfläche
  * farbige Windpfeile mit Knotenangabe
  * Isobaren im 4-hPa-Abstand
  * Zeitleiste im Stil eines Mediaplayers mit Abspielfunktion
  * Werte am Boot (Wind, Böen, Druck)
* **„Windvorhersage laden“** holt NOAA GFS 0,25° (Wind 10 m, Böen,
  Bodendruck, 0–72 h in 3-Stunden-Schritten) direkt von NOMADS für den
  sichtbaren Kartenausschnitt.

### Bordsysteme (Ansicht „Boot“)

Dritte Ansicht im Dock neben Cockpit und Karte (Taste `B`):

* **Kabinen-Layout** einer 28-Fuß-Yacht im Stil der Dufour 2800, von oben:
  * Bugkabine mit V-Koje, Salon mit Sitzbänken, Tisch und Kartentisch,
    Pantry, Bad, Hundekoje (Achterkabine) und Cockpit.
  * Jeder Raum ist nach seiner Temperatur eingefärbt (blau kalt, orange
    warm). Außentemperatur im Cockpit.
  * Leuchten erscheinen in ihrer Farbe und Helligkeit. Ein Tipp auf eine
    Leuchte schaltet sie, ebenso die Positionslaternen (rot/grün/weiß),
    Dampferlicht, Ankerlicht und Decksstrahler.
  * Frischwasser-, Fäkalien- und Dieseltank mit Füllstand; Batteriebank
    unter der Salonbank; Solarpaneele am Heckbügel mit aktueller Leistung;
    Landstromanschluss am Heck.
  * Webasto mit den Warmluftleitungen in die Räume (orange beim Heizen).
* **Lichtszenen:** Nachtfahrt (rot, stark gedimmt, nur Salon, Kartentisch,
  Fußbodenlicht, Pantry, Bad), Abend (warmweiß 50 %), Hell, Vor Anker
  (Ankerlicht an, Positionslaternen aus, Salon indirekt) und Alles aus.
  Positionslaternen schalten die Szenen nicht mit. Das Modell kennt je LED
  Helligkeit und Farbe; die Bedienung pro LED folgt mit der digitalen
  Schaltung.
* **Energie (Victron):** Ladezustand, Spannung, Strom und Restlaufzeit der
  Batteriebank (SmartShunt), Solar (MPPT-Zustand, Tagesertrag), Landstrom
  mit Ladegerät (MultiPlus), Verbraucher gesamt.
* **Heizung (Webasto Air Top):**
  * Ein/Aus und Solltemperatur.
  * Zustände: Start, Heizen, Lüften, Nachlauf, Störung.
  * Werte: Leistung, Ausblastemperatur, Dieselverbrauch, Spannung,
    Betriebsstunden.
* **Tanks** und **Verbraucher** (Kühlschrank, Wasserpumpe, Instrumente,
  Plotter, UKW, Autopilot, Steckdosen, Stereo, Wechselrichter,
  Bilgepumpe) mit Schaltern.

Noch sind alle Werte **simuliert**: Temperaturen folgen Tagesgang und
Heizung, die Batterie reagiert auf Verbraucher, Sonnenstand und Landstrom,
und Tanks leeren sich. Jeder Wert und jeder Schalter trägt bereits seinen
Signal-K-Pfad (`environment.inside.<raum>.temperature`,
`tanks.freshWater.0.currentLevel`, `electrical.switches.<id>.state` …).
Die Signal-K-Anbindung und die Elektronik folgen später.

### Routenplanung von Hafen zu Hafen

Oben auf der Seite **Routen**:

1. **Start** und **Ziel** eingeben. Vorgeschlagen werden die eigene
   Position, Markierungen und, aus den Offline-Gebieten, Marinas, Häfen,
   Ankerplätze und Orte. Alternativ per Rechtsklick auf die Karte
   „Route von hier“ bzw. „Route hierher“ wählen.
2. **Route planen**: Die App berechnet den kürzesten Weg über Wasser und
   zeigt ihn gestrichelt als Vorschau, mit Distanz, Fahrzeit bei der
   eingestellten Geschwindigkeit und Anzahl der Wegpunkte.
3. **Speichern** oder **Speichern und navigieren**. Die Wegpunkte heißen
   wie Start und Ziel, dazwischen WP01, WP02 …

So rechnet der Planer:

* Das Gebiet um Start und Ziel wird als Raster erfasst (bis 700 Zellen je
  Seite, mindestens etwa 20 m pro Zelle).
* Land kommt aus der Basiskarte. In Offline-Gebieten stammen die
  Küstenlinie (`natural=coastline`, Land liegt links) und Molen/Buhnen aus
  der lokalen Overpass-Instanz. Sie sind viel genauer, und dort wird die
  Basiskarte ignoriert.
* A* sucht mit Abstand zum Ufer. Danach wird der Weg auf die nötigen
  Wendepunkte begradigt.
* Liegen Start oder Ziel an Land (z. B. die Mitte einer Marina), wird die
  nächste erreichbare Wasserfläche gewählt.

#### Polardaten

Unter **Segel → Polare** liegt die Polare des Bootes:

* **Import** der üblichen Formate:
  * Tabellen von OpenCPN, qtVlm und ORC, auch als CSV mit Semikolon und
    Dezimalkomma. Die erste Zeile enthält die Windgeschwindigkeiten, die
    erste Spalte die Windwinkel.
  * Expedition: eine Zeile pro Windstärke im Format
    „TWS Winkel Fahrt Winkel Fahrt …“.
* **Bearbeiten:** Tabelleneditor mit Zeilen und Spalten zum Hinzufügen und
  Entfernen. Die Eingaben werden beim Speichern geprüft (Werte aufsteigend,
  Winkel 0–180°).
* **Export** als `.pol`.
* **Generisch:** eine Fahrtenyacht-Polare, skaliert auf die beste Fahrt.
* **Diagramm:** Kurven für 6–25 kn, die Punkte markieren den besten VMG
  am Wind und vor dem Wind.

Der Törnplaner rechnet mit der Polare (bilinear interpoliert). Kreuz- und
Vorwindwinkel sind die Winkel mit dem besten VMG bei der jeweiligen
Windstärke.

#### Wettermodelle und Abfahrtszeit

Nach der Routenberechnung lädt die App bei Verbindung automatisch die
stündlichen Windvorhersagen von **ICON (DWD)**, **GFS (NOAA)**,
**ECMWF IFS** und **Météo-France** für Punkte entlang der Route
(Open-Meteo, 7 Tage). Die Matrix zeigt für jede Quelle die Abfahrten im
Zeitfenster:

* Zeilen: die Modelle, eine geladene GRIB-Datei und der manuelle Wind.
* Spalten: die Abfahrtszeiten im Zeitfenster.
* Zellfarbe: Segelanteil. Zelltext: Dauer. Unterer Rand: Nachtanteil.
* Grün umrandet ist die beste Abfahrt je Quelle, weiß die gewählte. Ein
  Tipp wählt Quelle und Abfahrt für die Detailansicht.
* Darüber steht, wie weit die Modelle für die gewählte Abfahrt
  auseinanderliegen.

#### Reisen offline speichern

**„Reise für offline speichern“** legt die Reise unter
`voyages/<id>/voyage.json` im Benutzerdatenordner ab und lädt nach:

1. Vorhersagen aller Wettermodelle entlang der Route.
2. Eine Kopie der geöffneten GRIB-Datei.
3. Häfen, Marinas und Seezeichen in Kacheln entlang der Route (±5 sm).
   Sie werden ohne Ablaufdatum gespeichert.
4. Offline-Regionen (Küste, Häfen) entlang der Route (±3 sm) für die lokale
   Overpass-Instanz. Sie werden nacheinander geladen und am Ende einmal
   importiert.

Unter **Reisen (offline)** auf der Routen-Seite öffnet ein Tipp die Reise
mit Route, Eingaben und den gespeicherten Vorhersagen, auch ohne Internet.
Mit Verbindung werden die Vorhersagen frisch geladen. „Aktualisieren“
lädt fehlende oder neue Daten nach.

#### Suche

Für die erste Planung fragt die Suche zuerst Online-Quellen: Nominatim
und Overpass (Marinas, Häfen, Ankerplätze im Umkreis der Karte). Danach
kommen die lokale Overpass-Instanz und die gespeicherten Hafenkacheln.
Ohne Netz findet sie so weiterhin die Häfen entlang gespeicherter Reisen
und kürzlich angesehener Gebiete.

#### Törnplanung

Nach der Berechnung zeigt der Bereich **Törn**:

* **Abfahrt** und **Zeitfenster** (nur diese Zeit oder die nächsten
  6/12/24/48/72 Stunden). Im Fenster rechnet die App jede halbe bis
  zwei Stunden eine Abfahrt durch und wählt die beste nach **schnellster
  Fahrt**, **meistem Segeln** oder **Tageslicht**. Die Zeitleiste zeigt
  alle Abfahrten: Höhe = Dauer, Blau = Segelanteil, dunkel = Nachtanteil.
  Ein Tipp auf einen Balken wählt diese Abfahrt.
* **Antrieb:** Segeln (Motor nur bei Flaute oder zu wenig Fahrt),
  Motorsegeln (jeweils das Schnellere) oder Motor. Voreinstellung ist die
  Antriebsart der Segelkonfiguration.
* **Wind** aus der geladenen **GRIB-Vorhersage**, entlang der Route und in
  der Zeit interpoliert. Ohne GRIB oder außerhalb davon gilt der manuell
  eingegebene Wind; vorbelegt wird der aktuelle wahre Wind der Instrumente.
* **Ergebnis:** Dauer, Ankunft, Segel-/Motoranteil, Wenden und Halsen,
  gesegelte Distanz (mit Kreuzschlägen), Diesel (Verbrauch aus der
  Motoreinstellung), Nachtstunden, Sonnenuntergang am Ziel.
* **Warnungen:** Starkwind ab 25 kn, Sturm ab 30 kn, Ankunft im Dunkeln,
  GRIB deckt den Törn nicht ganz ab, Diesel wird knapp.
* **Etappen:** Kurs, Distanz, Fahrmodus (Segeln, Kreuzen, Vorwind,
  Motor), Wenden/Halsen, Zeiten, Wind und TWA. Auf der Karte ist jede
  Etappe nach Fahrmodus eingefärbt.

So wird gerechnet: Die Bootsgeschwindigkeit stammt aus der Polare des
Bootes (siehe oben). Gekreuzt bzw. gehalst wird mit den Winkeln des besten
VMG. Pro Schlaglänge von
etwa 1,5 sm wird eine Wende gezählt. Der Wind wird alle 2 sm neu bestimmt.
Beim Speichern erhalten die Wegpunkte ETA bzw. ETD und Fahrmodus.

Tiefen, Fahrwasser, Sperrgebiete und Brücken werden dabei **nicht**
berücksichtigt. Die Route ist ein Vorschlag und muss vor der Fahrt auf der
Seekarte geprüft werden.

Passen nicht alle Bereiche ins Dock, landen die hinteren unter **Mehr**.

### Häfen online nachladen

Mit Netzwerkverbindung (abschaltbar unter Offline → „Häfen online laden“):

* **Karte:** Für den sichtbaren Ausschnitt lädt die App Häfen, Marinas,
  Tankstellen, Slipanlagen, Ankerplätze und Seezeichen in Kacheln von
  0,1° × 0,1° über öffentliche Overpass-Server nach, während man die Karte
  verschiebt. Kacheln werden 14 Tage auf der Festplatte zwischengespeichert
  und sind danach auch ohne Netz da.
* **Server:** overpass-api.de, overpass.kumi.systems und
  overpass.private.coffee (in den Einstellungen `Overpass/onlineServers`
  änderbar). Antwortet einer nicht innerhalb von 6 s, wird parallel der
  nächste gefragt, die erste gültige Antwort zählt. Ohne Verbindung wird
  es nach einer Minute erneut versucht.
* **Suche:** Start und Ziel findet die App zusätzlich über Nominatim
  (OpenStreetMap-Ortssuche, höchstens eine Anfrage pro Sekunde, Ergebnisse
  nahe der Karte zuerst).
* **Routenplanung:** Ohne Offline-Gebiet holt sie die Küstenlinie und die
  Molen für das Planungsgebiet online.
* Offline-Gebiete der lokalen Overpass-Instanz haben immer Vorrang.

### Offline-Gebiete und lokale Overpass-Instanz

Unter **Offline** legt man Gebiete an, deren OpenStreetMap-Daten (Häfen,
Marinas, Tankstellen, Slipanlagen, Seezeichen, Küste, Orte) lokal
gespeichert und in eine **eigene Overpass-API-Instanz** importiert werden.
Abfragen laufen danach ohne Internet.

**Kartenausschnitt wählen** („Bereich auf Karte wählen“, Rechtsklick →
„Gebiet offline speichern…“):

* Ein Rahmen liegt über der Karte, der Bereich außerhalb wird abgedunkelt.
* **Ecken und Kanten ziehen** ändert die Größe, **im Rahmen ziehen**
  verschiebt ihn, **außerhalb ziehen** schwenkt die Karte; Zoomen geht wie
  gewohnt. Alles ist auch per Touch bedienbar.
* Die Leiste oben zeigt Breite × Höhe und Fläche und bietet
  Vorlagen: **Sichtbarer Bereich**, **Um das Boot** (± 8 sm) und
  **Entlang der Route** (aktive oder letzte Route, 3 sm Rand).
* Schon geladene Gebiete werden grün gestrichelt angezeigt.
* Name eingeben, **Laden** – Fortschritt und Abbruch auf der Offline-Seite.

**Download:** zuerst über einen Overpass-Server (Standard:
`overpass-api.de`, einstellbar). Ist er nicht erreichbar, lädt die App
kleinere Gebiete (bis 0,25 Quadratgrad) kachelweise über die
OpenStreetMap-API und teilt zu volle Kacheln automatisch. Eigene
`.osm`-Dateien lassen sich importieren (Kommandozeile: `--import-osm`).
Die Kacheln werden zusammengeführt (IDs eindeutig und sortiert) und als
`regions/<id>.osm` gespeichert; alle Gebiete zusammen ergeben
`combined.osm`.

**Overpass-Backend** (Offline → Overpass-Einstellungen):

| Backend | Plattform | Einrichtung |
| --- | --- | --- |
| **Lokal (osm-3s)** | Linux, macOS | `-DOCPN_QT_BUILD_OSM3S=ON` baut `update_database`, `osm3s_query` und `dispatcher` mit (Overpass 0.7.62.11, braucht `libexpat1-dev zlib1g-dev`, optional `liblz4-dev`) und installiert sie nach `libexec/osm-3s/bin`. Gefunden wird osm-3s auch über `OPENCPN_OSM3S_BIN`, den `PATH` oder `~/.local/osm-3s/bin`. |
| **HTTP** | alle, v. a. Windows | `docker compose -f qt/docker/overpass/docker-compose.yml up -d` startet das Image `wiktorn/overpass-api`, das `combined.osm` importiert und unter `http://localhost:12345/api/interpreter` antwortet. Ebenso geht jeder andere Overpass-Server. |

Beim lokalen Backend ruft die App nach jeder Änderung
`update_database --db-dir=… --meta=no` mit `combined.osm` auf und fragt die
Datenbank direkt mit `osm3s_query` ab, ein Dispatcher-Dienst ist nicht nötig.
Die Daten liegen im Benutzerdatenordner unter `overpass/` (Linux:
`~/.local/share/opencpn/opencpn-qt/overpass`).

Auf der Karte zeigt die Ebene **OSM-Details** ab Hafenmaßstab Marinas,
Häfen, Tankstellen, Ankerplätze und Slipanlagen, bei noch größerem Maßstab
auch Tonnen und Baken in ihrer Farbe. Überlappende Symbole und Beschriftungen
werden ausgeblendet. Die **Offline-Suche** findet Namen (auch
`seamark:name`) in allen Gebieten; ein Tipp auf das Ergebnis zeigt den Ort.

### Plugins und Media-Plugin

Die Plugin-API (`src/api/OcpnPlugin.h`) basiert auf Qt-Plugins
(`QPluginLoader`). Über `PluginHost` kann ein Plugin:

* Dock-Bereiche mit Seitenpanel-Seite hinzufügen,
* schwebende Karten in einer Bildschirmecke platzieren,
* Kartenebenen (`MapLayer`) zeichnen,
* Meldungen anzeigen und auf die Navigationsdaten zugreifen.

Plugins werden an diesen Stellen gesucht:

* `<bin>/plugins`
* `<prefix>/lib/opencpn-qt/plugins`
* im Benutzerdatenordner
* in `OPENCPN_QT_PLUGIN_PATH`

In den Einstellungen lassen sie sich einzeln ein- und ausschalten.

Das **Media-Plugin** (`plugins/media`) zeigt eine Player-Karte mit Cover,
Titel, Fortschritt und Wiedergabesteuerung, wie man sie aus Tesla-Fahrzeugen
kennt:

* **Linux (MPRIS):** Spotify-Desktop, die YouTube-Music-Desktop-App und
  `music.youtube.com` bzw. `open.spotify.com` im Browser (Chrome, Firefox,
  Edge) werden automatisch erkannt und gesteuert. Ein Konto ist nicht
  nötig.
* **Spotify-Web-API (alle Plattformen):** Steuert Spotify-Connect-Geräte
  wie Handy, Lautsprecher oder Auto. Einrichtung:
  1. Auf [developer.spotify.com](https://developer.spotify.com/dashboard)
     eine App anlegen.
  2. Die Redirect-URI `http://127.0.0.1:8898/callback` eintragen.
  3. Die Client-ID unter **Media** einfügen und „Spotify verbinden“
     wählen.

  Die Anmeldung läuft über OAuth mit PKCE, ein Client-Secret ist daher
  nicht nötig. Für die Wiedergabesteuerung verlangt Spotify ein
  Premium-Konto.
* **YouTube Music** hat keine öffentliche Wiedergabe-API. Es wird deshalb
  über MPRIS gesteuert (Desktop-App oder Browser-Tab).

## Bauen und Starten

Voraussetzungen:

* CMake ≥ 3.16 und ein C++17-Compiler
* Qt ≥ 6.4 mit den Modulen Core, Gui, Widgets, Network, Svg und Concurrent
* optional: SerialPort (serielle NMEA-Quellen) und DBus (MPRIS)

```bash
# Debian/Ubuntu
sudo apt install qt6-base-dev qt6-svg-dev libqt6serialport6-dev dbus
# für die lokale Overpass-Instanz zusätzlich
sudo apt install libexpat1-dev zlib1g-dev liblz4-dev

cmake -S qt -B build-qt -DCMAKE_BUILD_TYPE=Release -DOCPN_QT_BUILD_OSM3S=ON
cmake --build build-qt -j
./build-qt/opencpn-qt --demo        # Simulator und Demo-Route
(cd build-qt && ctest)              # Unit-, Karten-, GRIB-, OSM-, Planungs-, Media- und UI-Tests
```

Daten (Basiskarte, `s57data`, `tcdata`) werden automatisch im Quellbaum
(`data/`) oder unter `<prefix>/share/opencpn` gesucht. Alternativ lässt sich
der Pfad mit `OPENCPN_DATA_DIR` setzen.

| Option | Wirkung |
| --- | --- |
| `--demo` | legt eine Demo-Route an und aktiviert sie |
| `--charts <ordner>` | fügt einen Kartenordner hinzu |
| `--grib <datei>` | öffnet eine GRIB-Datei |
| `--center lat,lon[,maßstab]` | zentriert die Karte, z. B. `40.84,-73.93,15000` |
| `--view cockpit\|chart\|boat` | Startansicht |
| `--day` | startet im Tagmodus |
| `--page N` | öffnet das Seitenpanel N (0 Karten, 1 Routen …) |
| `--import-osm datei.osm` | importiert eine OSM-XML-Datei als Offline-Gebiet |
| `--select-area` | startet die Auswahl eines Kartenausschnitts |
| `--fresh` | nutzt temporäre Einstellungen und Daten; die echte Konfiguration bleibt unberührt |
| `--screenshot datei.png [--size 1600x1000] [--delay ms]` | speichert einen Screenshot und beendet das Programm, auch mit `QT_QPA_PLATFORM=offscreen` |

Tastatur: `+`/`-` zoomen, `F` folgt dem Schiff, `F11` schaltet Vollbild um,
`Esc` schließt das Panel bzw. bricht die Routeneingabe oder die
Gebietsauswahl ab, `Backspace`
entfernt den letzten Routenpunkt. Ein Rechtsklick auf die Karte öffnet das
Kontextmenü.

Kostenlose Testkarten gibt es zum Beispiel als ENCs der NOAA unter
charts.noaa.gov. Der Simulator und UDP-Port 10110 sind beim ersten Start
aktiv.

## Grenzen dieses Stands

* **S-63 (verschlüsselte ENCs)** ist nicht umgesetzt. Dafür bräuchte es eine
  IHO-Herstellerkennung (M_ID, M_KEY), Zertifikate und Testdaten des
  Datenservers. Das lässt sich als Plugin nachrüsten, wie bisher mit
  `s63_pi`.
* **CM93** ist nicht umgesetzt. Es ist ein veraltetes, proprietäres Format
  ohne frei verfügbare Testdaten.
* Die **S-52-Darstellung** ist vereinfacht und deckt die navigatorisch
  wichtigsten Objekte ab. Nicht enthalten sind die vollständige bedingte
  Symbolisierung (CSP), Symbol-Bitmaps aus `chartsymbols.xml` und
  Objektabfragen per Klick. Es gibt **keine Typzulassung**; nicht als
  alleiniges Navigationsmittel verwenden.
* Kartenüberlagerung (Quilting) erfolgt einfach nach Maßstab. Es gibt keine
  Speicherverwaltung für sehr große Kartensätze.
* Gezeiten: Die binären `.tcd`-Dateien (libtcd) werden noch nicht gelesen;
  die ASCII-Harmonics decken die mitgelieferten Stationen ab.
* GRIB: JPEG-2000- und PNG-gepackte GRIB2-Daten (5.40/5.41) sowie
  Nicht-Lat/Lon-Gitter (z. B. Lambert) werden nicht unterstützt.
* Plugins: Bestehende wxWidgets-Plugins sind nicht kompatibel. Die neue API
  ist bisher nur unter Linux/macOS getestet.
* Media: Unter Windows und macOS gibt es noch keine Systemmediensteuerung;
  dort funktioniert nur die Spotify-Web-API.
* Overpass: osm-3s lässt sich nicht unter Windows bauen, dort bleibt Docker
  (HTTP-Backend). Gebiete werden komplett neu importiert, Minutendiffs werden
  nicht eingespielt („Aktualisieren“ lädt das Gebiet neu). Die OSM-Daten
  ersetzen keine amtliche Seekarte.

## Architektur

```
qt/
├── src/api/OcpnPlugin.h   öffentliche Plugin-API (PluginHost, OcpnPlugin)
├── src/core/              UI-unabhängig (libocpnqt_core.so)
│   ├── GeoMath            Mercator, Großkreis, XTE, CPA/TCPA
│   ├── Nmea, Ais          Parser + AIS-Decoder
│   ├── NavStore           Eigenschiff, Wind, Motor, Routen, AIS, Zielführung
│   ├── BoatConfig         Segelkonfiguration, Antrieb, Tank
│   ├── Connections        UDP/TCP/Seriell/Simulator → NMEA → NavStore
│   ├── GpxIO, TrackRecorder
│   ├── Tides              harmonische Gezeitenvorhersage
│   ├── Grib, GribDownload GRIB1/2-Decoder, NOAA-GFS-Download
│   ├── RoutePlanner       Routing über Wasser (Raster, A*, Begradigung)
│   ├── PassagePlanner     Törnplanung (Polare, Kreuzen, Zeitfenster, Sonne)
│   ├── Polar              Polardaten (Formate, Interpolation, VMG)
│   ├── WeatherModels      Modellvorhersagen entlang der Route (Open-Meteo)
│   ├── Voyage             gespeicherte Reisen und Offline-Vorbereitung
│   ├── BoatSystems        Bordsysteme (Räume, Tanks, Victron, Webasto, Licht)
│   ├── osm/               Overpass-Dienst (osm-3s/HTTP), Offline-Gebiete, OSM-Merge,
│   │                      OnlineOsm (öffentliche Server, Kachel-Cache, Nominatim)
│   └── charts/            Chart, S57Chart, BsbChart, ChartDatabase
├── src/ui/                Widgets (libocpnqt_ui.so)
│   ├── Theme, Icons, GlassPanel
│   ├── CockpitView, SailPage Cockpit-Startbildschirm, Segelkonfiguration
│   ├── ChartView, MapLayer   Karte (Render-Thread) + Overlay-Ebenen
│   ├── Overlays, SideDrawer, DrawerPages
│   ├── TracksPage, TidesPage, WeatherPage
│   ├── OfflinePage        Gebietsauswahl-Leiste, Offline-Seite, OSM-Ebene
│   ├── RoutePlanPanel     Routenplanung von Hafen zu Hafen
│   ├── PassagePanel       Törnplanung, Modell-Matrix, Etappen
│   ├── PolarPanel         Polardiagramm, Import, Tabelleneditor
│   ├── VoyageList         Reisen (offline)
│   ├── BoatView           Ansicht „Boot“: Kabinen-Layout und Systemkarten
│   ├── PluginManager
│   └── MainWindow         Layout, PluginHost-Implementierung
├── plugins/media/         Media-Plugin (MPRIS, Spotify-Web-API)
├── docker/overpass/       Overpass-API als Docker-Container (HTTP-Backend)
└── tests/                 QtTest-Suiten + Testdaten (tests/data)
```

Core und UI sind Shared Libraries, damit Plugins dieselben Singletons
(Theme, Navigationsdaten) nutzen wie die Anwendung.

## Migrationsplan wxWidgets → Qt6

Der wx-Code (`gui/`, `model/`, `libs/`) umfasst rund 226 000 Zeilen. Die
Migration läuft parallel: Das wx-Programm bleibt voll funktionsfähig,
während das Qt-Frontend Stück für Stück Funktionen übernimmt.

1. ✅ **Neue Oberfläche:** Qt6-Programm mit modernem Design, NMEA/AIS,
   Routen und GPX.
2. ⏳ **`model/` von wx entkoppeln** (`wxString` → `std::string`,
   `wxDateTime` → `std::chrono`, Events → `libs/observable`). Danach kann
   das Qt-Frontend die vorhandenen Kommunikationstreiber (N2K, SignalK)
   übernehmen.
3. ✅ **Karten:** S-57 über die vorhandenen Reader, BSB neu. ⏳
   Vollständige S-52-CSP, S-63-Plugin.
4. ✅ **Fachfunktionen:** Gezeiten, GRIB inkl. Download, Tracks. ⏳
   Ankeralarm, Wetterrouting.
5. ✅ **Plugin-API mit Media-Plugin.** ⏳ Übergangsweg für wx-Plugins.
6. ⏳ **Umschaltung:** Das Qt-Programm wird zum Standard-Build, der
   wx-Build wird eingestellt.
