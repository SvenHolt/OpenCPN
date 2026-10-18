# seachart – OpenSeaMap offline als S-57-Karten für OpenCPN

`seachart` lädt OpenSeaMap-/OpenStreetMap-Daten und Tiefendaten für ein
Revier einmalig herunter, speichert sie offline in einer SQLite-Datei und
erzeugt daraus – ohne Internet – kleine Seekarten entlang einer geplanten
Route.

Die Karten werden als **S-57 ENC-Zellen (`.000`)** geschrieben. Das ist das
Vektorformat, das OpenCPN nativ mit S-52-Symbolik darstellt. Deshalb bleiben
alle Merkmale als echte Seekartenobjekte erhalten:

| OpenSeaMap / OSM                      | S-57 in der Karte                          |
|---------------------------------------|--------------------------------------------|
| Tonnen (lateral, Kardinal, Einzelgefahr, Sonder, Ansteuerung) | `BOYLAT`, `BOYCAR`, `BOYISD`, `BOYSPP`, `BOYSAW` mit Form, Farbe, Muster, Betonnungssystem |
| Baken, Pricken, Dalben                | `BCN*`, `PILPNT`, `MORFAC`                 |
| Feuer inkl. Sektoren, Kennung, Wiederkehr, Gruppe, Tragweite | `LIGHTS` (ein Objekt je Sektor)  |
| Toppzeichen, Nebelsignale, Radarreflektor | `TOPMAR`, `FOGSIG`, `RADRFL`, `RTPBCN`    |
| Häfen, Marinas, Hafeneinrichtungen    | `HRBFAC`, `SMCFAC`, `BERTHS`, `PONTON`     |
| Wracks, Steine, Hindernisse (mit Tiefe) | `WRECKS`, `UWTROC`, `OBSTRN` (`VALSOU`, `WATLEV`) |
| Fahrwasser, Leitlinien, Empf. Kurse, Sperrgebiete, Ankerplätze, Kabel, Pipelines, VTG | `FAIRWY`, `NAVLNE`, `RECTRC`, `RESARE`, `ACHARE`, `CBLSUB`, `PIPSOL`, `TSS*` … |
| Küstenlinie, Seen, Flüsse             | `COALNE`, `LNDARE`                         |
| Tiefendaten (Gitter)                  | `DEPARE` (Tiefenzonen), `DEPCNT` (Tiefenlinien), `SOUNDG` (Lotungen, flachster Wert je Block) |

Damit funktionieren in OpenCPN Sicherheitstiefe/-kontur, Objektabfrage
(Rechtsklick → Objektinfo) und die Darstellungsstufen wie bei offiziellen
ENCs. Die Attribut-Codes kommen aus `data/s57data` dieses Repositories, also
genau dem Katalog, den OpenCPN selbst verwendet.

> **Nicht zur Navigation.** OpenSeaMap-Daten sind freiwillig erfasst, die
> globalen Tiefenmodelle (SRTM15+ ≈ 450 m Raster) sind grob. Die Karten
> dienen Planung und Ergänzung, nicht als Ersatz amtlicher Seekarten.

## Installation

```sh
cd tools/seachart
pip install .            # oder: pip install numpy shapely
pip install osmium       # optional, für .osm.pbf-Import (Geofabrik)
```

Ohne Installation: `PYTHONPATH=tools/seachart python3 -m seachart …`

**Windows:** `tools/seachart/windows/seachart.bat` doppelklicken – richtet
beim ersten Start Python ein und bietet ein Menü. Eine fertige
`seachart.exe` (ohne Python) baut der Workflow `seachart-windows.yml`; sie
ist auch im Windows-Paket des Qt6-Frontends enthalten.

Der S-57-Objektkatalog wird aus `data/s57data` dieses Repositories gelesen.
Läuft das Tool außerhalb des Repositories, den Pfad mit `--s57data` oder der
Umgebungsvariable `SEACHART_S57DATA` angeben (sonst wird ein installiertes
OpenCPN unter `/usr/share/opencpn/s57data` gesucht).

