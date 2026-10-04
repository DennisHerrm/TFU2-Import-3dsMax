#include "tfu_pak.h"

#include "miniz.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace tfu {

namespace {

uint16_t U16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t U32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t U64(const uint8_t* p) { return static_cast<uint64_t>(U32(p)) | (static_cast<uint64_t>(U32(p + 4)) << 32); }

// Datei ueber Win32 (Pfade mit Umlauten, Dateien > 2 GB).
struct Datei {
    HANDLE h = INVALID_HANDLE_VALUE;
    explicit Datei(const std::wstring& pfad) {
        h = CreateFileW(pfad.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                        FILE_ATTRIBUTE_NORMAL, nullptr);
    }
    ~Datei() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); }
    bool ok() const { return h != INVALID_HANDLE_VALUE; }
    uint64_t Groesse() const {
        LARGE_INTEGER g;
        return GetFileSizeEx(h, &g) ? static_cast<uint64_t>(g.QuadPart) : 0;
    }
    bool Lies(uint64_t wo, void* ziel, size_t n) const {
        LARGE_INTEGER p;
        p.QuadPart = static_cast<LONGLONG>(wo);
        if (!SetFilePointerEx(h, p, nullptr, FILE_BEGIN)) return false;
        uint8_t* z = static_cast<uint8_t*>(ziel);
        while (n > 0) {
            DWORD stueck = static_cast<DWORD>(std::min<size_t>(n, 1u << 30));
            DWORD gelesen = 0;
            if (!ReadFile(h, z, stueck, &gelesen, nullptr) || gelesen == 0) return false;
            z += gelesen;
            n -= gelesen;
        }
        return true;
    }
};

bool DateiDa(const std::wstring& p) {
    const DWORD a = GetFileAttributesW(p.c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}

} // namespace

std::wstring Breit(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), &w[0], n);
    return w;
}

std::string Utf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

std::string Klein(std::string s) {
    for (char& c : s) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return s;
}

std::string Blatt(const std::string& pfad) {
    const size_t p = pfad.find_last_of("/\\");
    return p == std::string::npos ? pfad : pfad.substr(p + 1);
}

std::string OhneEndung(const std::string& name) {
    const size_t p = name.find('.');
    return p == std::string::npos ? name : name.substr(0, p);
}

