# ToyLisp für das Sanskrit-Studium

ToyLisp ist ein Desktop-Texteditor, in dem du Sanskrit-Texte, eigene Notizen und
Analyseergebnisse zusammen aufbewahren kannst. Du kannst Wörter nachschlagen,
Verse grammatisch untersuchen und markierte Textstellen mit selbst geschriebenen
Python-Makros bearbeiten. Die Dateien sind gewöhnlicher UTF-8-Text; mehrere Tabs,
Suchen/Ersetzen, Textzoom und ein zuschaltbarer Zeilenumbruch helfen beim Lesen.

## Wörter nachschlagen

Wähle in der Makro-Auswahlliste **sanskrit_lookup**, schreibe beispielsweise
diesen Block und drücke **Return direkt hinter `==macro`**:

```text
>>
गच्छति
==macro
```

ToyLisp fragt die Kölner Sanskrit-Dienste ab und fügt das Ergebnis darunter ein,
abgeschlossen durch `##`. Über **Sanskrit → Result Language** wählst du das
deutsche Petersburger Wörterbuch (PWG) oder das englische Monier-Williams (MW).
Neben dem eingegebenen Wort werden mögliche Grundformen und Wurzeln gesucht:
`गच्छति` kann so zu `गम्`, `रामेण` zu `राम` führen. Die gefundenen Stichwörter
stehen jeweils vor ihren Bedeutungen.

Diese Suche benötigt Internet und erwartet **ein einzelnes Devanagari-Wort**.
Sandhi und Komposita zerlegt sie nicht: `वसुधैव` musst du hier als `वसुधा` und
`एव` getrennt nachschlagen. Vokallänge und Schlussvirāma sind wesentlich.
Die Ausgabe ist ein gekürzter Wörterbuchauszug; ein fehlender Treffer beweist
nicht, dass eine Form falsch ist.

## Verse und Sätze untersuchen

Wähle **sanskrit_verse** in der Makro-Auswahlliste. Mit derselben Bedienung
kannst du dann einen längeren Text analysieren:

```text
>>
कर्मण्येवाधिकारस्ते मा फलेषु कदाचन
==macro
```

ToyLisp übermittelt den Text in IAST an den Online-Dienst Dharmamitra. Du erhältst
eine Wortzerlegung (Padapāṭha), Grundformen und grammatische Angaben sowie kurze
**PWG- oder MW-Bedeutungen**, entsprechend **Sanskrit → Result Language**.
Auch IAST ist als Eingabe möglich. Das eignet sich zum Vergleichen mit der eigenen
Analyse; eine zusammenhängende Übersetzung gehört bisher nicht zu diesem
Evaluator. Ergebnisse bleiben als bearbeitbarer Text im Dokument und lassen sich
mit deinen Übersetzungsversuchen und Fragen für den Unterricht ergänzen.

Trage unter **Macros → Python Interpreter…** den vollständigen Pfad zu deinem
Python-3-Interpreter ein. Die beiden neuen Sanskrit-Makros benötigen keine
zusätzlichen Python-Pakete. Du kannst **Macro** auch als Standard-Evaluator
wählen und dann statt `==macro` einfach `==` schreiben.

Zum Vergleichen bleiben `==sanskrit` und `==sanskrit-verse` aktiv: Sie verwenden
weiterhin den bisherigen C++-Durchlauf, unabhängig von der Makro-Auswahl.
Die alte Wortsuche benötigt kein Python; die alte Versanalyse liefert stets
englische MW-Bedeutungen und benötigt diese Pakete:

```bash
python3 -m venv .venv
.venv/bin/python -m pip install requests indic-transliteration
```

Trage für diese Umgebung unter **Macros → Python Interpreter…** den vollständigen
Pfad zu `.venv/bin/python` ein. Ein bereits gespeicherter Interpreter bleibt
beim Umzug der Einstellung erhalten.

## Eigene Python-Makros

