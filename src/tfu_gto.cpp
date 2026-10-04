#include "tfu_gto.h"
#include "tfu_pak.h"

#include <cstring>

namespace tfu {

namespace {
size_t TypGroesse(uint8_t t) {
    switch (t) {
    case 0: case 1: case 4: return 4;
    case 2: return 8;
    case 3: case 6: return 2;
    case 5: case 7: return 1;
    default: return 0;
    }
}
template <class T> T Hole(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof v); return v; }

float HalbZuFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
    uint32_t f;
    if (e == 0) {
        if (m == 0) f = s << 31;
        else {
            int ee = -1;
            uint32_t mm = m;
            do { ++ee; mm <<= 1; } while (!(mm & 1024));
            f = (s << 31) | (static_cast<uint32_t>(127 - 15 - ee) << 23) | ((mm & 1023) << 13);
        }
    } else if (e == 31) f = (s << 31) | 0x7f800000u | (m << 13);
    else f = (s << 31) | ((e + 112) << 23) | (m << 13);
    float r;
    std::memcpy(&r, &f, 4);
    return r;
}
} // namespace

const GtoEigenschaft* GtoKomponente::Finde(const char* n) const {
    for (const auto& e : eigenschaften) if (e.name == n) return &e;
    return nullptr;
}

const GtoKomponente* GtoObjekt::Finde(const char* n) const {
    for (const auto& k : komponenten) if (k.name == n) return &k;
    return nullptr;
}

bool GtoDatei::Lies(std::vector<uint8_t> d, std::string& fehler) {
    objekte.clear();
    strings.clear();
    if (!EntpackeGzip(d, fehler)) return false;
    if (d.size() < 20) { fehler = "GTO: too short"; return false; }
    const uint32_t magie = Hole<uint32_t>(&d[0]);
    const uint32_t ns = Hole<uint32_t>(&d[4]);
    const uint32_t no = Hole<uint32_t>(&d[8]);
    if (magie != 0x29f) { fehler = "GTO: wrong magic"; return false; }
    size_t p = 20;
    strings.reserve(ns);
    for (uint32_t i = 0; i < ns; ++i) {
        const void* e = std::memchr(&d[p], 0, d.size() - p);
        if (e == nullptr) { fehler = "GTO: broken string table"; return false; }
        const size_t len = static_cast<size_t>(static_cast<const uint8_t*>(e) - &d[p]);
        strings.emplace_back(reinterpret_cast<const char*>(&d[p]), len);
        p += len + 1;
    }
    auto str = [&](uint32_t i) -> std::string { return i < strings.size() ? strings[i] : std::string(); };

    std::vector<uint16_t> komponentenJe(no);
    objekte.resize(no);
    for (uint32_t i = 0; i < no; ++i) {
        if (p + 8 > d.size()) { fehler = "GTO: truncated object headers"; return false; }
        objekte[i].name = str(Hole<uint16_t>(&d[p]));
        objekte[i].protokoll = str(Hole<uint16_t>(&d[p + 2]));
        komponentenJe[i] = Hole<uint16_t>(&d[p + 6]);
        p += 8;
    }
    std::vector<uint16_t> eigenschaftenJe;
    for (uint32_t i = 0; i < no; ++i) {
        objekte[i].komponenten.resize(komponentenJe[i]);
        for (auto& k : objekte[i].komponenten) {
            if (p + 4 > d.size()) { fehler = "GTO: truncated component headers"; return false; }
            k.name = str(Hole<uint16_t>(&d[p]));
            eigenschaftenJe.push_back(Hole<uint16_t>(&d[p + 2]));
            p += 4;
        }
    }
    size_t ki = 0;
    for (auto& o : objekte) {
        for (auto& k : o.komponenten) {
            k.eigenschaften.resize(eigenschaftenJe[ki++]);
            for (auto& e : k.eigenschaften) {
                if (p + 12 > d.size()) { fehler = "GTO: truncated property headers"; return false; }
                e.name = str(Hole<uint16_t>(&d[p]));
                e.anzahl = Hole<uint32_t>(&d[p + 4]);
                e.typ = d[p + 8];
                e.breite = d[p + 9];
                p += 12;
            }
        }
    }
    const size_t datenStart = p;
    for (auto& o : objekte) {
        for (auto& k : o.komponenten) {
            for (auto& e : k.eigenschaften) {
                const size_t g = TypGroesse(e.typ);
                if (g == 0) { fehler = "GTO: unknown type " + std::to_string(e.typ); return false; }
                e.versatz = p - datenStart;
                p += g * e.Werte();
                if (p > d.size()) { fehler = "GTO: data truncated"; return false; }
            }
        }
    }
    daten.assign(d.begin() + static_cast<std::ptrdiff_t>(datenStart), d.begin() + static_cast<std::ptrdiff_t>(p));
    return true;
}

int32_t GtoDatei::Int(const GtoEigenschaft& e, size_t i) const {
    if (i >= e.Werte()) return 0;
    const uint8_t* q = Roh(e);
    switch (e.typ) {
    case 0: case 4: return Hole<int32_t>(q + 4 * i);
    case 1: return static_cast<int32_t>(Hole<float>(q + 4 * i));
    case 6: return Hole<int16_t>(q + 2 * i);
    case 5: case 7: return q[i];
    default: return 0;
    }
}

float GtoDatei::Float(const GtoEigenschaft& e, size_t i) const {
    if (i >= e.Werte()) return 0.0f;
    const uint8_t* q = Roh(e);
    switch (e.typ) {
    case 1: return Hole<float>(q + 4 * i);
    case 2: return static_cast<float>(Hole<double>(q + 8 * i));
    case 3: return HalbZuFloat(Hole<uint16_t>(q + 2 * i));
    case 0: return static_cast<float>(Hole<int32_t>(q + 4 * i));
    case 6: return static_cast<float>(Hole<int16_t>(q + 2 * i));
    case 5: case 7: return static_cast<float>(q[i]);
    default: return 0.0f;
    }
}

std::string GtoDatei::String(const GtoEigenschaft& e, size_t i) const {
    if (e.typ != 4 || i >= e.Werte()) return std::string();
    const uint32_t s = Hole<uint32_t>(Roh(e) + 4 * i);
    return s < strings.size() ? strings[s] : std::string();
}

} // namespace tfu
