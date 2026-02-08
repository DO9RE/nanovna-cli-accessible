# Beta-Test-Anleitung - NanoVNA CLI Accessible

**Version:** Beta  
**Datum:** Januar 2026  
**Sprache:** Deutsch

---

## Inhaltsverzeichnis

1. [Über das Beta-Testing](#1-über-das-beta-testing)
2. [Was soll getestet werden](#2-was-soll-getestet-werden)
3. [Debug-Ausgabe aktivieren](#3-debug-ausgabe-aktivieren)
4. [Debug-Protokolle verstehen](#4-debug-protokolle-verstehen)
5. [Rückmeldungen notieren](#5-rückmeldungen-notieren)
6. [Test-Szenarien](#6-test-szenarien)
7. [Bekannte Probleme und Einschränkungen](#7-bekannte-probleme-und-einschränkungen)
8. [Kontaktinformationen](#8-kontaktinformationen)

---

## 1. Über das Beta-Testing

Vielen Dank für Ihre Teilnahme am Beta-Test von NanoVNA CLI Accessible! Ihr Feedback ist unschätzbar wertvoll zur Verbesserung dieser Anwendung.

### Ziele des Beta-Tests:
- Funktionalität auf verschiedenen Windows-Versionen überprüfen
- Kompatibilität mit verschiedenen Screenreadern testen
- Fehler und Benutzbarkeitsprobleme identifizieren
- Akustische Analyse- und Braille-Export-Funktionen validieren
- Feedback zur Benutzeroberfläche und Dokumentation sammeln

### Was Sie erhalten haben:
- **Einsatzbereites Build** (keine Kompilierung nötig)
- **Benutzerhandbuch** (erklärt die Verwendung der Anwendung)
- **Diese Beta-Test-Anleitung** (erklärt was und wie zu testen ist)

### Zeitaufwand:
- **Minimum:** 2-3 Stunden Testzeit
- **Empfohlen:** 5-10 Stunden für umfassende Tests
- Testen Sie in Ihrem eigenen Tempo über mehrere Tage

---

## 2. Was soll getestet werden

### Priorität 1: Kernfunktionalität (Wesentlich)

**Gerätekommunikation:**
- [ ] COM-Port-Erkennung und Auswahl
- [ ] Geräteverbindung und -identifikation
- [ ] Batteriespannungsanzeige
- [ ] Befehlsausführung und Antwort

**Basismessungen:**
- [ ] Einzelner Frequenzdurchlauf (S11-Parameter)
- [ ] Verschiedene Frequenzbereiche (KW, VHF, UHF)
- [ ] Verschiedene Schrittweiten (klein und groß)
- [ ] Genauigkeit der Messdaten
- [ ] Tabellenanzeige der Daten

**Benutzeroberfläche:**
- [ ] Navigation im Hauptmenü
- [ ] Hilfesystem (kontextabhängig)
- [ ] Sprachwechsel (Englisch/Deutsch)
- [ ] Klarheit der Fehlermeldungen
- [ ] Screenreader-Kompatibilität

### Priorität 2: Analyse-Funktionen (Wichtig)

**U-Menü Analyse-Werkzeuge:**
- [ ] Bandtauglichkeitsprüfung
- [ ] Resonanzsuche
- [ ] SWR-Bandbreitenberechnung
- [ ] Fußpunktimpedanz-Analyse
- [ ] Anpassungshinweise
- [ ] Kabellängenschätzung
- [ ] Kabelfehlererkennung
- [ ] Filteranalyse (falls S21 verfügbar)

**Zusammenfassung und Statistik:**
- [ ] Minimum/Maximum-Werterkennung
- [ ] Durchschnittsberechnungen
- [ ] Frequenz der besten Werte

### Priorität 3: Akustische Analyse (Wichtig)

**Basis-Audio-Wiedergabe:**
- [ ] Eintritt in akustischen Modus
- [ ] Wiedergabe/Pause/Stopp-Steuerung
- [ ] Audio-Engine-Auswahl (Synthesizer/MIDI)
- [ ] Einzelne Kurven umschalten (Tasten 1-5)
- [ ] Lautstärkeregelung (Umschalt/Strg + 1-5)
- [ ] Audio-Klarheit und -Qualität

**Erweiterte Audio-Funktionen:**
- [ ] Glatter vs. gepunkteter Modus
- [ ] Navigation mit Pfeiltasten
- [ ] Sprungweitenanpassung
- [ ] Wiedergabezeitanpassung (+/- Tasten)
- [ ] Schleifenmarker (L/R Tasten)
- [ ] Schleifenmodus-Funktionalität
- [ ] Kontinuierlicher Sweep mit Live-Updates

**Audio-Qualität:**
- [ ] Stereo-Panning (links = Start, rechts = Ende)
- [ ] Tonhöhe repräsentiert Wert korrekt
- [ ] Keine Audio-Artefakte oder Verzerrungen
- [ ] MIDI-Instrumentenauswahl und -Vorschau

### Priorität 4: Datenverwaltung (Mäßig)

**Export-Funktionen:**
- [ ] CSV-Export-Funktionalität
- [ ] Text-Export-Funktionalität
- [ ] Dateinamenskonvention
- [ ] Export mit Schleifenmarkern
- [ ] Datenintegrität in exportierten Dateien

**Import-Funktionen:**
- [ ] Laden zuvor gespeicherter Messungen
- [ ] Dateiauswahl aus Liste
- [ ] Datenintegrität nach Import
- [ ] Analyse mit importierten Daten

**Braille-Export:**
- [ ] Braille-Dateiexport (.brl Format)
- [ ] Direktdruck auf Index-Brailledrucker (falls verfügbar)
- [ ] Druckererkennung und -auswahl
- [ ] Kurvenauswahl für Export/Druck
- [ ] Ausgabequalität (falls Brailledrucker verfügbar)

**Web-Interface:**
- [ ] Web-Interface starten (I-Taste, dann S)
- [ ] Zugriff von localhost (gleicher Computer)
- [ ] Zugriff von anderem Gerät im Netzwerk
- [ ] Alle Menüfunktionen funktionieren über Browser
- [ ] Screenreader-Kompatibilität im Browser
- [ ] Web-Interface stoppen
- [ ] Behandlung von Port-Konflikten
- [ ] Verbindungsstabilität

### Priorität 5: Erweiterte Funktionen (Optional)

**Kalibrierung:**
- [ ] Kalibrierungsassistenten-Ablauf
- [ ] Open/Short/Load-Kalibrierung
- [ ] Kalibrierungseffekt auf Messungen
- [ ] Dauerhaftigkeit der Kalibrierungsdaten

**Kontinuierlicher Sweep:**
- [ ] Aktivieren/Deaktivieren kontinuierlicher Sweep
- [ ] Live-Messungsaktualisierungen
- [ ] Leistung mit akustischem Modus
- [ ] Stoppen des kontinuierlichen Sweeps

**Tabellenanpassung:**
- [ ] Spaltenauswahl
- [ ] Paginierung
- [ ] Datenformatierung

---

## 3. Debug-Ausgabe aktivieren

Debug-Ausgabe ist **essentiell für Beta-Tests**. Sie hilft bei der Identifikation und Behebung von Fehlern.

### Debug-Modus aktivieren:

**Methode 1: Kommandozeile (Empfohlen)**
1. Eingabeaufforderung oder PowerShell öffnen
2. Zum Anwendungsverzeichnis navigieren:
   ```
   cd C:\Pfad\Zu\NanoVNA-CLI
   ```
3. Mit Debug-Flag ausführen:
   ```
   nanovna-cli.exe -d
   ```

**Methode 2: Verknüpfung erstellen**
1. Rechtsklick auf `nanovna-cli.exe`
2. "Verknüpfung erstellen" auswählen
3. Rechtsklick auf Verknüpfung → Eigenschaften
4. Im "Ziel"-Feld `-d` am Ende hinzufügen:
   ```
   "C:\Pfad\Zu\nanovna-cli.exe" -d
   ```
5. OK klicken
6. Diese Verknüpfung zum Testen verwenden

**Methode 3: Batch-Datei**
Datei namens `start-debug.bat` im Anwendungsverzeichnis erstellen:
```batch
@echo off
nanovna-cli.exe -d
pause
```
Diese Datei doppelklicken zum Starten mit Debug-Protokollierung.

### Was der Debug-Modus macht:

- Erstellt detaillierte Protokolldateien im `logs/`-Verzeichnis
- Zeichnet alle Operationen, Befehle und Antworten auf
- Enthält Zeitstempel für jedes Ereignis
- Protokolliert Fehler mit detaillierten Informationen
- Verlangsamt die Anwendung NICHT signifikant
- Beeinflusst die Funktionalität NICHT

**Wichtig:** Verwenden Sie während des Beta-Tests immer den Debug-Modus!

---

## 4. Debug-Protokolle verstehen

Debug-Protokolle werden im `logs/`-Verzeichnis mit zeitgestempelten Dateinamen gespeichert.

### Protokolldateitypen:

**1. debug_JJJJMMTT_HHMMSS.txt**
- **Allgemeines Anwendungsprotokoll**
- Enthält: Menüaktionen, Funktionsaufrufe, Fehler, Warnungen
- Verwenden für: Allgemeine Fehlersuche, Programmablauf verstehen

**2. debug_comm_JJJJMMTT_HHMMSS.txt**
- **Serielle Kommunikationsprotokolle**
- Enthält: Alle an NanoVNA gesendeten Befehle, alle empfangenen Antworten
- Verwenden für: Gerätekommunikationsprobleme, Messprobleme

### Protokolle lesen:

**Protokolleintrag-Format:**
```
[ZEITSTEMPEL] [MODUL] Nachrichtentext
```

Beispiel:
```
[2026-01-25 14:35:22] [SERIAL] Öffne Port COM4
[2026-01-25 14:35:22] [SERIAL] Port erfolgreich geöffnet
[2026-01-25 14:35:23] [PROTOCOL] Sende Befehl: info
[2026-01-25 14:35:23] [PROTOCOL] Antwort: NanoVNA-H4 v1.0.70
```

**Wichtige Protokoll-Module:**

- **[SERIAL]** - Serielle Port-Operationen
- **[PROTOCOL]** - NanoVNA Befehle/Antworten
- **[AUDIO]** - Audio-Synthese und -Wiedergabe
- **[BRAILLE_PRINTER]** - Braille-Export und -Druck
- **[EXPORT]** - Dateiexport-Operationen
- **[IMPORT]** - Dateiimport-Operationen
- **[U_MENU]** - Analyse-Toolkit-Funktionen
- **[ERROR]** - Fehlermeldungen
- **[WARNING]** - Warnungen

### Häufige Protokollmuster:

**Erfolgreiche Operation:**
```
[SERIAL] Öffne Port COM4
[SERIAL] Port erfolgreich geöffnet
[PROTOCOL] Gerät identifiziert: NanoVNA-H4
```

**Fehlermuster:**
```
[ERROR] Fehler beim Öffnen von Port COM4: Zugriff verweigert
[WARNING] Bitte andere Anwendungen schließen, die diesen Port verwenden
```

**Messprozess:**
```
[PROTOCOL] Starte Scan von 144000000 bis 146000000
[PROTOCOL] Schrittweite: 10000 Hz
[PROTOCOL] Sammle Datenpunkt 1/200
[PROTOCOL] Sammle Datenpunkt 2/200
...
[PROTOCOL] Scan abgeschlossen. 200 Punkte gesammelt.
```

### Worauf zu achten ist:

1. **Fehlermeldungen** - Gekennzeichnet durch [ERROR] Tag
2. **Warnmeldungen** - Gekennzeichnet durch [WARNING] Tag
3. **Ungewöhnliche Muster** - Wiederholte Fehler, Timeouts
4. **Fehlgeschlagene Operationen** - "Fehler bei...", "Kann nicht...", "Error:"
5. **Unerwartete Werte** - Werte, die keinen Sinn ergeben

---

## 5. Rückmeldungen notieren

Ihr Feedback ist entscheidend! So melden Sie verschiedene Arten von Problemen.

### Fehlerberichts-Vorlage:

```
FEHLERBERICHT

Titel: [Kurze Beschreibung des Fehlers]

Beschreibung:
[Detaillierte Beschreibung, was schiefgelaufen ist]

Schritte zur Reproduktion:
1. [Erster Schritt]
2. [Zweiter Schritt]
3. [Dritter Schritt]

Erwartetes Verhalten:
[Was hätte passieren sollen]

Tatsächliches Verhalten:
[Was tatsächlich passiert ist]

Systeminformationen:
- Windows-Version: [z.B., Windows 10 64-Bit]
- Screenreader: [z.B., NVDA 2023.3, JAWS 2024, Keiner]
- NanoVNA-Modell: [z.B., NanoVNA-H4]
- Firmware-Version: [aus Geräteinformations-Menü]

Debug-Protokoll:
[Relevante Abschnitte aus Debug-Protokoll anhängen oder einfügen]
[Einschließen: logs/debug_JJJJMMTT_HHMMSS.txt]

Zusätzliche Hinweise:
[Weitere relevante Informationen]
```

### Funktions-Feedback-Vorlage:

```
FUNKTIONS-FEEDBACK

Funktionsname: [z.B., Akustischer Analysemodus]

Bewertung: [1-5, wobei 5 = ausgezeichnet, 1 = schlecht]

Was gut funktioniert:
- [Positiver Punkt 1]
- [Positiver Punkt 2]

Was verbessert werden könnte:
- [Verbesserung 1]
- [Verbesserung 2]

Vorschläge:
[Spezifische Verbesserungsvorschläge]
```

### Benutzerfreundlichkeits-Feedback-Vorlage:

```
BENUTZERFREUNDLICHKEITS-FEEDBACK

Bereich: [z.B., Hauptmenü, U-Menü, Akustischer Modus]

Benutzerfreundlichkeit: [1-5, wobei 5 = sehr einfach, 1 = sehr schwierig]

Klarheit: [1-5, wobei 5 = sehr klar, 1 = sehr verwirrend]

Kommentare:
[Detailliertes Feedback zur Benutzerfreundlichkeit]

Screenreader-Hinweise:
[Spezifisches Feedback zur Screenreader-Erfahrung]
```

### Dokumentations-Feedback-Vorlage:

```
DOKUMENTATIONS-FEEDBACK

Dokument: [Benutzerhandbuch / Beta-Test-Anleitung]

Abschnitt: [Welcher Abschnitt]

Problem:
[Was unklar, fehlend oder falsch ist]

Vorschlag:
[Wie es verbessert werden kann]
```

### Was einzuschließen ist:

**Immer einschließen:**
1. Ihre Windows-Version
2. Verwendeter Screenreader (falls vorhanden)
3. NanoVNA-Modell und Firmware-Version
4. Schritte zur Reproduktion des Problems
5. Relevante Debug-Protokoll-Abschnitte

**Bei Relevanz einschließen:**
1. Screenshots (bei visuellen Problemen)
2. Exportierte Datendateien (bei Datenproblemen)
3. Audio-Aufnahmen (bei Audio-Problemen)
4. Vollständige Debug-Protokolldatei (bei Abstürzen oder schwerwiegenden Fehlern)

### Wohin Feedback senden:

[Kontaktinformationen werden separat von DO9RE bereitgestellt]

**Dateiorganisation:**
- Ordner mit Ihrem Namen und Datum erstellen
- Debug-Protokolle, Screenshots, exportierte Dateien einschließen
- Komprimieren (ZIP) vor dem Senden, falls groß

---

## 6. Test-Szenarien

Hier sind spezifische Test-Szenarien zum Durcharbeiten. Versuchen Sie, so viele wie möglich abzuschließen.

### Szenario 1: Erstbenutzer-Setup (15 Minuten)

**Ziel:** Die Ersteinrichtungs-Erfahrung überprüfen.

1. Beta-Distribution in einen neuen Ordner entpacken
2. Anwendung mit Debug-Protokollierung starten
3. **Test:** COM-Port-Auswahl
   - Findet die Anwendung Ihren NanoVNA?
   - Wird das Gerät korrekt identifiziert?
   - Ist der Prozess klar und intuitiv?
4. **Test:** Erste Messung
   - Frequenzbereich konfigurieren (z.B., 144-146 MHz)
   - Messung durchführen
   - Ergebnisse in Tabelle anzeigen
5. **Test:** Hilfesystem
   - H im Hauptmenü drücken
   - H im akustischen Modus drücken
   - Ist die Hilfe klar und nützlich?

**Melden:** Ersteinrichtungs-Erfahrung, Verwirrungen oder Schwierigkeiten.

### Szenario 2: Antennenanalyse (30 Minuten)

**Ziel:** Antennenanalyse-Funktionen testen.

**Voraussetzungen:** NanoVNA angeschlossen, Antenne oder Dummy-Last angeschlossen.

1. **S11-Messung durchführen** an einer Antenne (beliebiges Band)
2. **Test U-Menü → Bandtauglichkeitsprüfung (Option 1)**
   - Identifiziert es korrekt geeignete Bänder?
   - Sind SWR-Werte vernünftig?
3. **Test U-Menü → Resonanzsuche (Option 2)**
   - Findet es die Resonanzpunkt(e)?
   - Sind Frequenzen genau?
4. **Test U-Menü → SWR-Bandbreite (Option 3)**
   - 2:1 Bandbreite berechnen
   - Sind Werte vernünftig?
5. **Test U-Menü → Fußpunktimpedanz (Option 4)**
   - Impedanz bei Resonanz prüfen
   - Werden R, X, |Z| und Phase korrekt angezeigt?
6. **Test U-Menü → Anpassungshinweise (Option 5)**
   - Für nicht-resonante Frequenz
   - Sind Vorschläge vernünftig?

**Melden:** Genauigkeit der Analyse, Nützlichkeit der Ergebnisse, Fehler.

### Szenario 3: Akustische Analyse - Basis (20 Minuten)

**Ziel:** Basis-Funktionalität der akustischen Analyse testen.

**Voraussetzungen:** Funktionierende Soundkarte, Messdaten verfügbar.

1. **Messung durchführen** mit klaren Merkmalen (z.B., Antenne mit Resonanz)
2. **Akustischen Modus aktivieren** (Taste A drücken)
3. **Basis-Wiedergabe testen:**
   - LEERTASTE zum Abspielen drücken
   - LEERTASTE zum Pausieren drücken
   - S zum Stoppen drücken
   - F zum Einfrieren drücken
4. **Kurven umschalten testen:**
   - 1-5 drücken zum Ein-/Ausschalten von Kurven
   - Können Sie den Unterschied hören?
5. **Lautstärkeregelung testen:**
   - Umschalt+1 zum Erhöhen der SWR-Lautstärke
   - Strg+1 zum Verringern der SWR-Lautstärke
   - Andere Kurven ausprobieren
6. **Audio-Engine-Wechsel testen:**
   - Y zum Öffnen der Audio-Konfiguration drücken
   - Zu MIDI-Engine wechseln
   - Verschiedene Instrumente ausprobieren
7. **Auf Stereo-Panning hören:**
   - Repräsentiert das linke Ohr die Startfrequenz?
   - Repräsentiert das rechte Ohr die Endfrequenz?

**Melden:** Audio-Qualität, Klarheit der Kurven, Benutzerfreundlichkeit, Fehler.

### Szenario 4: Akustische Analyse - Erweitert (20 Minuten)

**Ziel:** Erweiterte akustische Funktionen testen.

**Voraussetzungen:** Messdaten verfügbar.

1. **Glatter vs. gepunkteter Modus testen:**
   - Im akustischen Modus T zum Umschalten drücken
   - Unterschied bemerken
   - Welcher ist nützlicher?
2. **Navigation testen:**
   - ↑/↓ zum Ändern der Sprungweite verwenden
   - ←/→ zum Navigieren verwenden
   - Wird Position korrekt aktualisiert?
3. **Zeitanpassung testen:**
   - + zum Verlangsamen der Wiedergabe drücken
   - - zum Beschleunigen der Wiedergabe drücken
   - Angenehme Geschwindigkeit finden
4. **Schleifenmarker testen:**
   - Zu einem interessanten Bereich navigieren
   - L zum Setzen des linken Markers drücken
   - Zum Ende des Bereichs navigieren
   - R zum Setzen des rechten Markers drücken
   - O zum Aktivieren der Schleife drücken
   - Läuft sie korrekt in Schleife?
5. **Kontinuierliche Wiederholung testen:**
   - C für kontinuierliche Wiederholung drücken
   - Spielt sie wiederholt ab?

**Melden:** Nützlichkeit der Funktionen, Fehler, Vorschläge.

### Szenario 5: Kontinuierlicher Sweep-Modus (15 Minuten)

**Ziel:** Live-Messungsaktualisierungen während Wiedergabe testen.

**Voraussetzungen:** Antenne oder einstellbare Last.

1. **Frequenzbereich konfigurieren** (z.B., um Antennenresonanz herum)
2. **Kontinuierlichen Sweep aktivieren** (W im Hauptmenü drücken)
3. **Akustischen Modus aktivieren** (A drücken)
4. **Wiedergabe starten** (LEERTASTE drücken)
5. **Antenne oder Last anpassen** während des Abspielens
   - Können Sie Änderungen in Echtzeit hören?
   - Ist die Aktualisierungsrate akzeptabel?
6. **Stoppen des kontinuierlichen Sweeps testen:**
   - Zum Hauptmenü zurückkehren
   - W zum Deaktivieren drücken
   - Stoppt es korrekt?

**Melden:** Leistung, Nützlichkeit, Verzögerungen oder Probleme.

### Szenario 6: Export und Import (15 Minuten)

**Ziel:** Datenexport- und -import-Funktionalität testen.

**Voraussetzungen:** Messdaten verfügbar.

1. **CSV-Export testen:**
   - E im Hauptmenü drücken
   - Option 1 (CSV) auswählen
   - Datei im Export/-Verzeichnis finden
   - In Tabellenkalkulationsanwendung öffnen
   - Datenintegrität überprüfen
2. **Text-Export testen:**
   - E im Hauptmenü drücken
   - Option 2 (Text) auswählen
   - In Texteditor öffnen
   - Ist Format lesbar?
3. **Import testen:**
   - L im Hauptmenü drücken
   - Zuvor exportierte Datei auswählen
   - Überprüfen, dass Daten korrekt geladen wurden
   - Analyse mit importierten Daten versuchen
4. **Export mit Schleifenmarkern testen:**
   - Im akustischen Modus Schleifenmarker setzen (L und R)
   - Daten exportieren
   - Überprüfen, dass nur Schleifenbereich exportiert wird

**Melden:** Dateiformat-Qualität, Import-Erfolg, Probleme.

### Szenario 7: Braille-Export (15-30 Minuten)

**Ziel:** Braille-Dateiexport und Direktdruck testen.

**Voraussetzungen:** Messdaten verfügbar. Index-Brailledrucker für Direktdruck-Test.

**Teil A: Dateiexport (alle Tester)**
1. **Akustischen Modus aktivieren** (A drücken)
2. **E für Export drücken**
3. **Option 3 auswählen** (Braille-Dateiexport)
4. **Kurven auswählen** (z.B., "1 2" für SWR und Rückflussdämpfung drücken, oder "a" für alle)
5. **Export/-Verzeichnis überprüfen** auf .brl-Datei
6. **Melden:** War der Prozess klar? Fehler?

**Teil B: Direktdruck (nur falls Index-Brailledrucker verfügbar)**
1. **Überprüfen, dass Drucker installiert** ist in Windows und online
2. **Akustischen Modus aktivieren** (A drücken)
3. **E für Export drücken**
4. **Option 4 auswählen** (Direktdruck)
5. **Kurven auswählen** zum Drucken
6. **Drucker auswählen** aus Liste
7. **Auf Druckabschluss warten**
8. **Ausgabe untersuchen:** Sind Kurven taktil erkennbar?
9. **Melden:** Druckqualität, Probleme, getestetes Druckermodell

**Teil C: Fehlerbehandlung (alle Tester)**
1. **Abbrechen versuchen** während Kurvenauswahl (Enter ohne Auswahl drücken)
2. **Ungültige Eingaben versuchen** während Druckerauswahl
3. **Melden:** Sind Fehlermeldungen klar?

**Debug-Protokoll überprüfen** auf Braille-bezogene Einträge:
- `[BRAILLE_PRINTER]` Einträge
- Druckererkennung
- Datengenerierung
- Druckauftragsstatus

### Szenario 8: Screenreader-Kompatibilität (30 Minuten)

**Ziel:** Kompatibilität mit Ihrem Screenreader testen.

**Voraussetzungen:** Screenreader läuft (NVDA, JAWS oder andere).

1. **Alle Menüs testen:**
   - Hauptmenü-Navigation
   - U-Menü-Navigation
   - Akustischer Modus
   - Export/Import-Dialoge
   - Kalibrierungsassistent
2. **Eingabeaufforderungen und Meldungen testen:**
   - Sind Eingabeaufforderungen klar?
   - Werden Fehlermeldungen korrekt gesprochen?
   - Sind Statusinformationen zugänglich?
3. **Datenpräsentation testen:**
   - Lesbarkeit der Tabellenansicht
   - Zusammenfassungsstatistiken
   - Messergebnisse
4. **Hilfesystem testen:**
   - Kontextabhängige Hilfe (H-Taste)
   - Sind Hilfeinformationen klar?
5. **Spezifische Probleme notieren:**
   - Nicht gesprochene Informationen
   - Verwirrende Formulierungen
   - Fehlende Beschriftungen

**Melden:** Screenreader-Name/-Version, was gut funktioniert, was verbessert werden muss.

### Szenario 9: Kalibrierung (20 Minuten)

**Ziel:** Kalibrierungsfunktionalität testen.

**Voraussetzungen:** Kalibrierungsstandards (Open, Short, Load).

1. **Kalibrierungsassistenten starten** (K drücken)
2. **Eingabeaufforderungen folgen:**
   - Frequenzbereich einstellen
   - OPEN-Standard anschließen → messen
   - SHORT-Standard anschließen → messen
   - LOAD-Standard anschließen → messen
3. **Messung durchführen** nach Kalibrierung
4. **Mit unkalibrierter Messung vergleichen**
   - Gibt es eine merkbare Verbesserung?
   - Sind Werte realistischer?
5. **Abrufen der Kalibrierung testen:**
   - Anwendung neu starten
   - Ist Kalibrierung verloren? (erwartet)
   - Kalibrierung erneut ausführen
   - Funktioniert es konsistent?

**Melden:** Klarheit des Kalibrierungsprozesses, Wirksamkeit, Probleme.

### Szenario 10: Belastungstest (15 Minuten)

**Ziel:** Anwendungsstabilität und Fehlerbehandlung testen.

1. **Große Frequenzbereiche testen:**
   - Scan von 1 MHz bis 900 MHz
   - Ist die Leistung akzeptabel?
   - Funktioniert akustischer Modus mit vielen Punkten?
2. **Sehr kleine Schrittweiten testen:**
   - Kleiner Bereich mit vielen Punkten
   - Verlangsamt sich die Anwendung?
   - Speicherprobleme?
3. **Schnelle Befehle testen:**
   - Schnell verschiedene Tasten drücken
   - Schnell zwischen Modi wechseln
   - Bleibt die Anwendung stabil?
4. **Ungültige Eingaben testen:**
   - Nicht-numerische Werte eingeben, wenn Zahlen erwartet werden
   - Frequenzen außerhalb des Bereichs eingeben
   - Sind Fehlermeldungen klar?
5. **NanoVNA trennen testen:**
   - Gerät während Messung trennen
   - Was passiert?
   - Ist Fehlerbehandlung elegant?

**Melden:** Abstürze, Einfrieren oder unerwartetes Verhalten.

### Szenario 11: Web-Interface (20 Minuten)

**Ziel:** Fernzugriffsfunktionalität über Web-Interface testen.

**Voraussetzungen:** NanoVNA verbunden, anderes Gerät (Smartphone/Tablet) im selben Netzwerk.

1. **Web-Interface starten:**
   - I-Taste im Hauptmenü drücken
   - S drücken zum Starten
   - Angezeigte URLs notieren (localhost und Netzwerk)
   - Prüfen, ob Windows-Firewall-Eingabeaufforderung erscheint
2. **Lokalen Zugriff testen:**
   - Browser auf demselben Computer öffnen
   - Zu http://localhost:8080 navigieren
   - Erscheint die Terminal-Schnittstelle?
   - Hauptmenü-Navigation ausprobieren
3. **Netzwerkzugriff testen:**
   - Browser auf anderem Gerät öffnen (Smartphone/Tablet)
   - Zur angezeigten Netzwerk-URL navigieren (z.B. http://192.168.1.x:8080)
   - Funktioniert die Verbindung?
   - Ist die Schnittstelle reaktionsschnell?
4. **Funktionalität über Web testen:**
   - Messung über Web-Interface durchführen
   - Akustischen Modus testen (Hinweis: Audio wird auf Server abgespielt, nicht im Browser)
   - Export-Funktionen testen
   - U-Menü-Funktionen testen
5. **Screenreader-Kompatibilität testen:**
   - Screenreader im Browser verwenden (NVDA, JAWS, Browser-integriert)
   - Durch Menüs navigieren
   - Sind alle Elemente zugänglich?
6. **Web-Interface stoppen:**
   - Zum Hauptmenü zurückkehren (ESC)
   - I erneut drücken
   - S zum Stoppen drücken
   - Überprüfen, ob Web-Interface stoppt

**Melden:** Verbindungsstabilität, Screenreader-Kompatibilität im Browser, Probleme mit Fernsteuerung, Verbesserungsvorschläge.

---

## 7. Bekannte Probleme und Einschränkungen

Dies sind bekannte Einschränkungen, die nicht gemeldet werden müssen:

### Plattform-Einschränkungen:
- **Nur Windows:** Anwendung kann nicht auf Linux oder macOS laufen
- **Nur NanoVNA-H4:** Speziell für dieses Modell entwickelt
- **Feste Baudrate:** 9600 Baud, keine automatische Erkennung

### Messeinschränkungen:
- **Kalibrierung nicht gespeichert:** Muss jede Sitzung neu kalibriert werden
- **Einzelner Sweep-Typ:** S11 oder S21, nicht gleichzeitig
- **Begrenzter Speicher:** Sehr große Datensätze können langsam sein

### Audio-Einschränkungen:
- **Windows Audio-API:** Benötigt funktionierendes Windows-Soundsystem
- **MIDI-Gerät:** Einige MIDI-Instrumente sind möglicherweise nicht auf allen Systemen verfügbar
- **Audio-Qualität:** Hängt von Soundkarten-Qualität ab

### Braille-Einschränkungen:
- **Nur Index-Drucker:** Direktdruck optimiert für Index V5-Serie
- **Grafikmodus:** Verwendet taktile Grafiken, nicht Braille-Text
- **Windows-Drucker:** Benötigt in Windows installierten Drucker

### Benutzeroberflächen-Einschränkungen:
- **Nur Konsole:** Keine grafische Oberfläche
- **Nur Tastatur:** Keine Mausunterstützung (durch Design)
- **Nur Englisch/Deutsch:** Noch keine anderen Sprachen

---

## 8. Kontaktinformationen

### Feedback melden:

**Hauptkontakt:** DO9RE

**Was zu senden ist:**
1. Fehlerberichte mit Vorlagen aus Abschnitt 5
2. Funktions-Feedback mit Vorlagen aus Abschnitt 5
3. Debug-Protokolldateien aus `logs/`-Verzeichnis
4. Screenshots (falls relevant)
5. Exportierte Datendateien (falls relevant)

**Dateiorganisation:**
```
IhrName_JJJJMMTT/
├── feedback_zusammenfassung.txt
├── logs/
│   ├── debug_20260125_143522.txt
│   └── debug_comm_20260125_143522.txt
├── screenshots/
│   └── screenshot1.png
└── exports/
    └── problematischer_export.csv
```

Vor dem Senden zu ZIP komprimieren.

### Antwortzeit:

- **Fehlerberichte:** Werden innerhalb von 48 Stunden bestätigt
- **Fragen:** Werden so schnell wie möglich beantwortet
- **Feature-Anfragen:** Werden für zukünftige Versionen gesammelt

### Beta-Test-Dauer:

- **Start:** [Wird von DO9RE bekannt gegeben]
- **Ende:** [Wird von DO9RE bekannt gegeben]
- **Finales Feedback-Deadline:** [Wird von DO9RE bekannt gegeben]

---

## Vielen Dank!

Ihre Teilnahme an diesem Beta-Test wird sehr geschätzt. Ihr Feedback wird NanoVNA CLI Accessible direkt verbessern und es für die Amateurfunk-Community nützlicher machen.

**73 DE DO9RE**

---

## Schnelle Test-Checkliste

Verwenden Sie dies als schnelle Referenz:

**Wesentliche Tests:**
- [ ] COM-Port-Auswahl und Geräteverbindung
- [ ] Basis-S11-Messung durchführen
- [ ] Ergebnisse in Tabelle anzeigen
- [ ] U-Menü Bandtauglichkeitsprüfung versuchen
- [ ] Akustischen Modus aktivieren und Messung abspielen
- [ ] Kurven ein-/ausschalten (Tasten 1-5)
- [ ] Nach CSV exportieren
- [ ] Hilfesystem testen (H-Taste)

**Bei verfügbarer Zeit:**
- [ ] MIDI-Audio-Engine testen
- [ ] Schleifenmarker im akustischen Modus testen
- [ ] Kontinuierlichen Sweep-Modus testen
- [ ] Import-Funktion testen
- [ ] Braille-Export testen (Datei oder Direktdruck)
- [ ] Kalibrierungsassistent testen
- [ ] Alle U-Menü-Analysefunktionen testen

**Screenreader-Benutzer:**
- [ ] Alle Menüs mit Screenreader testen
- [ ] Überprüfen, dass alle Eingabeaufforderungen gesprochen werden
- [ ] Tabellenlesbarkeit prüfen
- [ ] Zugänglichkeitsprobleme melden

**Immer:**
- [ ] Mit Debug-Protokollierung ausführen (`-d` Flag)
- [ ] Debug-Protokolle aus `logs/`-Verzeichnis speichern
- [ ] Probleme mit Schritten zur Reproduktion dokumentieren
- [ ] Systeminformationen in Berichten einschließen

---

*Vielen Dank, dass Sie helfen, NanoVNA CLI Accessible für alle besser zu machen!*
