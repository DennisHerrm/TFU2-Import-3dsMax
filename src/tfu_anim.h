// ============================================================
//  TFU Import - Animationen (.animations)
//
//  Huelle: "R2D2pack" (mit Eintragsliste) oder direkt "R2D2mult";
//  darin ein Block "mina". Ab mina+0x40 (der Basis) liegen die
//  Spurdaten, die Spurtabelle und die Keyzeiten.
//
//    mina+0x14  u32  Anzahl Spuren
//    mina+0x18  f32  Dauer in Sekunden (30 Bilder je Sekunde)
//    mina+0x30  u32  Spurtabelle   (relativ zur Basis)
//    mina+0x34  u32  Keyzeiten     (relativ zur Basis)
//
//  Spurtabelle, 16 Byte je Spur:
//    u32 Knochen-CRC (wie BoneCRCs im Modell)
//    u32 Versatz der Keyzeiten
//    u32 Versatz der Daten
//    u16 Anzahl Keys, u8 f0, u8 f1
//
//  f1: Bit 0 Verschiebung da, Bit 1 Drehung da, Bit 6 Keyzeiten als
//      u8 (sonst u16), Bits 2-5 die Kodierung:
//        0x3C  quantisiert: je Kanal Mitte (int16) und Bitbreite, dann
//              ein Bitstrom (MSB zuerst) mit Abweichungen, Keys
//              verschraenkt. Verschiebung = Wert * Bereich/32767,
//              Drehung = Wert/32767 fuer x,y,z, w = sqrt(1 - ...).
//        0x28  rohe Floats je vorhandenem Kanal.
//        0x00/0x20  feste Typen je Key (f0-Nibble ist der Typ).
//  f0: hohes Nibble Drehung, niedriges Verschiebung. Bei 0x3C/0x28 ist
//      es eine Maske 1xyz (welche Kanaele gespeichert sind, der Rest 0).
//
//  Geprueft gegen 56 Clips, die das Spiel zusaetzlich als Maya-XML
//  mitliefert: groesste Abweichung 0,6 Grad und 0,6 mm - das ist die
//  verlustbehaftete Kompression selbst. Alle 6516 Dateien lesen sich
//  ohne Ueberlauf, alle Quaternionen normiert.
// ============================================================
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace tfu {

struct AnimSpur {
    uint32_t crc = 0;
    uint8_t f0 = 0, f1 = 0;
    std::vector<uint16_t> keys;        // Bildnummern
    bool hatT = false, hatR = false;
    std::vector<float> t;              // 3 je Key, Meter
    std::vector<float> r;              // 4 je Key, x y z w
};

struct AnimClip {
    std::string name;
    float dauer = 0.0f;
    int bilder = 0;                    // Dauer * 30 + 1
    std::vector<AnimSpur> spuren;

    bool Lies(const std::vector<uint8_t>& d, std::string& fehler);
};

} // namespace tfu
