# NanoVNA CLI Accessible - Benutzerhandbuch

**Version:** Beta  
**Datum:** Januar 2026  
**Sprache:** Deutsch

---

## Inhaltsverzeichnis

1. [Über NanoVNA CLI Accessible](#1-über-nanovna-cli-accessible)
2. [Systemanforderungen](#2-systemanforderungen)
3. [Installation](#3-installation)
4. [Erste Schritte](#4-erste-schritte)
5. [Hauptmenü-Übersicht](#5-hauptmenü-übersicht)
6. [Grundlegende Bedienung](#6-grundlegende-bedienung)
7. [U-Menü: Analyse-Werkzeuge](#7-u-menü-analyse-werkzeuge)
8. [Akustischer Analysemodus](#8-akustischer-analysemodus)
9. [Export und Import](#9-export-und-import)
10. [Web-Interface](#10-web-interface)
11. [Kalibrierung](#11-kalibrierung)
12. [Fehlerbehebung](#12-fehlerbehebung)
13. [Dateistruktur](#13-dateistruktur)

---

## 1. Über NanoVNA CLI Accessible

NanoVNA CLI Accessible ist eine spezialisierte Windows-Konsolenanwendung zur Bedienung des NanoVNA-H4 Vektor-Netzwerkanalysators. Sie wurde speziell für blinde und sehbehinderte Funkamateure entwickelt.

### Hauptmerkmale:
- **Akustische Darstellung** von HF-Parametern (SWR, Impedanz, Reaktanz, Phase)
- **Screenreader-kompatibel** mit kontextabhängiger Hilfe
- **Umfassende Analyse-Werkzeuge** für Antennen, Kabel und Filter
- **Zweisprachige Oberfläche** (Englisch und Deutsch)
- **Braille-Export** für taktile Grafiken auf Index-Brailledruckern

---

## 2. Systemanforderungen

### Hardware:
- **Betriebssystem:** Windows 7 oder neuer (64-Bit empfohlen)
- **NanoVNA-Gerät:** NanoVNA-H4
- **Verbindung:** USB-Kabel (erscheint als COM-Port)
- **Soundkarte:** Für akustischen Analysemodus
- **Optional:** Index-Brailledrucker für taktile Grafiken

### Software:
- Die Anwendung ist vollständig eigenständig
- Keine zusätzliche Softwareinstallation erforderlich
- Alle Abhängigkeiten sind statisch eingebunden

---

## 3. Installation

Die Beta-Version wird als fertiges Build-Verzeichnis ausgeliefert. **Keine Kompilierung erforderlich.**

### Installationsschritte:

1. **ZIP-Datei entpacken** an einen Ort Ihrer Wahl (z.B. `C:\NanoVNA-CLI`)

2. **Verzeichnisstruktur überprüfen:**
   ```
   nanovna-cli.exe         (Hauptprogramm)
   Languages/              (Sprachdateien)
   config/                 (Konfigurationsdateien)
   bandplans/              (Amateurfunk-Banddefinitionen)
   logs/                   (Protokolldateien werden hier gespeichert)
   Export/                 (Exportierte Daten werden hier gespeichert)
   ```

3. **NanoVNA-H4 anschließen** an einen USB-Port

4. **Fertig!** Sie können die Anwendung jetzt starten.

---

## 4. Erste Schritte

### Anwendung starten:

**Option 1: Einfacher Start**
- Doppelklick auf `nanovna-cli.exe`
- Oder über Kommandozeile: `nanovna-cli.exe`

**Option 2: Mit Debug-Protokollierung (empfohlen für Beta-Tests)**
- Eingabeaufforderung oder PowerShell öffnen
- Zum Anwendungsverzeichnis navigieren
- Ausführen: `nanovna-cli.exe -d`

### Ersteinrichtung:

1. **COM-Port auswählen:**
   - Taste **P** im Hauptmenü drücken
   - Die Anwendung scannt verfügbare COM-Ports
   - Portnummer auswählen, an dem Ihr NanoVNA angeschlossen ist
   - Bei Unsicherheit: Jeden Port ausprobieren - die Geräteerkennung wird angezeigt

2. **Frequenzbereich einstellen:**
   - Taste **R** für Bereichskonfiguration drücken
   - Oder **M** für manuelle Messung
   - Startfrequenz eingeben (in Hz, z.B. `144000000` für 144 MHz)
   - Endfrequenz eingeben (in Hz)
   - Schrittweite eingeben (z.B. `10000` für 10 kHz Schritte)

3. **Erste Messung durchführen:**
   - Taste **M** drücken zum Starten einer Messung
   - Auf Abschluss des Scans warten
   - Die Ergebnisse sind jetzt gespeichert und analysierbar

### Sprachwahl:

- Die Anwendung erkennt automatisch Ihre Windows-Sprache
- Englisch und Deutsch werden unterstützt
- Sprache kann in der Konfigurationsdatei geändert werden

---

## 5. Hauptmenü-Übersicht

Das Hauptmenü bietet Zugriff auf alle Funktionen:

```
Hauptmenü-Befehle:
  U - Komfortfunktionen   Analyse-Werkzeugkasten (siehe Abschnitt 7)
  A - Akustische Analyse  Daten in Töne umwandeln
  R - Bereich             Frequenzscan konfigurieren
  M - Manuell             Einzelmessung durchführen
  Z - Zusammenfassung     Min/Max/Mittelwert-Statistiken anzeigen
  T - Tabelle             Messdaten anzeigen
  E - Export              Messungen in Datei speichern
  L - Laden               Zuvor gespeicherte Messungen laden
  K - Kalibrierung        Kalibrierungsassistent
  P - Port                COM-Port auswählen
  G - Gerät               Batteriespannung und Geräteinformationen
  I - Web-Interface       Web-Interface für Fernzugriff starten/stoppen
  W - Wiederholen         Automatische Messungen umschalten
  C - Spalten             Tabellenanzeige konfigurieren
  O - Optionen            Einstellungen und Konfiguration
  H - Hilfe               Kontextabhängige Hilfe
  Q - Beenden             Anwendung verlassen
```

**Hinweis:** Die deutsche Version verwendet angepasste Tastenkürzel (z.B. **Z** für "Zusammenfassung", **G** für "Gerät"). Dies entspricht der deutschen Terminologie und verbessert die Merkfähigkeit.

---

## 6. Grundlegende Bedienung

### Messungen durchführen:

#### Schnellmessung:
1. Taste **M** drücken (Manuell)
2. Startfrequenz, Endfrequenz und Schrittweite eingeben
3. Auf Abschluss warten
4. Ergebnisse werden automatisch gespeichert

#### Kontinuierlicher Sweep-Modus:
1. Bereich mit **R** oder **M** konfigurieren
2. Taste **W** drücken für kontinuierlichen Sweep
3. Taste **A** drücken für akustischen Modus
4. **LEERTASTE** drücken zum Starten - Messungen werden live aktualisiert

### Ergebnisse anzeigen:

#### Tabellenansicht (**T**):
- Zeigt alle Datenpunkte in paginierter Tabellenform
- **Bild auf/ab** zum Navigieren
- **C** drücken zum Anpassen der angezeigten Spalten
- Spalten: Frequenz, SWR, Rückflussdämpfung, |Z|, R, X, Phase

#### Zusammenfassung (**Z**):
- Zeigt Minimum, Maximum und Durchschnittswerte
- Zeigt beste Frequenz für jeden Parameter
- Schneller Überblick über Messqualität

### Geräteinformationen (**G**):
- Zeigt NanoVNA-Firmware-Version
- Zeigt Batteriespannung
- Nützlich zur Überprüfung der Geräteverbindung

---

## 7. U-Menü: Analyse-Werkzeuge

Taste **U** im Hauptmenü drücken für umfassende Analyse-Werkzeuge. Dieses Menü bietet spezialisierte Tools für verschiedene Messszenarien.

### Antennen- und Impedanzanalyse (S11 erforderlich):

**1. Bandtauglichkeitsprüfung**
- Testet Ihre Antenne über 15 Amateurfunkbänder (160m bis 23cm)
- Zeigt SWR und Impedanz für jedes Band
- Zeigt an, welche Bänder geeignet sind (SWR < Schwellwert)

**2. Resonanzsuche**
- Findet Frequenzen, bei denen SWR minimal ist
- Nützlich zum Finden von Antennenresonanzpunkten
- Zeigt mehrere Resonanzen an, falls vorhanden

**3. SWR-Bandbreite**
- Berechnet Bandbreite, bei der SWR unter 1,5:1, 2:1 oder 3:1 bleibt
- Wichtig zur Bestimmung des nutzbaren Frequenzbereichs
- Gibt Bandbreite in kHz oder MHz an

**4. Fußpunktimpedanz-Analyse**
- Detaillierte Impedanzanalyse bei spezifischer Frequenz
- Zeigt: R (Widerstand), X (Reaktanz), |Z| (Betrag), Phase
- Zeigt an, ob Antenne kapazitiv oder induktiv ist

**5. Anpassungshinweise**
- Schlägt Impedanzanpassungsnetzwerke vor
- Gibt Bauteilwerte an (L und C)
- Hilft, 50-Ohm-Anpassung zu erreichen

**6. Kabellängenschätzung**
- Schätzt Kabellänge aus Phasenantwort
- Benötigt Konfiguration des Verkürzungsfaktors
- Nützlich zur Kabelidentifikation

**7. Kabelfehlererkennung**
- Erkennt Kurzschlüsse, offene Leitungen oder Kabelschäden
- Nutzt Time-Domain-Reflectometry (TDR) Prinzip
- Zeigt ungefähre Entfernung zum Fehler

### Kabel- und Filteranalyse (S21 erforderlich):

**8. Kabeldämpfungsmessung**
- Misst Kabelverlust pro Meter oder pro 100 Fuß
- Benötigt durchkalibrierte S21-Messung
- Vergleich mit Herstellerangaben möglich

**9. Filteranalyse**
- Analysiert Durchlassbereich, Sperrbereich und Welligkeit
- Findet -3 dB Grenzfrequenzen
- Misst Einfügedämpfung

### Hilfsprogramme:

**10. Vorher/Nachher-Vergleich**
- Vergleicht zwei Messungs-Schnappschüsse
- Nützlich für Abstimmanpassungen
- Zeigt Unterschiede in allen Parametern

**11. Automatische Markerplatzierung**
- Platziert Marker automatisch an interessanten Punkten
- Minimum SWR, Null Reaktanz, Maximum S21
- Beschleunigt Analyse

**12. Konfiguration**
- Verkürzungsfaktor für Kabelmessungen einstellen (Standard: 0,66)
- SWR-Schwellwert für Bandtauglichkeit konfigurieren
- Kabelverlustspezifikationen setzen

---

## 8. Akustischer Analysemodus

Wandeln Sie Ihre HF-Messungen in interaktive Mehrkanal-Audio für intuitive Analyse um.

### Akustischen Modus aktivieren:
1. Messung durchführen (**M** oder **R**)
2. Taste **A** drücken für akustischen Modus
3. **LEERTASTE** drücken zum Starten der Wiedergabe

### Audio-Darstellung:

Die Anwendung wandelt 5 Parameter in simultane Audio-Kurven um:

**Zwei verfügbare Audio-Engines:**

#### Synthesizer (Standard):
1. **SWR** - Sinuswelle
2. **Rückflussdämpfung** - Reine Sinuswelle
3. **Impedanz |Z|** - Dreieckswelle
4. **Reaktanz X** - Sägezahn (steigend = induktiv, fallend = kapazitiv)
5. **Phase** - Sinuswelle

#### MIDI-Engine:
1. **SWR** - Streicherensemble
2. **Rückflussdämpfung** - Kirchenorgel
3. **Impedanz |Z|** - Zugriegel-Orgel
4. **Reaktanz X** - Violine
5. **Phase** - Lead 2 (Synthesizer-Lead)

Mit Taste **A** im akustischen Modus zwischen Engines wechseln (öffnet Audio-Konfigurationsbildschirm).

### Wiedergabefunktionen:

**Stereo-Panning:**
- Linker Kanal = Startfrequenz
- Rechter Kanal = Endfrequenz
- Mitte = Mittlere Frequenzen

**Tonhöhenzuordnung:**
- Höhere Tonhöhe = Höherer Parameterwert
- Niedrigere Tonhöhe = Niedrigerer Parameterwert
- Tonhöhenbereich wird automatisch skaliert

**Lautstärkeregelung:**
- Tasten **1-5**: Einzelne Kurven ein-/ausschalten
- **Strg + 1-5**: Kurvenlautstärke verringern
- **Umschalt + 1-5**: Kurvenlautstärke erhöhen
- Lautstärkebereich: 0-200%

### Wiedergabemodi:

**Glatter Modus (Standard):**
- Kontinuierlicher Sweep durch alle Datenpunkte
- Stellt Gesamtkurvenform dar
- Gut für allgemeinen Überblick

**Gepunkteter Modus (Taste T drücken):**
- Spielt diskrete Datenpunkte ab
- Nutzt intelligentes Downsampling (LTTB-Algorithmus)
- Bewahrt Spitzen, Täler und signifikante Merkmale
- Warnung, wenn Zeitfenster zu klein für genaue Darstellung

### Navigation:

**Pfeiltasten:**
- **↑/↓** - Sprungweite ändern (1, 10, 100, 500, 1000 Punkte)
- **←/→** - Nach Sprungweite navigieren
- Echtzeit-Position und Frequenzanzeige

**Wiedergabesteuerung:**
- **LEERTASTE** - Wiedergabe/Pause
- **S** - Stopp und zurück zum Anfang
- **F** - Einfrieren (Pause ohne Positionsänderung)

**Zeitfensteranpassung:**
- **+** - Wiedergabezeit erhöhen (langsamer, mehr Detail)
- **-** - Wiedergabezeit verringern (schneller, weniger Detail)
- Typischer Bereich: 5-60 Sekunden

### Schleifenmarker:

**Schleifenmarker setzen:**
1. Zur gewünschten Startposition navigieren
2. Taste **L** drücken zum Setzen des linken Markers
3. Zur Endposition navigieren
4. Taste **R** drücken zum Setzen des rechten Markers

**Schleifen verwenden:**
- Taste **O** drücken zum Ein-/Ausschalten des Schleifenmodus
- Taste **Z** drücken zum Ein-/Ausschalten des Schleifen-Zooms (zentriert Schleife im Stereofeld)
- Taste **I** drücken zum Invertieren der Schleife (spielt außerhalb der Marker statt innerhalb)
- Wiedergabe wiederholt sich zwischen Markern
- Nützlich für detaillierte Analyse spezifischer Frequenzbereiche
- Taste **C** für kontinuierlichen Wiederholungsmodus

### Erweiterte Funktionen:

**Y-Achsen-Lineal (Taste Y):**
- Spielt aufsteigende Skalenreferenz von Min- bis Max-Wert ab
- Hilft bei der Kalibrierung Ihrer Wahrnehmung der Tonhöhen-Wert-Zuordnung
- Nützlich zum Verständnis der Audio-Darstellung

**X-Achsen-Lineal (Taste X):**
- Schaltet Impulse bei jedem Messpunkt ein/aus
- Hilft bei der Identifizierung der Position im Frequenz-Sweep
- Nützlich zum Zählen von Datenpunkten

**Statuszeile (Taste N):**
- Schaltet detaillierte Statusinformationen während der Wiedergabe ein/aus
- Zeigt Position, Frequenz und Messwerte an
- Aktualisiert sich in Echtzeit während der Wiedergabe

**Gehe-Zu-Menü (Taste G):**
- Sprung zu spezifischer Frequenz oder Position
- Schnelle Navigation zu interessanten Bereichen

**Audio-Konfiguration (Taste A):**
- Zwischen Synthesizer- und MIDI-Engines umschalten
- Frequenzbereich für Synthesizer konfigurieren
- MIDI-Instrumente pro Kurve auswählen
- Klänge vor dem Anwenden vorhören

### Kontinuierlicher Sweep-Modus:
1. Kontinuierlichen Sweep aktivieren (**W** im Hauptmenü)
2. Akustischen Modus aktivieren (**A**)
3. **LEERTASTE** drücken zum Starten
4. Messungen werden live während der Wiedergabe aktualisiert
5. Nützlich für Echtzeit-Abstimmung

### Weitere Funktionen:
- **M** - Aktuelle Messungsinformationen anzeigen
- **E** - Aktuelle Daten exportieren
- **H** - Hilfe anzeigen (zeigt alle verfügbaren Tasten)
- **ESC** - Zurück zum Hauptmenü

### Schnelle Tastenreferenz:

**Wiedergabe:** LEERTASTE (play/pause), F (einfrieren), S (stopp), T (glatt/gepunktet umschalten)  
**Navigation:** Pfeiltasten (←→ bewegen, ↑↓ Sprungweite ändern)  
**Zeit:** +/- (Wiedergabegeschwindigkeit anpassen)  
**Kurven:** 1-5 (umschalten), Strg+1-5 (Lautstärke runter), Umschalt+1-5 (Lautstärke hoch)  
**Schleife:** L (linker Marker), R (rechter Marker), O (umschalten), Z (zoom), I (invertieren), C (kontinuierlich)  
**Werkzeuge:** Y (Y-Lineal), X (X-Lineal), N (Statuszeile), G (gehe zu), A (Audio-Konfig), M (messen), E (exportieren)

---

## 9. Export und Import

### Messungen exportieren:

Taste **E** im Hauptmenü oder akustischen Modus drücken zum Öffnen des Export-Menüs.

**Export-Optionen:**

**1. CSV-Export**
- Standard-CSV-Format mit Semikolon-Trennzeichen
- Enthält: Frequenz, SWR, Rückflussdämpfung, |Z|, R, X, Phase
- Dateiname: `nanovna_JJJJMMTT_HHMMSS_startfreq_endfreq_schritt.csv`
- Ort: `Export/`-Verzeichnis
- Kann in Excel, LibreOffice oder jeder Tabellenkalkulation geöffnet werden

**2. Text-Export**
- Menschenlesbares formatiertes Text
- Enthält alle Messdaten mit Einheiten
- Enthält Messungs-Metadaten (Datum, Zeit, Bereich)
- Dateiname: `nanovna_JJJJMMTT_HHMMSS_startfreq_endfreq_schritt.txt`
- Ort: `Export/`-Verzeichnis

**3. Braille-Grafik-Export (Datei)**
- Exportiert akustische Kurven als taktile Grafiken
- Format: `.brl`-Dateien für Index-Brailledrucker
- 80x25 Braillezellen-Raster (8-Punkt-Braille)
- Kompatibel mit Index Basic, Everest, V3, V4, V5 Druckern
- Einzelne Kurven oder alle Kurven auswählen
- Nutzt ESC G/ESC E Format mit Nibble-Kodierung
- Dateiname: `nanovna_JJJJMMTT_HHMMSS_startfreq_endfreq_schritt.brl`
- Ort: `Export/`-Verzeichnis

**4. Braille-Direktdruck**
- Direkt auf angeschlossenen Index-Brailledrucker drucken
- Keine Zwischendatei erstellt
- Nutzt ESC Z Grafikmodus (vertikale Spaltenkodierung)
- Zu druckende Kurven auswählen
- Anwendung listet alle Windows-Drucker auf
- Index-Brailledrucker aus Liste auswählen
- Erzeugt taktile Grafiken der ausgewählten Kurven
- Optimiert für Index V5-Serie Drucker

**Export mit Schleifenmarkern:**
- Wenn Schleifenmarker gesetzt sind (L und R im akustischen Modus)
- Nur Daten innerhalb des Schleifenbereichs werden exportiert
- Nützlich zum Exportieren spezifischer Frequenzbereiche

### Messungen importieren:

Taste **L** im Hauptmenü drücken zum Laden zuvor gespeicherter Messungen.

**Import-Prozess:**
1. Taste **L** für Laden drücken
2. Datei aus Liste auswählen (zeigt alle Exporte)
3. Daten werden in Speicher geladen
4. Alle Analysefunktionen können nun mit importierten Daten verwendet werden

**Unterstützte Import-Formate:**
- CSV-Dateien (.csv)
- Textdateien (.txt)
- Müssen von dieser Anwendung erstellt sein (korrektes Format)

---

## 10. Web-Interface

Das Web-Interface ermöglicht die Fernsteuerung der NanoVNA CLI-Anwendung über einen Webbrowser im lokalen Netzwerk.

### Zweck und Anwendungsfälle:

- **Fernsteuerung:** Bedienung des Geräts von Smartphone oder Tablet
- **Feldarbeit:** NanoVNA am Antennenmast platzieren, während Sie es aus sicherer Entfernung steuern
- **Barrierefreiheit:** Web-Interface mit jedem Screenreader verwenden, der mit Ihrem Browser funktioniert
- **Multi-Device:** Zugriff von jedem Gerät im lokalen Netzwerk

### Web-Interface starten:

1. Drücken Sie **I** im Hauptmenü
2. Überprüfen Sie die angezeigten Informationen
3. Drücken Sie **S**, um den Webserver zu starten
4. Notieren Sie sich die angezeigten URLs:
   - **Lokaler Zugriff:** `http://localhost:8080` (vom selben Computer)
   - **Netzwerkzugriff:** `http://[Ihre-IP]:8080` (von anderen Geräten in Ihrem Netzwerk)
5. Öffnen Sie die URL in einem beliebigen Webbrowser

### Web-Interface verwenden:

- Das Web-Interface spiegelt das Terminal-Interface wider
- Alle Tastaturbefehle funktionieren gleich
- Screenreader lesen den Inhalt normal vor
- Audioausgabe vom akustischen Modus wird auf dem Server-Computer abgespielt (noch nicht im Browser)

### Web-Interface stoppen:

1. Kehren Sie zum Hauptmenü zurück
2. Drücken Sie erneut **I**
3. Drücken Sie **S**, um den Webserver zu stoppen

### Sicherheitshinweise:

- **Nur HTTP:** Keine Verschlüsselung (nur für lokales Netzwerk vorgesehen)
- **Keine Authentifizierung:** Jeder in Ihrem Netzwerk kann sich verbinden
- **Nur lokales Netzwerk:** Nicht für Internetzugriff konzipiert
- **Firewall:** Windows fragt möglicherweise beim ersten Mal nach Erlaubnis

### Fehlerbehebung:

**Verbindung von anderem Gerät nicht möglich:**
- Überprüfen Sie, ob beide Geräte im selben Netzwerk sind
- Prüfen Sie die Firewall-Einstellungen am Server-Computer
- Verwenden Sie die korrekte IP-Adresse, die im Terminal angezeigt wird

**Port bereits in Verwendung:**
- Eine andere Anwendung könnte Port 8080 verwenden
- Schließen Sie die andere Anwendung oder starten Sie Ihren Computer neu

---

## 11. Kalibrierung

Eine ordnungsgemäße Kalibrierung ist für genaue Messungen unerlässlich.

### Kalibrierungstypen:

**S11-Kalibrierung (Eintor):**
- Erforderlich für: Antennenmessungen, SWR, Impedanz
- Standards: Open (Offen), Short (Kurzschluss), Load (Last, 50 Ohm)

**S21-Kalibrierung (Zweitor):**
- Erforderlich für: Kabelverlust, Filtermessungen
- Standards: Through (Durchgang), Isolation

### Kalibrierungsassistent (Taste K):

**Für S11-Messungen:**
1. Taste **K** im Hauptmenü drücken
2. Frequenzbereich auswählen (muss mit Messbereich übereinstimmen)
3. **Schritt 1:** OPEN-Standard anschließen → messen
4. **Schritt 2:** SHORT-Standard anschließen → messen
5. **Schritt 3:** LOAD (50Ω)-Standard anschließen → messen
6. Kalibrierung ist nun aktiv

**Für S21-Messungen:**
1. Für Zweitor-Messung konfigurieren
2. **Through:** Ports direkt verbinden → messen
3. **Isolation:** Ports unverbunden lassen → messen

### Kalibrierung speichern und abrufen:

**Kalibrierung speichern:**
- Nach Kalibrierungsassistent werden Daten automatisch verwendet
- Kalibrierung kann im internen NanoVNA-Speicher gespeichert werden
- Gerätebefehle im Protokollmodus verwenden

**Kalibrierung abrufen:**
- Kalibrierung geht beim Neustart der Anwendung verloren
- Kalibrierungsassistent für jede Sitzung erneut ausführen
- Oder aus internem NanoVNA-Speicher abrufen

### Kalibrierungstipps:

- Hochwertige Kalibrierstandards verwenden
- Kabellängen konsistent halten
- Alle Verbindungen fest anziehen
- Handkontakt mit Verbindern während der Messung vermeiden
- Bei Änderung des Frequenzbereichs neu kalibrieren
- Kalibrierungsqualität mit bekanntem guten Gerät überprüfen

---

## 12. Fehlerbehebung

### COM-Port-Probleme:

**Problem:** NanoVNA auf COM-Port nicht gefunden
- **Lösung 1:** USB-Verbindung überprüfen
- **Lösung 2:** Verschiedene COM-Ports ausprobieren (**P**-Menü zeigt alle Ports)
- **Lösung 3:** Windows-Geräte-Manager auf COM-Port-Nummer überprüfen
- **Lösung 4:** Überprüfen, ob NanoVNA eingeschaltet ist (Display leuchtet)
- **Lösung 5:** Anderes USB-Kabel versuchen

**Problem:** "Fehler beim Öffnen des seriellen Ports"
- **Lösung:** Andere Anwendungen schließen, die den COM-Port verwenden
- **Lösung:** NanoVNA trennen und wieder anschließen
- **Lösung:** Anwendung neu starten

### Messprobleme:

**Problem:** Seltsame oder unrealistische Werte
- **Lösung 1:** Kalibrierung überprüfen (**K**-Menü ausführen)
- **Lösung 2:** Überprüfen, ob Frequenzbereich angemessen ist
- **Lösung 3:** Verbindungen überprüfen (lockere Kabel)
- **Lösung 4:** Sicherstellen, dass Antenne/Prüfling richtig angeschlossen ist

**Problem:** "Keine Daten verfügbar"
- **Lösung 1:** Zuerst eine Messung durchführen (**M** oder **R**)
- **Lösung 2:** Überprüfen, dass Messung erfolgreich abgeschlossen wurde
- **Lösung 3:** Überprüfen, dass COM-Port ausgewählt ist und Gerät antwortet

### Audio-Probleme:

**Problem:** Kein Ton im akustischen Modus
- **Lösung 1:** Windows-Lautstärkeeinstellungen überprüfen
- **Lösung 2:** Überprüfen, ob Soundkarte funktioniert (Testton in Windows abspielen)
- **Lösung 3:** Kurven ein-/ausschalten (Tasten 1-5)
- **Lösung 4:** Kurvenlautstärken erhöhen (Umschalt+1 bis Umschalt+5)
- **Lösung 5:** Andere Audio-Engine versuchen (Taste **Y**)

**Problem:** Audio verzerrt oder knistert
- **Lösung 1:** Kurvenlautstärken verringern (Strg+1 bis Strg+5)
- **Lösung 2:** Andere Audio-Anwendungen schließen
- **Lösung 3:** Wiedergabezeit anpassen (+ / - Tasten für längere/kürzere Dauer)

### Braille-Druck-Probleme:

**Problem:** "Keine Drucker gefunden"
- **Lösung:** Mindestens einen Drucker in Windows installieren
- **Lösung:** Windows Geräte und Drucker überprüfen

**Problem:** "Fehler beim Öffnen des Druckers"
- **Lösung 1:** Überprüfen, ob Drucker online und bereit ist
- **Lösung 2:** Überprüfen, ob Druckername genau übereinstimmt
- **Lösung 3:** Windows-Testseite zuerst drucken versuchen
- **Lösung 4:** Druckerberechtigungen in Windows überprüfen

**Problem:** Druckauftrag erfolgreich, aber nichts druckt
- **Lösung 1:** Druckerwarteschlange überprüfen (Geräte und Drucker)
- **Lösung 2:** Überprüfen, ob Papier eingelegt ist
- **Lösung 3:** Druckerspezifische Fehleranzeigen überprüfen

**Problem:** Verstümmelte Ausgabe auf Brailledrucker
- **Lösung 1:** Dateiexport (Option 3) statt Direktdruck versuchen
- **Lösung 2:** Druckermodellkompatibilität überprüfen (Index-Drucker)
- **Lösung 3:** Debug-Protokoll auf Fehler überprüfen

### Anwendungsprobleme:

**Problem:** Anwendung stürzt ab oder friert ein
- **Lösung 1:** Mit Debug-Protokollierung ausführen: `nanovna-cli.exe -d`
- **Lösung 2:** `logs/`-Verzeichnis auf Fehlermeldungen überprüfen
- **Lösung 3:** Problem mit Debug-Protokoll melden

**Problem:** Screenreader-Kompatibilitätsprobleme
- **Lösung:** Anwendung verwendet Standard-Konsolenausgabe
- **Lösung:** Die meisten Screenreader funktionieren gut mit Konsolenanwendungen
- **Lösung:** Kontextabhängige Hilfe verwenden (Taste **H**)

### Hilfe erhalten:

Wenn Probleme bestehen bleiben:
1. Debug-Protokollierung aktivieren: `nanovna-cli.exe -d`
2. Problem reproduzieren
3. Protokolldateien im `logs/`-Verzeichnis überprüfen:
   - `debug_JJJJMMTT_HHMMSS.txt` (allgemeines Protokoll)
   - `debug_comm_JJJJMMTT_HHMMSS.txt` (serielle Kommunikation)
4. Problem mit Protokolldateien melden (siehe Beta-Test-Anleitung)

---

## 13. Dateistruktur

Das Verständnis der Dateistruktur hilft bei Fehlerbehebung und Organisation.

### Verzeichnisstruktur:

```
nanovna-cli.exe                Hauptprogramm
│
├── Languages/                 Sprachdateien
│   ├── eng.lng               Englische Übersetzung
│   └── deu.lng               Deutsche Übersetzung
│
├── config/                    Konfigurationsdateien
│   ├── app_settings.cfg      Anwendungseinstellungen (automatisch gespeichert)
│   ├── command_templates.cfg NanoVNA-Befehlsvorlagen
│   └── cables.cfg            Kabelspezifikationen
│
├── bandplans/                 Amateurfunk-Banddefinitionen
│   ├── usa.ini               US-Amateurfunkbänder
│   └── deu.ini               Deutsche Amateurfunkbänder
│
├── logs/                      Protokolldateien (automatisch generiert)
│   ├── debug_*.txt           Allgemeine Debug-Protokolle
│   └── debug_comm_*.txt      Serielle Kommunikationsprotokolle
│
└── Export/                    Exportierte Messungen
    ├── *.csv                 CSV-Exporte
    ├── *.txt                 Text-Exporte
    └── *.brl                 Braille-Grafik-Exporte
```

### Konfigurationsdateien:

**app_settings.cfg**
- Automatisch beim Beenden gespeichert
- Enthält zuletzt verwendete Einstellungen:
  - Sprachpräferenz
  - Letzter COM-Port
  - Letzter Frequenzbereich
  - Verkürzungsfaktor
  - SWR-Schwellwerte
  - Audio-Einstellungen
- Kann manuell bearbeitet werden (Textformat)
- Löschen, um auf Standardeinstellungen zurückzusetzen

**command_templates.cfg**
- NanoVNA-Seriellbefehle
- Sollte keine Änderung benötigen
- Format: `BEFEHL=befehlszeichenfolge`

**cables.cfg**
- Kabelspezifikationen für Verlustberechnungen
- Format: `KABELNAME=verlust_pro_meter,verkürzungsfaktor`
- Benutzerdefinierte Kabel können hinzugefügt werden

### Protokolldateien:

**debug_JJJJMMTT_HHMMSS.txt**
- Allgemeines Anwendungsprotokoll
- Enthält alle Operationen und Fehler
- Nützlich für Fehlerbehebung
- Wird erstellt, wenn `-d` Flag verwendet wird

**debug_comm_JJJJMMTT_HHMMSS.txt**
- Serielle Kommunikationsprotokolle
- Zeigt alle an NanoVNA gesendeten Befehle
- Zeigt alle empfangenen Antworten
- Nützlich bei Gerätekommunikationsproblemen

### Export-Dateien:

Alle Exporte verwenden Dateinamenformat:
`nanovna_JJJJMMTT_HHMMSS_startfreq_endfreq_schritt.ext`

Beispiel:
`nanovna_20260125_143522_144000000_146000000_10000.csv`
- Datum: 25.01.2026
- Zeit: 14:35:22
- Start: 144,000 MHz
- Ende: 146,000 MHz
- Schritt: 10 kHz

---

## Kurzreferenzkarte

### Wesentliche Befehle:
```
P - COM-Port auswählen
M - Messen
A - Akustischer Modus
U - Analyse-Werkzeuge
E - Export
H - Hilfe
Q - Beenden
```

### Akustischer Modus:
```
LEERTASTE - Wiedergabe/Pause
←/→       - Navigieren
1-5       - Kurven umschalten
+/-       - Geschwindigkeit anpassen
L/R       - Schleifenmarker setzen
B         - Zurück zum Menü
```

### U-Menü Analyse:
```
1 - Bandtauglichkeit
2 - Resonanzsuche
3 - SWR-Bandbreite
4 - Fußpunktimpedanz
5 - Anpassungshinweise
```

---

## Über Screenreader-Zugänglichkeit

NanoVNA CLI Accessible wurde von Grund auf für Zugänglichkeit entwickelt:

- **Standard-Konsolenausgabe:** Kompatibel mit allen Screenreadern
- **Klare, beschreibende Meldungen:** Keine kryptischen Codes oder Symbole
- **Kontextabhängige Hilfe:** Taste **H** in jedem Menü
- **Logische Navigation:** Konsistente Tastenkombinationen
- **Audio-Darstellung:** HF-Kurven durch Töne "sehen"
- **Braille-Ausgabe:** Taktile Grafiken für detaillierte Analyse

---

## Support erhalten

Für Fragen, Fehlerberichte oder Rückmeldungen während des Beta-Tests, siehe die Beta-Test-Anleitung, die mit dieser Distribution enthalten ist.

**73 DE DO9RE**

---

*NanoVNA CLI Accessible - HF-Messungen für alle zugänglich machen.*
