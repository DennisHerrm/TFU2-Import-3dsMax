// ============================================================
//  TFU Import - GTO-Leser
//
//  Die Modelle (.gto) sind Tweak-GTO, Version 3, gzip-gepackt -
//  aber in einer GEKUERZTEN Fassung: die Koepfe sind 16-Bit-Felder
//  statt der 32-Bit-Felder des offenen GTO-Formats.
//
//    Kopf          5 x u32   Magie 0x29F, Strings, Objekte, Version, Flags
//    Strings       nullterminiert, so viele wie angegeben
//    Objekt        8 Byte    u16 Name, u16 Protokoll, u8, u8, u16 Komponenten
//    Komponente    4 Byte    u16 Name, u16 Eigenschaften
//    Eigenschaft  12 Byte    u16 Name, u16 (Muell), u32 Anzahl, u8 Typ, u8 Breite, u16 (Muell)
//    Daten         alle Eigenschaften hintereinander, Anzahl x Breite Werte
//
//  Typen: 0 int, 1 float, 2 double, 3 half, 4 string (u32 Stringindex),
//         5 bool, 6 short, 7 byte.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tfu {

struct GtoEigenschaft {
    std::string name;
    uint8_t typ = 0;
    uint8_t breite = 1;
    uint32_t anzahl = 0;
    size_t versatz = 0;        // in GtoDatei::daten
    size_t Werte() const { return static_cast<size_t>(anzahl) * breite; }
};

struct GtoKomponente {
    std::string name;
    std::vector<GtoEigenschaft> eigenschaften;
    const GtoEigenschaft* Finde(const char* n) const;
};

struct GtoObjekt {
    std::string name, protokoll;
    std::vector<GtoKomponente> komponenten;
    const GtoKomponente* Finde(const char* n) const;
};

class GtoDatei {
public:
    bool Lies(std::vector<uint8_t> roh, std::string& fehler);   // entpackt gzip selbst

    std::vector<GtoObjekt> objekte;
    std::vector<std::string> strings;
    std::vector<uint8_t> daten;

    // Werte lesen (Typ wird umgewandelt, wo es Sinn ergibt).
    int32_t Int(const GtoEigenschaft& e, size_t i = 0) const;
    float Float(const GtoEigenschaft& e, size_t i = 0) const;
    std::string String(const GtoEigenschaft& e, size_t i = 0) const;
    const uint8_t* Roh(const GtoEigenschaft& e) const { return daten.data() + e.versatz; }
};

} // namespace tfu