Markiere Text, wähle in der Werkzeugleiste ein Makro und klicke **Run Macro**.
Das mitgelieferte **devanagari_iast** fügt unter jeder geeigneten Devanagari-Zeile
eine IAST-Zeile ein, etwa `रामः` → `rāmaḥ`. Gemischte Zeilen mit lateinischen
Buchstaben oder ASCII-Ziffern bleiben unverändert. Die Umschrift erfolgt lokal.

Eigene Makros legst du als `.py`-Dateien in `src/macro/python/` ab. Sie erhalten
die Auswahl als String und geben den Ersatztext als String zurück. Beispielsweise
wandelt `nur_iast.py` die Auswahl unmittelbar in IAST um:

```python
from indic_transliteration import sanscript


def macro(txt):
    return sanscript.transliterate(txt, sanscript.DEVANAGARI, sanscript.IAST)
```

So kannst du auch eigene Vokabellisten formatieren, Text bereinigen oder Lernhilfen
erzeugen. Beim Öffnen der Auswahlliste erscheinen neue Makros; Änderungen am
Makrocode gelten beim nächsten Aufruf **ohne Neubauen der Anwendung**.
Die Auswahl wird in einem rückgängig machbaren Schritt ersetzt.

Der Python-Prozess bleibt zwischen Aufrufen bestehen. Gemeinsame Initialisierung,
etwa für einen API-Client, kann in `src/macro/macro_runtime.py` stehen und explizit
importiert werden. Nach Änderungen an diesem Modul oder importierten Helfern
verwende **Actions → Restart Macro Interpreter**. Rückgängig stellt Text wieder
her, aber keinen Python-Zustand und keine externen API-Aufrufe.

Per Makro kannst du auch Sprachmodelle für Übersetzungsentwürfe oder
Grammatikfragen einbinden. Zusätzliche Pakete, Dienste und gegebenenfalls
Zugangsdaten hängen vom verwendeten Makro und seinen Imports ab. Bei einem
Cloud-Dienst wird der Text dorthin übertragen. Prüfe solche Vorschläge anhand
von Grammatik und Wörterbuch.

## Qt, C++, GitHub und Linux

Die Anwendung ist in **C++17** geschrieben; **Qt5 Widgets** liefert die grafische
Oberfläche. Für eigene Python-Makros brauchst du keine C++- oder Qt-Kenntnisse.
Linux ist die bisher erprobte Plattform; Windows-Unterstützung ist vorgesehen,
aber noch nicht verifiziert. Trotz des Namens ist Lisp noch nicht in den Editor
integriert.

Auf [GitHub](https://github.com/juergenboehm/toylisp) liegen Quellcode und
Projektdokumentation. Mit Git holst du eine lokale Kopie; auf GitHub lassen sich
auch Fehler melden und Änderungen als Pull Request vorschlagen.
Zum Bauen benötigst du Git, einen C++17-Compiler (auf 64-Bit-Linux auch einen
C-Compiler für das separate Lisp500), CMake ab 3.16, Qt5 ab 5.12 mit
Widgets/Network und Qt Test sowie utf8proc-Entwicklungsdateien und pkg-config.
Für die Symbole in der Werkzeugleiste wird das Qt-SVG-Plugin benötigt.

```bash
git clone https://github.com/juergenboehm/toylisp.git
cd toylisp
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build --output-on-failure
./build/toylisp
```

CMake bereitet den Bau vor, der Compiler erzeugt die Anwendung und CTest führt
die Offline-Tests aus. Liegt Qt außerhalb der üblichen Systempfade, ergänze beim
ersten CMake-Aufruf `-DCMAKE_PREFIX_PATH=/pfad/zur/Qt5-Installation`.
Für Online-Abfragen muss auch die TLS-Unterstützung der Qt-Installation passen.
Behalte das Quellverzeichnis für die Makros am konfigurierten Ort; nach einem
Umzug muss sie neu konfiguriert und gebaut werden. Weitere Einzelheiten stehen in der
[README](README.md).
