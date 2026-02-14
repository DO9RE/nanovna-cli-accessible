# NanoVNA CLI Accessible - Training Suite

**🎓 Willkommen zur umfassenden Schulungs-Suite für blinde und sehbehinderte Funkamateure!**

Diese Training Suite enthält **10 realistische Messszenarien** mit vollständigen CSV-Dateien, die Sie direkt in das NanoVNA CLI Accessible Programm importieren können - **ohne echte Hardware!**

---

## 🚀 Schnellstart

### 1. Programm starten
```bash
nanovna-cli.exe
```

### 2. Training-Datei laden
1. Drücken Sie **L** (Laden) im Hauptmenü
2. Navigieren Sie mit **Pfeiltasten** zur gewünschten Training-Datei
3. Drücken Sie **Enter**
4. Bestätigung: "XX Messpunkte erfolgreich geladen"

### 3. Akustische Analyse starten
1. Drücken Sie **A** (Akustische Analyse)
2. Drücken Sie **LEERTASTE** zum Abspielen
3. Drücken Sie **V** für Smith-Diagramm-Visualisierung
4. Drücken Sie **H** für Hilfe

### 4. MIDI Controller einrichten (optional)
Wenn Sie einen MIDI Controller (z.B. Behringer X-Touch Compact) besitzen:
1. Controller per USB anschließen
2. In der Akustischen Analyse **A** drücken (Audio-Konfiguration)
3. **C** drücken (MIDI Controller Konfiguration)
4. **E** drücken zum Aktivieren
5. **D** drücken und Gerät wählen
6. **P** drücken und Preset wählen (z.B. `behringer_x_touch_compact.cfg`)
7. **ESC** zweimal drücken um zurück zur Analyse zu kommen
8. Die Motor-Fader bewegen sich nun mit den Kurvenamplituden!

**Tipp:** Aktivieren Sie **Freeze by Touch** (T im MIDI-Menü), um durch Berühren der Fader 
die Wiedergabe zu pausieren und die aktuellen Werte zu ertasten.

---

## 📁 Enthaltene Training-Dateien

### Anfänger (⭐)
- **01_perfekte_resonanz_50ohm.csv** - Perfekte Antenne (SWR 1.0)
- **02_antenne_zu_kurz_induktiv.csv** - Zu kurze Antenne (induktiv)
- **03_antenne_zu_lang_kapazitiv.csv** - Zu lange Antenne (kapazitiv)

### Fortgeschritten (⭐⭐)
- **04_lambda_viertel_kabel.csv** - Viertelwellen-Transformation
- **05_tiefpass_filter.csv** - Tiefpass-Filter mit S21
- **06_multiband_antenne.csv** - 3 Resonanzen über mehrere Bänder
- **07_kabel_mit_daempfung.csv** - Kabelverlust-Messung

### Experten (⭐⭐⭐)
- **08_kurzschluss_10m.csv** - Kurzschluss bei 10 Meter (TDR)
- **09_offene_leitung_5m.csv** - Offene Leitung bei 5 Meter (TDR)
- **10_bandpass_filter_2m.csv** - Bandpass-Filter für 2m Band

---

## 📚 Vollständige Dokumentation

Jedes Szenario hat eine **ausführliche Dokumentation** im `doc/` Verzeichnis:

- `doc/Training_01_Perfekte_Resonanz.md`
- `doc/Training_02_Antenne_Zu_Kurz.md`
- `doc/Training_03_Antenne_Zu_Lang.md`
- ... (und so weiter für alle 10 Szenarien)

**Master-Index:** `doc/Training_Index.md` - Kompletter Leitfaden mit Lernpfad!

---

## 🎯 Was Sie lernen werden

### Grundlagen
✅ Perfekte Anpassung erkennen (SWR 1.0)  
✅ Induktiv vs. Kapazitiv unterscheiden  
✅ Smith-Diagramm akustisch navigieren  
✅ Alle 5 Kurven interpretieren (SWR, RL, |Z|, X, Phase)

### Smith-Diagramm Modi
✅ **Kartesisch** - Rechteckige Positionierung  
✅ **Polar** - Rotation und Entfernung  
✅ **Impedanz Direkt** - R und X direkt  
✅ **SWR-Kreise** - Anpassungsqualität

