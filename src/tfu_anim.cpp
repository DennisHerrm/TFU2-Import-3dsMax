#include "tfu_anim.h"

#include <cmath>
#include <cstring>

namespace tfu {

namespace {

template <class T> T Hole(const std::vector<uint8_t>& d, size_t p) {
    T v{};
    if (p + sizeof(T) <= d.size()) std::memcpy(&v, &d[p], sizeof v);
    return v;
}

struct BitLeser {
    const std::vector<uint8_t>& d;
    size_t bit;
    BitLeser(const std::vector<uint8_t>& daten, size_t byte) : d(daten), bit(byte * 8) {}
    uint32_t Lies(int n) {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) {
            const size_t b = bit >> 3;
            const uint32_t x = (b < d.size()) ? ((d[b] >> (7 - (bit & 7))) & 1u) : 0u;
            v = (v << 1) | x;
            ++bit;
        }
        return v;
    }
};

void Kanaele(int nibble, int out[3], int& n) {
    n = 0;
    for (int j = 0; j < 3; ++j) if (nibble & (4 >> j)) out[n++] = j;
}

void MitW(float* q) {
    const float w2 = 1.0f - (q[0] * q[0] + q[1] * q[1] + q[2] * q[2]);
    q[3] = w2 > 0.0f ? std::sqrt(w2) : 0.0f;
}

bool Dekodiere(const std::vector<uint8_t>& d, size_t o, AnimSpur& s, std::string& fehler) {
    const int rn = s.f0 >> 4, tn = s.f0 & 15;
    const int enc = s.f1 & 0x3c;
    const size_t nk = s.keys.size();
    s.hatT = (s.f1 & 1) != 0;
    s.hatR = (s.f1 & 2) != 0;
    if (s.hatT) s.t.assign(nk * 3, 0.0f);
    if (s.hatR) s.r.assign(nk * 4, 0.0f);
    size_t p = o;
    if (enc == 0x3c) {
        int tch[3], rch[3], ntc = 0, nrc = 0;
        int16_t tc[3] = {}, rc[3] = {};
        uint8_t tb[3] = {}, rb[3] = {};
        float skal = 0.0f;
        if (s.hatT) {
            skal = Hole<float>(d, p) / 32767.0f;
            p += 4;
            Kanaele(tn, tch, ntc);
            for (int j = 0; j < ntc; ++j) { tc[j] = Hole<int16_t>(d, p); p += 2; }
            for (int j = 0; j < ntc; ++j) tb[j] = (p < d.size()) ? d[p++] : 0;
        }
        if (s.hatR) {
            Kanaele(rn, rch, nrc);
            for (int j = 0; j < nrc; ++j) { rc[j] = Hole<int16_t>(d, p); p += 2; }
            for (int j = 0; j < nrc; ++j) rb[j] = (p < d.size()) ? d[p++] : 0;
        }
        for (int j = 0; j < 3; ++j) if (tb[j] > 31 || rb[j] > 31) { fehler = "bit width out of range"; return false; }
        BitLeser br(d, p);
        for (size_t k = 0; k < nk; ++k) {
            for (int j = 0; j < ntc; ++j) {
                const int n = tb[j];
                const int v = tc[j] + (n ? static_cast<int>(br.Lies(n)) - (1 << (n - 1)) : 0);
                s.t[k * 3 + static_cast<size_t>(tch[j])] = static_cast<float>(v) * skal;
            }
            if (s.hatR) {
                float* q = &s.r[k * 4];
                for (int j = 0; j < nrc; ++j) {
                    const int n = rb[j];
                    const int v = rc[j] + (n ? static_cast<int>(br.Lies(n)) - (1 << (n - 1)) : 0);
                    q[rch[j]] = static_cast<float>(v) / 32767.0f;
                }
                MitW(q);
            }
        }
        if ((br.bit + 7) / 8 > d.size()) { fehler = "track data overrun"; return false; }
    } else if (enc == 0x28) {
        int tch[3], rch[3], ntc = 0, nrc = 0;
        if (s.hatT) Kanaele(tn, tch, ntc);
        if (s.hatR) Kanaele(rn, rch, nrc);
        if (p + nk * static_cast<size_t>(ntc + nrc) * 4 > d.size()) { fehler = "track data overrun"; return false; }
        for (size_t k = 0; k < nk; ++k) {
            for (int j = 0; j < ntc; ++j) { s.t[k * 3 + static_cast<size_t>(tch[j])] = Hole<float>(d, p); p += 4; }
            if (s.hatR) {
                float* q = &s.r[k * 4];
                for (int j = 0; j < nrc; ++j) { q[rch[j]] = Hole<float>(d, p); p += 4; }
                MitW(q);
            }
        }
    } else {
        // Typen 8-15: Maske 1xyz, Floats nur fuer die gesetzten Kanaele (fehlende = 0).
        // TFU2 nutzt fuer die Translation nur 15 (xyz), TFU1 auch Rotationen
        // mit Teilmasken (Fahrzeuge, Zwischensequenzen).
        int tch[3], rch[3], ntc = 0, nrc = 0;
        if (tn & 8) Kanaele(tn, tch, ntc);
        if (rn & 8) Kanaele(rn, rch, nrc);
        float skal = 0.0f;
        if (s.hatT && tn == 2) { skal = Hole<float>(d, p) / 32767.0f; p += 4; }
        for (size_t k = 0; k < nk; ++k) {
            if (s.hatT) {
                float* t = &s.t[k * 3];
                if (tn == 1) { for (int j = 0; j < 3; ++j) t[j] = Hole<float>(d, p + 4u * static_cast<size_t>(j)); p += 16; }
                else if (tn == 2) { for (int j = 0; j < 3; ++j) t[j] = Hole<int16_t>(d, p + 2u * static_cast<size_t>(j)) * skal; p += 6; }
                else if (tn & 8) { for (int j = 0; j < ntc; ++j) { t[tch[j]] = Hole<float>(d, p); p += 4; } }
                else { fehler = "unknown translation type " + std::to_string(tn); return false; }
            }
            if (s.hatR) {
                float* q = &s.r[k * 4];
                if (rn == 1) { for (int j = 0; j < 4; ++j) q[j] = Hole<float>(d, p + 4u * static_cast<size_t>(j)); p += 16; }
                else if (rn == 2) { for (int j = 0; j < 4; ++j) q[j] = Hole<int16_t>(d, p + 2u * static_cast<size_t>(j)) / 32767.0f; p += 8; }
                else if (rn == 3) { for (int j = 0; j < 3; ++j) q[j] = Hole<int16_t>(d, p + 2u * static_cast<size_t>(j)) / 32767.0f; MitW(q); p += 6; }
                else if (rn & 8) { for (int j = 0; j < nrc; ++j) { q[rch[j]] = Hole<float>(d, p); p += 4; } MitW(q); }
                else { fehler = "unknown rotation type " + std::to_string(rn); return false; }
                const float l = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
                if (l > 1e-6f) for (int j = 0; j < 4; ++j) q[j] /= l;
            }
        }
        if (p > d.size()) { fehler = "track data overrun"; return false; }
    }
    return true;
}

} // namespace