## Ablauf

### 1. Revier offline verfügbar machen (online)

```sh
# Rechteck W,S,E,N – z. B. westliche Ostsee
seachart --db ostsee.db download --bbox 9.8,53.9,13.0,55.6

# oder nur das, was eine Route braucht (Korridor ±10 sm)
seachart --db ostsee.db download törn.gpx --width 10
```

* OpenSeaMap/OSM kommt über die Overpass-API in 0,5°-Kacheln
  (`--overpass-url` für einen eigenen Server, mehrere Spiegel werden
  automatisch probiert). Bereits geladene Kacheln werden übersprungen,
  `--force` lädt neu.
* Tiefen kommen per ERDDAP aus **SRTM15+** (weltweit). Andere Datensätze:
  `--erddap-url`, `--dataset`, `--variable`.

Für große Reviere besser einen Geofabrik-Auszug importieren:

```sh
seachart --db ostsee.db import schleswig-holstein-latest.osm.pbf
```

`import` versteht außerdem `.osm` (JOSM/Overpass-XML), Overpass-`.json`,
ESRI-ASCII-Gitter `.asc` (z. B. **EMODnet Bathymetry**-Kacheln, ≈ 115 m,
deutlich genauer als SRTM15+) und eigene Lotungen als `.xyz`/`.csv`
(`lon lat tiefe`, `--resolution 0.001`). Feinere Gitter haben beim
Kartenbau Vorrang vor gröberen.

`seachart --db ostsee.db info` zeigt, was offline vorliegt.

### 2. Karten entlang der Route bauen (offline)

Route in OpenCPN planen und als GPX exportieren (Routenmanager →
Exportieren), dann:

```sh
seachart --db ostsee.db route törn.gpx --width 8 --out ~/Charts/seachart
```

Pro Route entsteht ein Ordner mit

* **Übersichtskarten** 1:250 000 (`--overview-scale`, 0 = aus),
* **Routenkarten** 1:50 000 (`--scale`) in Kacheln von 0,25°
  (`--cell-size`), jeweils auf den Korridor zugeschnitten (`M_COVR`),
* **Hafenkarten** 1:12 000 um Start- und Zielhafen
  (`--harbour-scale`, `--harbour-radius` in sm, 0 = aus).

Weitere Optionen: `--name` (nur eine Route aus der GPX-Datei),
`--waypoints "54.33,10.15 54.50,10.30"` statt GPX, `--levels 2,5,10,20,50`
(Tiefenlinien in m), `--download` (fehlende Daten vorher nachladen).
Fehlen Daten offline, wird gewarnt.

Für ein festes Gebiet ohne Route:

```sh
seachart --db ostsee.db region --bbox 10.0,54.3,10.5,54.6 --scale 25000 --out charts/kiel
```

### Häfen, Ankerplätze, POIs und Wetter

`download` legt zusätzlich an (abschaltbar mit `--no-pois`, `--no-weather`,
`--no-navgrid`):

* **Häfen und Ankerplätze** aus OpenSeaMap als Ausgangsliste, die du selbst
  pflegst – Einträge, die du bearbeitet hast, werden nie überschrieben:

  ```sh
  seachart harbours list
  seachart harbours add --kind anchorage --name "Bucht" --lat 54.44 --lon 10.36 --notes "Sand, 3 m"
  seachart harbours edit 12 --vhf 69 --depth 2.8 --facilities fuel,water,showers --rating 4
  seachart harbours photo 12 einfahrt.jpg --caption "Einfahrt"
  seachart harbours export haefen.csv     # bearbeiten, dann: harbours import haefen.csv
  ```

  Fotos werden nach `Dokumente/seachart/seachart_media/` kopiert.