### Analysefunktionen
✅ U-Menü Comfort-Funktionen (12 Werkzeuge)  
✅ Resonanzsuche  
✅ SWR-Bandbreite  
✅ Anpassungshinweise  
✅ TDR-Fehlerortung

### S-Parameter
✅ S11 (Reflexion) verstehen  
✅ S21 (Transmission) verstehen  
✅ Filter analysieren  
✅ Kabeldämpfung messen

---

## 🎓 Empfohlener Lernpfad

### Woche 1: Grundlagen
**Tag 1-2:** Szenario 1 (Perfekt) - Referenz lernen  
**Tag 3-4:** Szenario 2 (Induktiv) - Zu kurz erkennen  
**Tag 5-7:** Szenario 3 (Kapazitiv) - Zu lang erkennen

### Woche 2-3: Fortgeschritten
**Tag 8-10:** Szenario 4 (λ/4 Kabel) - Transformation  
**Tag 11-13:** Szenario 5 (Tiefpass) - Filter-Analyse  
**Tag 14-17:** Szenario 6 (Multiband) - Mehrfach-Resonanzen  
**Tag 18-21:** Szenario 7 (Kabel-Dämpfung) - S21-Messung

### Woche 4: Profi
**Tag 22-24:** Szenario 8 (Kurzschluss) - TDR Fehlersuche  
**Tag 25-27:** Szenario 9 (Offene Leitung) - TDR Vergleich  
**Tag 28-30:** Szenario 10 (Bandpass) - Komplette Filter-Analyse

---

## 🎮 Wichtigste Tastenkombinationen

### Navigation
- **L** - Datei laden
- **A** - Akustische Analyse
- **LEERTASTE** - Play/Pause
- **ESC** - Zurück zum Hauptmenü

### Kurven-Steuerung
- **1-5** - Kurven ein/ausschalten
- **Strg+1-5** - Lautstärke verringern
- **Umschalt+1-5** - Lautstärke erhöhen
- **T** - Smooth/Dotted Modus

### Smith-Diagramm
- **V** - Smith-Visualisierung ein/aus
- **B** - Modi wechseln (1-6)
- **H** - Smith-spezifische Hilfe

### Analysen
- **U** - U-Menü Comfort-Funktionen
- **Y** - Y-Achsen-Lineal
- **X** - X-Achsen-Blips
- **N** - Statuszeile

---

## 💡 Tipps für erfolgreiches Lernen

### 1. Mit Szenario 1 beginnen
Machen Sie sich mit der **perfekten Referenz** vertraut. Das ist Ihre Baseline für alle Vergleiche!

### 2. Kurven einzeln durchhören
Schalten Sie alle Kurven aus (Taste 1-5), dann hören Sie jede **einzeln**. So lernen Sie die Charakteristik jeder Kurve.

### 3. Reaktanz ist King! 👑
Die **Reaktanz-Kurve (Taste 4)** ist meist am aussagekräftigsten:
- Positiv = Induktiv = Zu kurz
- Negativ = Kapazitiv = Zu lang
- Um Null = Perfekt!

### 4. Smith-Modus 3 für Anfänger
Starten Sie mit **Impedanz Direkt (Modus 3)** - am einfachsten zu verstehen!

### 5. Surround-Kalibrierung nutzen
Führen Sie den **Kalibrierungs-Assistenten** durch (A → C → W) für optimale räumliche Audio-Wahrnehmung!

### 6. Notizen machen
Schreiben Sie auf, was Sie **hören und lernen**. Das hilft beim Verinnerlichen!

---

## 🧠 Akustische Merkhilfen

### Induktiv vs. Kapazitiv
```
VORNE   = Voraus    = Induktiv   = Zu kurz  → Verlängern
HINTEN  = Hinterher = Kapazitiv  = Zu lang  → Kürzen

POSITIV = Plus      = Induktiv   = Spule
NEGATIV = Negativ   = Kapazitiv  = Kondensator
```

### SWR-Trend-Diagnose
```
SWR FÄLLT nach rechts → Resonanz OBERHALB Band → Antenne VERLÄNGERN
SWR STEIGT nach rechts → Resonanz UNTERHALB Band → Antenne KÜRZEN
```

### Smith-Position
```
ZENTRUM = Perfekt    = SWR 1.0 = 50Ω
VORNE   = Induktiv   = X > 0
HINTEN  = Kapazitiv  = X < 0
LINKS   = Niederohmig = R < 50Ω
RECHTS  = Hochohmig   = R > 50Ω
```