bool EndetMit(const std::string& s, const std::string& ende) {
    if (ende.size() > s.size()) return false;
    for (size_t i = 0; i < ende.size(); ++i) {
        char a = s[s.size() - ende.size() + i], b = ende[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

std::string Pakete::Schluessel(const std::string& pfad) {
    std::string s = Klein(pfad);
    std::replace(s.begin(), s.end(), '\\', '/');
    while (!s.empty() && s[0] == '/') s.erase(0, 1);
    return s;
}

bool Pakete::Oeffne(const std::wstring& ordnerIn, std::string& fehler) {
    ordner_.clear();
    dateien_.clear();
    eintraege_.clear();
    nachPfad_.clear();

    // Ordner mit SWTFU2.exe, LevelPacks selbst oder eine der .lp-Dateien.
    std::wstring o = ordnerIn;
    while (!o.empty() && (o.back() == L'\\' || o.back() == L'/')) o.pop_back();
    if (o.size() > 3 && (_wcsicmp(o.c_str() + o.size() - 3, L".lp") == 0 || _wcsicmp(o.c_str() + o.size() - 4, L".exe") == 0)) {
        const size_t p = o.find_last_of(L"\\/");
        if (p != std::wstring::npos) o = o.substr(0, p);
    }
    std::wstring lp = o + L"\\LevelPacks";
    if (!DateiDa(lp + L"\\pak0.lp")) {
        if (DateiDa(o + L"\\pak0.lp")) {
            lp = o;
            const size_t p = o.find_last_of(L"\\/");
            if (p != std::wstring::npos) o = o.substr(0, p);
        } else {
            fehler = "LevelPacks\\pak0.lp not found in " + Utf8(o);
            return false;
        }
    }
    ordner_ = o;

    for (int i = 0; i < 64; ++i) {
        const std::wstring pfad = lp + L"\\pak" + std::to_wstring(i) + L".lp";
        if (!DateiDa(pfad)) break;
        Datei f(pfad);
        if (!f.ok()) { fehler = "cannot open " + Utf8(pfad); return false; }
        const uint64_t groesse = f.Groesse();
        // Ende des zentralen Verzeichnisses suchen (max. 64 KB Kommentar).
        const size_t fenster = static_cast<size_t>(std::min<uint64_t>(groesse, 65557 + 20));
        std::vector<uint8_t> ende(fenster);
        if (!f.Lies(groesse - fenster, ende.data(), fenster)) { fehler = "read error " + Utf8(pfad); return false; }
        size_t eocd = std::string::npos;
        for (size_t k = fenster - 22 + 1; k-- > 0;) {
            if (U32(&ende[k]) == 0x06054b50) { eocd = k; break; }
        }
        if (eocd == std::string::npos) { fehler = "no ZIP directory in " + Utf8(pfad); return false; }
        uint64_t zahl = U16(&ende[eocd + 10]);
        uint64_t cdGroesse = U32(&ende[eocd + 12]);
        uint64_t cdStart = U32(&ende[eocd + 16]);
        // ZIP64: Werte stehen im ZIP64-Ende.
        if (zahl == 0xFFFF || cdStart == 0xFFFFFFFF) {
            if (eocd >= 20 && U32(&ende[eocd - 20]) == 0x07064b50) {
                const uint64_t z64 = U64(&ende[eocd - 20 + 8]);
                uint8_t k64[56];
                if (f.Lies(z64, k64, sizeof k64) && U32(k64) == 0x06064b50) {
                    zahl = U64(k64 + 32);
                    cdGroesse = U64(k64 + 40);
                    cdStart = U64(k64 + 48);
                }
            }
        }
        std::vector<uint8_t> cd(static_cast<size_t>(cdGroesse));
        if (!f.Lies(cdStart, cd.data(), cd.size())) { fehler = "cannot read ZIP directory " + Utf8(pfad); return false; }
        size_t p = 0;
        for (uint64_t n = 0; n < zahl; ++n) {
            if (p + 46 > cd.size() || U32(&cd[p]) != 0x02014b50) { fehler = "broken ZIP directory in " + Utf8(pfad); return false; }
            PakEintrag e;
            e.pak = static_cast<uint32_t>(dateien_.size());
            e.methode = U16(&cd[p + 10]);
            e.gepackt = U32(&cd[p + 20]);
            e.groesse = U32(&cd[p + 24]);
            const uint16_t nl = U16(&cd[p + 28]), xl = U16(&cd[p + 30]), kl = U16(&cd[p + 32]);
            e.kopf = U32(&cd[p + 42]);
            if (p + 46 + nl + xl + kl > cd.size()) { fehler = "broken ZIP entry in " + Utf8(pfad); return false; }
            e.name.assign(reinterpret_cast<const char*>(&cd[p + 46]), nl);
            // ZIP64-Zusatzfeld
            size_t x = p + 46 + nl;
            const size_t xe = x + xl;
            while (x + 4 <= xe) {
                const uint16_t id = U16(&cd[x]), len = U16(&cd[x + 2]);
                if (id == 0x0001) {
                    size_t q = x + 4;
                    if (e.groesse == 0xFFFFFFFF && q + 8 <= x + 4 + len) { e.groesse = U64(&cd[q]); q += 8; }
                    if (e.gepackt == 0xFFFFFFFF && q + 8 <= x + 4 + len) { e.gepackt = U64(&cd[q]); q += 8; }
                    if (e.kopf == 0xFFFFFFFF && q + 8 <= x + 4 + len) { e.kopf = U64(&cd[q]); q += 8; }
                }
                x += 4u + len;
            }
            p += 46u + nl + xl + kl;
            if (!e.name.empty() && e.name.back() == '/') continue;   // Ordner
            const std::string s = Schluessel(e.name);
            const auto it = nachPfad_.find(s);
            if (it != nachPfad_.end()) {
                eintraege_[it->second] = e;          // spaeteres Paket gewinnt
            } else {
                nachPfad_.emplace(s, eintraege_.size());
                eintraege_.push_back(e);
            }
        }
        dateien_.push_back(pfad);
    }
    if (dateien_.empty()) { fehler = "no pak*.lp found"; return false; }
    return true;
}

bool Pakete::Hat(const std::string& pfad) const { return Finde(pfad) != nullptr; }

const PakEintrag* Pakete::Finde(const std::string& pfad) const {
    const auto it = nachPfad_.find(Schluessel(pfad));
    return it == nachPfad_.end() ? nullptr : &eintraege_[it->second];
}

bool Pakete::Lies(const std::string& pfad, std::vector<uint8_t>& aus, std::string& fehler) const {
    const PakEintrag* e = Finde(pfad);
    if (e == nullptr) { fehler = "not in the game: " + pfad; return false; }
    return Lies(*e, aus, fehler);
}

bool Pakete::Lies(const PakEintrag& e, std::vector<uint8_t>& aus, std::string& fehler) const {
    if (e.pak >= dateien_.size()) { fehler = "bad pak index"; return false; }
    Datei f(dateien_[e.pak]);
    if (!f.ok()) { fehler = "cannot open " + Utf8(dateien_[e.pak]); return false; }
    uint8_t lk[30];
    if (!f.Lies(e.kopf, lk, sizeof lk) || U32(lk) != 0x04034b50) { fehler = "bad local header: " + e.name; return false; }
    const uint64_t daten = e.kopf + 30 + U16(lk + 26) + U16(lk + 28);
    std::vector<uint8_t> roh(static_cast<size_t>(e.gepackt));
    if (!roh.empty() && !f.Lies(daten, roh.data(), roh.size())) { fehler = "read error: " + e.name; return false; }
    if (e.methode == 0) {
        aus.swap(roh);
    } else if (e.methode == 8) {
        aus.assign(static_cast<size_t>(e.groesse), 0);
        const size_t n = tinfl_decompress_mem_to_mem(aus.data(), aus.size(), roh.data(), roh.size(), 0);
        if (n == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || n != aus.size()) { fehler = "inflate failed: " + e.name; return false; }
    } else {
        fehler = "unsupported ZIP method " + std::to_string(e.methode) + ": " + e.name;
        return false;
    }
    return true;
}

bool Pakete::LiesAnfang(const PakEintrag& e, size_t n, std::vector<uint8_t>& aus) const {
    if (e.pak >= dateien_.size() || e.methode != 0) return false;
    Datei f(dateien_[e.pak]);
    uint8_t lk[30];
    if (!f.ok() || !f.Lies(e.kopf, lk, sizeof lk) || U32(lk) != 0x04034b50) return false;
    const uint64_t daten = e.kopf + 30 + U16(lk + 26) + U16(lk + 28);
    aus.resize(static_cast<size_t>(std::min<uint64_t>(n, e.gepackt)));
    return aus.empty() || f.Lies(daten, aus.data(), aus.size());
}

bool EntpackeGzipAnfang(const std::vector<uint8_t>& d, size_t n, std::vector<uint8_t>& aus) {
    if (d.size() < 18 || d[0] != 0x1f || d[1] != 0x8b || d[2] != 8) { aus.assign(d.begin(), d.begin() + static_cast<std::ptrdiff_t>(std::min(n, d.size()))); return true; }
    const uint8_t fl = d[3];
    size_t p = 10;
    if (fl & 4) p += 2u + U16(&d[p]);
    if (fl & 8) { while (p < d.size() && d[p] != 0) ++p; ++p; }
    if (fl & 16) { while (p < d.size() && d[p] != 0) ++p; ++p; }
    if (fl & 2) p += 2;
    if (p >= d.size()) return false;
    aus.assign(n, 0);
    mz_stream s = {};
    if (mz_inflateInit2(&s, -MZ_DEFAULT_WINDOW_BITS) != MZ_OK) return false;
    s.next_in = &d[p];
    s.avail_in = static_cast<unsigned int>(d.size() - p);
    s.next_out = aus.data();
    s.avail_out = static_cast<unsigned int>(n);
    const int r = mz_inflate(&s, MZ_SYNC_FLUSH);
    aus.resize(n - s.avail_out);
    mz_inflateEnd(&s);
    return r == MZ_OK || r == MZ_STREAM_END || r == MZ_BUF_ERROR;
}

bool EntpackeGzip(std::vector<uint8_t>& d, std::string& fehler) {
    if (d.size() < 18 || d[0] != 0x1f || d[1] != 0x8b) return true;
    if (d[2] != 8) { fehler = "gzip: unknown method"; return false; }
    const uint8_t fl = d[3];
    size_t p = 10;
    if (fl & 4) { if (p + 2 > d.size()) return false; p += 2u + U16(&d[p]); }
    if (fl & 8) { while (p < d.size() && d[p] != 0) ++p; ++p; }
    if (fl & 16) { while (p < d.size() && d[p] != 0) ++p; ++p; }
    if (fl & 2) p += 2;
    if (p + 8 > d.size()) { fehler = "gzip: truncated"; return false; }
    const uint32_t soll = U32(&d[d.size() - 4]);
    std::vector<uint8_t> aus(soll);
    const size_t n = tinfl_decompress_mem_to_mem(aus.data(), aus.size(), &d[p], d.size() - 8 - p, 0);
    if (n == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED || n != soll) { fehler = "gzip: inflate failed"; return false; }
    d.swap(aus);
    return true;
}

} // namespace tfu