* **POIs** im Umkreis der Häfen aus OpenStreetMap (Restaurants,
  Supermärkte, Tankstellen, Bootszubehör, Service, Sehenswertes):
  `seachart pois [--poi-radius 1500]`
* **Wetter** stündlich (Wind, Böen, Welle) von Open-Meteo, offline
  gespeichert – vor dem Ablegen aktualisieren: `seachart weather --days 7`
* **Navigationsgitter** (Land/Wasser/Tiefe) für die Routenberechnung in
  OpenCPN: `seachart navgrid [--nav-resolution 0.002]` (feiner für enge
  Durchfahrten)

Beim Kartenbau entstehen im Kartenordner außerdem `guide.html` (Hafenführer
mit Fotos, POIs und Wettertabelle) und `harbours.gpx` (Häfen und POIs als
Wegpunkte für OpenCPN).

### 3. In OpenCPN einbinden

*Optionen → Karten → Kartendateien → Verzeichnis hinzufügen* und den
Ausgabeordner wählen. Beim ersten Öffnen erzeugt OpenCPN die SENC-Dateien.

## Zusammenspiel mit OpenCPN

Das Tool schreibt `seachart.db` (Häfen, Fotos, POIs, Wetter,
Navigationsgitter) nach `Dokumente/seachart/`. Daneben legen die
Oberflächen `logbook.db` an. Beide OpenCPN-Oberflächen lesen und schreiben
dieselben Dateien:

* **OpenCPN Qt6** (`qt/`): Dock-Bereich „Passage“ mit Planung, Häfen und
  Logbuch im Design der neuen Oberfläche.
* **OpenCPN (wx)**: Werkzeuge → „Passage Planner…“ / „Logbook…“ und das
  Kontextmenü der Karte.

Planung, Datenbankzugriff und Logbuch sind in `libs/seachart` (C++)
gemeinsam implementiert. Hafenführer (`guide.html`) und Logbuch-Export
verwenden die Farben und Karten des Qt6-Designs (Tag/Nacht nach
Systemeinstellung).

Unter Windows enthält das Paket des Qt6-Frontends (Workflow
`qt6-windows.yml`) auch `seachart.exe` mit Startmenü
(„Seekarten und Haefen (seachart).bat“).

## Aufbau

| Modul          | Aufgabe                                                        |
|----------------|----------------------------------------------------------------|
| `sources.py`   | Overpass, ERDDAP, Import von .pbf/.osm/.json/.asc/.xyz          |
| `harbours.py`  | eigene Häfen/Ankerplätze, Fotos, CSV/GPX                        |
| `pois.py`, `weather.py`, `navgrid.py` | POIs (OSM), Wetter (Open-Meteo), Navigationsgitter |
| `guide.py`     | Hafenführer `guide.html` + `harbours.gpx`                       |
| `store.py`     | Offline-Speicher (SQLite + R-Tree)                             |
| `route.py`     | GPX lesen, Korridor, Aufteilung in Kartenzellen                |
| `coast.py`     | Landflächen aus OSM-Küstenlinien (Land links der Linie)        |
| `depth.py`     | Tiefenlinien (Marching Squares), Tiefenzonen, Lotungen         |
| `s57map.py`    | OpenSeaMap-Tags → S-57-Objektklassen und -Attribute            |
| `s57writer.py` | ISO 8211/S-57-Encoder (GDALs S-57-Treiber schreibt keine Geometrie) |
| `builder.py`   | setzt eine Zelle aus allen Teilen zusammen                     |

Tests: `python3 -m unittest discover -s tools/seachart/tests`
(mit installiertem GDAL wird die Ausgabe zusätzlich mit `ogrinfo` gelesen).

## Lizenz der Daten

OpenSeaMap/OpenStreetMap: © OpenStreetMap-Mitwirkende, ODbL. SRTM15+:
Tozer et al. 2019 (frei verfügbar). Bei Weitergabe der Karten die Quellen
nennen.