---

## 📊 CSV-Format (Technische Info)

Die Training-Dateien verwenden das **Standard-Export-Format** des Programms:

```csv
freq_hz,s11_re,s11_im,swr,return_loss_db,r_ohm,x_ohm,s21_re,s21_im
144000000,-0.001,0.002,1.003,46.48,50.1,0.1,,
...
```

**Felder:**
- `freq_hz` - Frequenz in Hertz
- `s11_re` / `s11_im` - S11 Reflexionskoeffizient (Real/Imaginär)
- `swr` - Stehwellenverhältnis
- `return_loss_db` - Rückflussdämpfung in dB
- `r_ohm` - Widerstand in Ohm
- `x_ohm` - Reaktanz in Ohm
- `s21_re` / `s21_im` - S21 Transmissionskoeffizient (optional)

---

## 🐛 Test-Driven Development

Diese Training Suite dient auch als **Test-Framework**!

### Bug-Report erstellen
Wenn Audio-Verhalten nicht mit Dokumentation übereinstimmt:

1. **Szenario identifizieren:** z.B. "Szenario 2"
2. **Dokumentation zitieren:** "Zeile 245: Sound sollte vorne-links starten"
3. **Tatsächliches Verhalten:** "Sound startet zentral"
4. **Reproduktionsschritte:** CSV-Datei, Tasten-Sequenz, Settings
5. **Issue erstellen:** GitHub mit detailliertem Report

**Beispiel-Struktur für Issues:**
```
Titel: Smith Kartesisch Position falsch bei Szenario 2

Szenario: Training_02
CSV: 02_antenne_zu_kurz_induktiv.csv
Dokumentation: Training_02_Antenne_Zu_Kurz.md, Zeile 245

Erwartet: Sound vorne-links, bewegt sich nach hinten-rechts
Tatsächlich: Sound zentral, keine Bewegung

Schritte:
1. CSV laden
2. Akustischen Modus (A)
3. Smith aktivieren (V)
4. Modus 1 (B → 1)
5. LEERTASTE

System: Windows 10, Stereo (2.0)
```

---

## 🎖️ Zertifizierung (Selbst-Test)

Wenn Sie **alle 10 Szenarien** durchgearbeitet haben und diese Fragen mit "Ja" beantworten können:

- [ ] Erkenne ich sofort, ob eine Antenne zu kurz, zu lang oder perfekt ist?
- [ ] Verstehe ich Smith-Positionen und kann sie deuten?
- [ ] Kann ich aus Messungen konkrete Handlungsempfehlungen ableiten?
- [ ] Kenne ich alle U-Menü-Funktionen?
- [ ] Verstehe ich S11 vs. S21?
- [ ] Kann ich Filter charakterisieren?
- [ ] Kann ich TDR-Fehlerortung durchführen?
- [ ] Fühle ich mich bereit für eigene Messungen?

**→ Herzlichen Glückwunsch! Sie sind ein zertifizierter Power-User! 🎉**

---

## 📞 Support

### Bei Fragen
1. **Dokumentation lesen:** `doc/Training_Index.md`
2. **FAQ durchsuchen:** Im Index enthalten
3. **Issue erstellen:** GitHub mit Details

### Feedback
Ihre Rückmeldungen helfen, die Suite zu verbessern:
- Was war hilfreich?
- Was war unklar?
- Welche Szenarien fehlen?

---

## 📄 Statistiken

**Training Suite umfasst:**
- 🗂️ **10 CSV-Dateien** (total 370 Zeilen, 20 KB)
- 📝 **11 Dokumentations-Dateien** (total 211 KB)
- 🎯 **30+ Übungen** eingebaut
- ⏱️ **8-10 Stunden** Lernmaterial
- 🏆 **3 Schwierigkeitsstufen** (Anfänger bis Experte)
- 🧠 **100+ Lernziele** abgedeckt

---

## 🙏 Credits

Erstellt mit größter Sorgfalt für blinde und sehbehinderte Funkamateure.

**Besonderer Dank an:**
- Die OpenSource-Community
- Beta-Tester und Feedback-Geber
- Die Amateur-Radio-Community

---

**73 DE DO9RE** 🎙️📡

*"Making RF measurements accessible to everyone."*

---

**Start your training journey NOW:** Load `01_perfekte_resonanz_50ohm.csv` and press SPACE! 🚀