bool AnimClip::Lies(const std::vector<uint8_t>& d, std::string& fehler) {
    spuren.clear();
    size_t mi = std::string::npos;
    for (size_t i = 0; i + 4 <= d.size() && i < 4096; ++i) {
        if (d[i] == 'm' && d[i + 1] == 'i' && d[i + 2] == 'n' && d[i + 3] == 'a') { mi = i; break; }
    }
    if (mi == std::string::npos || mi + 0x40 > d.size()) { fehler = "no 'mina' block - not an animation"; return false; }
    const size_t basis = mi + 0x40;
    const uint32_t n = Hole<uint32_t>(d, mi + 0x14);
    dauer = Hole<float>(d, mi + 0x18);
    const size_t tabelle = basis + Hole<uint32_t>(d, mi + 0x30);
    const size_t zeiten = basis + Hole<uint32_t>(d, mi + 0x34);
    if (n > 4096 || tabelle + static_cast<size_t>(n) * 16 > d.size()) { fehler = "track table out of range"; return false; }
    bilder = static_cast<int>(std::lround(dauer * 30.0f)) + 1;
    spuren.resize(n);
    for (uint32_t i = 0; i < n; ++i) {
        AnimSpur& s = spuren[i];
        const size_t e = tabelle + static_cast<size_t>(i) * 16;
        s.crc = Hole<uint32_t>(d, e);
        const uint32_t kv = Hole<uint32_t>(d, e + 4);
        const uint32_t dv = Hole<uint32_t>(d, e + 8);
        const uint16_t nk = Hole<uint16_t>(d, e + 12);
        s.f0 = d[e + 14];
        s.f1 = d[e + 15];
        s.keys.resize(nk);
        const bool kurz = (s.f1 & 0x40) != 0;
        const size_t kp = zeiten + kv;
        if (kp + static_cast<size_t>(nk) * (kurz ? 1 : 2) > d.size()) { fehler = "key times out of range"; return false; }
        for (uint16_t k = 0; k < nk; ++k) s.keys[k] = kurz ? d[kp + k] : Hole<uint16_t>(d, kp + 2u * k);
        if (!Dekodiere(d, basis + dv, s, fehler)) {
            fehler = "track " + std::to_string(i) + ": " + fehler;
            return false;
        }
    }
    return true;
}

} // namespace tfu
