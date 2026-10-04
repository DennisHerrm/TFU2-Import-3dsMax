// ============================================================
//  TFU2 Import - Zugriff auf die Spielpakete
//
//  Das Spiel legt seine Daten in LevelPacks\pak0.lp .. pak3.lp ab.
//  Das sind gewoehnliche ZIP-Archive (Signatur "PK\3\4"), jede
//  Datei ungepackt gespeichert (Methode 0). Gelesen wird nur das
//  zentrale Verzeichnis am Ende jedes Archivs; danach steht fuer
//  jeden Pfad fest, in welchem Paket und an welcher Stelle er liegt.
//
//  Pfade werden klein geschrieben und mit '/' gefuehrt: die
//  Materialdateien nennen ihre Texturen klein ("game/disc/..."),
//  im Archiv stehen sie gemischt ("Game/Disc/...").
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace tfu {

struct PakEintrag {
    std::string name;          // wie im Archiv (gemischte Schreibung)
    uint32_t pak = 0;          // Index in Pakete::dateien
    uint64_t kopf = 0;         // Versatz des lokalen Dateikopfs
    uint64_t gepackt = 0;
    uint64_t groesse = 0;
    uint16_t methode = 0;      // 0 = gespeichert, 8 = deflate
};

class Pakete {
public:
    // Spielordner (der mit SWTFU2.exe) oder direkt LevelPacks.
    bool Oeffne(const std::wstring& ordner, std::string& fehler);

    bool Hat(const std::string& pfad) const;
    const PakEintrag* Finde(const std::string& pfad) const;
    bool Lies(const std::string& pfad, std::vector<uint8_t>& aus, std::string& fehler) const;
    bool Lies(const PakEintrag& e, std::vector<uint8_t>& aus, std::string& fehler) const;
    // Nur die ersten n Bytes der gespeicherten (gepackten) Daten.
    bool LiesAnfang(const PakEintrag& e, size_t n, std::vector<uint8_t>& aus) const;

    const std::vector<PakEintrag>& Eintraege() const { return eintraege_; }
    const std::wstring& Ordner() const { return ordner_; }
    size_t PaketZahl() const { return dateien_.size(); }

    static std::string Schluessel(const std::string& pfad);   // klein, '/'

private:
    std::wstring ordner_;
    std::vector<std::wstring> dateien_;
    std::vector<PakEintrag> eintraege_;
    std::unordered_map<std::string, size_t> nachPfad_;
};

// gzip (1f 8b) entpacken; alles andere bleibt unveraendert.
bool EntpackeGzip(std::vector<uint8_t>& daten, std::string& fehler);
// Nur den Anfang eines gzip-Stroms entpacken (hoechstens n Bytes).
bool EntpackeGzipAnfang(const std::vector<uint8_t>& daten, size_t n, std::vector<uint8_t>& aus);

// Hilfen fuer Dateinamen
std::wstring Breit(const std::string& utf8);
std::string Utf8(const std::wstring& w);
std::string Klein(std::string s);
std::string Blatt(const std::string& pfad);          // ohne Ordner
std::string OhneEndung(const std::string& name);
bool EndetMit(const std::string& s, const std::string& ende);   // ohne Gross/klein

} // namespace tfu
