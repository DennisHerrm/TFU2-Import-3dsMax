// ============================================================
//  TFU2 Import - DLL-Einstieg, Importer-Klasse und MAXScript
//
//  Die Exporte sind nicht extern "C"; die .def-Datei sorgt dafuer,
//  dass Max die undekorierten Namen findet (wie beim SWBF2 Import).
// ============================================================
#include "tfu2import.h"

#include <iFnPub.h>

HINSTANCE hInstance = nullptr;

BOOL WINAPI DllMain(HINSTANCE hinstDLL, ULONG fdwReason, LPVOID) {
    if (fdwReason == DLL_PROCESS_ATTACH) {
        hInstance = hinstDLL;
        DisableThreadLibraryCalls(hinstDLL);
    }
    return TRUE;
}

class TFU2SceneImport : public SceneImport {
public:
    int ExtCount() override { return 3; }
    const MCHAR* Ext(int i) override {
        switch (i) {
        case 0: return _T("lp");
        case 1: return _T("gto");
        case 2: return _T("exe");
        default: return _T("");
        }
    }
    const MCHAR* ShortDesc() override { return _T("Star Wars: The Force Unleashed I + II"); }
    const MCHAR* LongDesc() override {
        return _T("TFU / TFU2: game (a LevelPacks .lp / SWTFU.exe / SWTFU2.exe -> character window) or a loose .gto model");
    }
    const MCHAR* AuthorName() override { return _T("DennisH"); }
    const MCHAR* CopyrightMessage() override { return _T(""); }
    const MCHAR* OtherMessage1() override { return _T(""); }
    const MCHAR* OtherMessage2() override { return _T(""); }
    unsigned int Version() override { return TFU2IMPORT_VERSION; }
    void ShowAbout(HWND hWnd) override {
        MessageBox(hWnd, _T("TFU2 Import ") TFU2IMPORT_VERSION_STR _T("\n\nCharacters and animations straight from\n")
                         _T("Star Wars: The Force Unleashed and The Force Unleashed II.\n\nNot affiliated with Lucasfilm, LucasArts, Disney or Autodesk."),
                   _T("TFU2 Import"), MB_ICONINFORMATION);
    }
    int DoImport(const MCHAR* name, ImpInterface*, Interface*, BOOL suppressPrompts) override {
        return tfu2::ImportiereEingang(name, suppressPrompts);
    }
};

class TFU2SceneImportClassDesc : public ClassDesc2 {
public:
    int IsPublic() override { return TRUE; }
    // new statt statischem Objekt: Max gibt die Importer-Instanz selbst frei.
    void* Create(BOOL) override { return new TFU2SceneImport(); }
    const MCHAR* ClassName() override { return _T("TFU2 Import"); }
#if defined(MAX_RELEASE) && (MAX_RELEASE >= 24000)
    const MCHAR* NonLocalizedClassName() override { return _T("TFU2 Import"); }
#endif
    SClass_ID SuperClassID() override { return SCENE_IMPORT_CLASS_ID; }
    Class_ID ClassID() override { return TFU2IMPORT_SCENE_CLASS_ID; }
    const MCHAR* Category() override { return _T("Import"); }
    const MCHAR* InternalName() override { return _T("TFU2SceneImport"); }
    HINSTANCE HInstance() override { return hInstance; }
};

static TFU2SceneImportClassDesc theSceneImportClassDesc;

// ------------------------------------------------------------
//  MAXScript: Tfu2Cpp
//
//    Tfu2Cpp.showDialog()                          das Figurenfenster
//    Tfu2Cpp.showAnimDialog()                      das Animationsfenster
//    Tfu2Cpp.loadOwnAnimations <ordner> <n>        die ersten n eigenen Clips der
//                                                  Figur in die Zeitleiste (Tests)
//    Tfu2Cpp.version()                             Fassung der .dlu
//    Tfu2Cpp.importCharacter <ordner> <name>       Figur ohne Fenster (Tests)
//    Tfu2Cpp.applyAnimation <ordner> <pfad> <bool> Clip ohne Fenster (Tests)
//
//  Die beiden letzten liefern den Bericht; "ERROR: ..." bei Fehlern.
// ------------------------------------------------------------
class TFU2ImportFP : public FPStaticInterface {
public:
    enum { fn_showDialog = 0, fn_version = 1, fn_importCharacter = 2, fn_applyAnimation = 3, fn_showAnimDialog = 4, fn_loadOwn = 5 };

    BOOL showDialog() { return tfu2::OeffneFenster() >= 0 ? TRUE : FALSE; }
    BOOL showAnimDialog() { return tfu2::OeffneAnimFenster() >= 0 ? TRUE : FALSE; }

