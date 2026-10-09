// ============================================================
//  TFU Import - Plugin-Kopf
//
//  Figuren aus Star Wars: The Force Unleashed II direkt aus den
//  Spielpaketen (LevelPacks\pak*.lp) nach 3ds Max: Skelett, Meshes,
//  Skinning, Materialien mit Texturen, Animationen.
//
//  Der Leser (tfu_*.h) ist derselbe, den tfudump.exe benutzt.
// ============================================================
#pragma once

#include "tfu_katalog.h"

#include <max.h>
#include <iparamb2.h>
#include <impexp.h>
#include <istdplug.h>

#include <string>
#include <vector>

#define TFU2IMPORT_VERSION      301
#define TFU2IMPORT_VERSION_STR  _T("0.3.1")

// Einmalig gezogen, nie wieder aendern.
#define TFU2IMPORT_SCENE_CLASS_ID  Class_ID(0x4c1e7b93, 0x2a6f0d58)
#define TFU2IMPORT_FP_ID           Interface_ID(0x71d3a25e, 0x0b94c6f1)

namespace tfu2 {

// Ablage: %LOCALAPPDATA%\TFU2Import (Einstellungen, Protokoll, Texturen)
std::wstring Ablage();
std::wstring LiesEinstellung(const std::wstring& schluessel);
void SchreibeEinstellung(const std::wstring& schluessel, const std::wstring& wert);

// Protokoll nach Ablage()\import.log
void LogNeu(const char* titel);
void Log(const char* format, ...);

// Spiel laden (Pakete + Katalog), einmal je Ordner; TFU1 und TFU2 bleiben
// nebeneinander geladen. Leerer Ordner = der des aktiven Spiels.
bool Spiel(const std::wstring& ordner, const tfu::Pakete*& pakete, const tfu::Katalog*& katalog, std::string& fehler);
// Dasselbe ueber die Nummer (1 = TFU, 2 = TFU II) und den gemerkten Ordner.
bool SpielNr(int spiel, const tfu::Pakete*& pakete, const tfu::Katalog*& katalog, std::string& fehler);
// Einstellungsschluessel des Ordners ("Spielordner1" / "Spielordner") und das
// im Figurenfenster gewaehlte Spiel (Einstellung "Spiel", Vorgabe 2).
std::wstring SpielordnerSchluessel(int spiel);
int AktivesSpiel();

struct ImportOptionen {
    bool texturen = true;
    bool skin = true;
};

// Figur importieren; bericht = Zusammenfassung fuer den Anwender.
bool ImportiereFigur(const tfu::Pakete& p, const tfu::FigurEintrag& f, const ImportOptionen& o, std::wstring& bericht);

// Eine importierte Figur in der Szene (alle Knoten tragen dieselbe tfu2_id).
struct SzenenFigur {
    std::string id, gto, name;
    size_t knochen = 0;
    int spiel = 2;             // tfu2_spiel der Knoten (aeltere Importe: 2)
};
// Gewaehlte Figur zuerst, sonst die zuletzt importierte.
std::vector<SzenenFigur> FigurenInSzene();

// Eine Sequenz der Zeitleiste (aus der Notizspur der Szenenwurzel), in Bildern.
struct Sequenz {
    std::string name;
    int start = 0, ende = 0;
};

// Clip auf eine Figur legen. nachVorn: Wurzel am Clipanfang nach vorn und in den Ursprung. id leer = Auswahl, sonst die zuletzt importierte.
bool WendeAnimationAn(const tfu::Pakete& p, const std::string& animPfad, bool wurzelBewegung, bool nachVorn, const std::string& id,
                      std::wstring& bericht);

// Viele Clips hintereinander in die Zeitleiste (Bindepose auf Bild 0, Abstand
// in Bildern), mit Notizspur und Custom Attributes NeoDexSequenceData.
bool WendeFolgeAn(const tfu::Pakete& p, const std::vector<std::string>& pfade, int abstand, bool notiz, bool wurzelBewegung, bool nachVorn,
                  const std::string& id, std::vector<Sequenz>& plan, std::wstring& bericht);
bool LiesSequenzen(std::vector<Sequenz>& aus);
void ZeigeBereich(int startBild, int endeBild);

// Knochen-CRCs einer Figur in der Szene (sortiert; leer wenn keine) und ihre GTO.
std::vector<uint32_t> CrcsInSzene(const std::string& id = std::string(), std::string* figur = nullptr);

// Farbe aus Max' Theme (GetCustSysColor) fuer die Fenster
uint32_t ThemeFarbe(int welche);

// Die Fenster (modal, wie beim SWBF2 Import)
int OeffneFenster();
int OeffneAnimFenster(HWND eltern = nullptr);

// Datei -> Importieren: .lp oder SWTFU2.exe oeffnet das Fenster, .gto importiert eine lose Datei.
int ImportiereEingang(const MCHAR* pfad, BOOL ohneRueckfragen);

} // namespace tfu2