    const MCHAR* loadOwnAnimations(const MCHAR* ordner, int anzahl) {
        const tfu::Pakete* p = nullptr;
        const tfu::Katalog* k = nullptr;
        std::string fehler;
        static MSTR antwort;
        if (!tfu2::Spiel(ordner ? ordner : _T(""), p, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        std::string gto;
        const std::vector<uint32_t> sk = tfu2::CrcsInSzene(std::string(), &gto);
        const tfu::FigurEintrag* f = nullptr;
        for (const auto& e : k->figuren) if (tfu::Klein(e.gto) == tfu::Klein(gto)) { f = &e; break; }
        if (f == nullptr || sk.empty()) { antwort = _T("ERROR: no TFU character in the scene"); return antwort.data(); }
        const auto& crcs = k->AnimCrcs(*p);
        std::vector<std::string> pfade;
        for (size_t i : k->EigeneAnimationen(*p, *f))
            if (tfu::PasstZu(crcs[i], sk) && (anzahl <= 0 || static_cast<int>(pfade.size()) < anzahl)) pfade.push_back(k->animationen[i].pfad);
        std::vector<tfu2::Sequenz> plan;
        std::wstring bericht;
        const bool ok = tfu2::WendeFolgeAn(*p, pfade, 10, true, true, true, std::string(), plan, bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }
    const MCHAR* version() { return TFU2IMPORT_VERSION_STR; }

    const MCHAR* importCharacter(const MCHAR* ordner, const MCHAR* name) {
        const tfu::Pakete* p = nullptr;
        const tfu::Katalog* k = nullptr;
        std::string fehler;
        static MSTR antwort;
        if (!tfu2::Spiel(ordner ? ordner : _T(""), p, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        const std::string w = tfu::Klein(tfu::Utf8(name ? name : _T("")));
        const tfu::FigurEintrag* f = nullptr;
        for (const auto& e : k->figuren) if (tfu::Klein(e.name) == w || tfu::Klein(e.gto) == w) { f = &e; break; }
        if (f == nullptr) { antwort = _T("ERROR: no such character"); return antwort.data(); }
        std::wstring bericht;
        tfu2::ImportOptionen o;
        const bool ok = tfu2::ImportiereFigur(*p, *f, o, bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    const MCHAR* applyAnimation(const MCHAR* ordner, const MCHAR* pfad, BOOL wurzel) {
        const tfu::Pakete* p = nullptr;
        const tfu::Katalog* k = nullptr;
        std::string fehler;
        static MSTR antwort;
        if (!tfu2::Spiel(ordner ? ordner : _T(""), p, k, fehler)) { antwort = MSTR(_T("ERROR: ")) + MSTR::FromUTF8(fehler.c_str()); return antwort.data(); }
        std::wstring bericht;
        const bool ok = tfu2::WendeAnimationAn(*p, tfu::Utf8(pfad ? pfad : _T("")), wurzel != FALSE, true, std::string(), bericht);
        antwort = MSTR(ok ? _T("") : _T("ERROR: ")) + MSTR(bericht.c_str());
        return antwort.data();
    }

    DECLARE_DESCRIPTOR(TFU2ImportFP)
    BEGIN_FUNCTION_MAP
        FN_0(fn_showDialog, TYPE_BOOL, showDialog)
        FN_0(fn_version, TYPE_STRING, version)
        FN_2(fn_importCharacter, TYPE_STRING, importCharacter, TYPE_STRING, TYPE_STRING)
        FN_3(fn_applyAnimation, TYPE_STRING, applyAnimation, TYPE_STRING, TYPE_STRING, TYPE_BOOL)
        FN_0(fn_showAnimDialog, TYPE_BOOL, showAnimDialog)
        FN_2(fn_loadOwn, TYPE_STRING, loadOwnAnimations, TYPE_STRING, TYPE_INT)
    END_FUNCTION_MAP
};

static TFU2ImportFP theTFU2ImportFP(
    TFU2IMPORT_FP_ID, _T("Tfu2Cpp"), 0, &theSceneImportClassDesc, FP_CORE,
    TFU2ImportFP::fn_showDialog, _T("showDialog"), 0, TYPE_BOOL, 0, 0,
    TFU2ImportFP::fn_version, _T("version"), 0, TYPE_STRING, 0, 0,
    TFU2ImportFP::fn_importCharacter, _T("importCharacter"), 0, TYPE_STRING, 0, 2,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("name"), 0, TYPE_STRING,
    TFU2ImportFP::fn_applyAnimation, _T("applyAnimation"), 0, TYPE_STRING, 0, 3,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("animation"), 0, TYPE_STRING,
        _T("rootMotion"), 0, TYPE_BOOL,
    TFU2ImportFP::fn_showAnimDialog, _T("showAnimDialog"), 0, TYPE_BOOL, 0, 0,
    TFU2ImportFP::fn_loadOwn, _T("loadOwnAnimations"), 0, TYPE_STRING, 0, 2,
        _T("gameFolder"), 0, TYPE_STRING,
        _T("count"), 0, TYPE_INT,
    p_end);

__declspec(dllexport) const TCHAR* LibDescription() {
    return _T("TFU2 Import ") TFU2IMPORT_VERSION_STR _T(" - Star Wars: The Force Unleashed I + II Importer");
}
__declspec(dllexport) int LibNumberClasses() { return 1; }
__declspec(dllexport) ClassDesc* LibClassDesc(int i) { return (i == 0) ? &theSceneImportClassDesc : nullptr; }
__declspec(dllexport) ULONG LibVersion() { return VERSION_3DSMAX; }

// Fehlt die Anmeldung des Kerninterfaces, wird sie nachgeholt (Lehre aus dem SWBF2 Import).
__declspec(dllexport) int LibInitialize() {
    static bool erledigt = false;
    if (erledigt) return TRUE;
    erledigt = true;
    if (GetCOREInterface(TFU2IMPORT_FP_ID) == nullptr) RegisterCOREInterface(&theTFU2ImportFP);
    return TRUE;
}
__declspec(dllexport) int CanAutoDefer() { return FALSE; }
