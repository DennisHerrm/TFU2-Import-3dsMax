// ============================================================
//  TFU Import - Modell aus einer GTO
//
//  Aufbau einer Figur (gemessen an playerstarkillermcquarrie.gto):
//    Info                 Plattform, Version
//    Geometry/Model       Name, Anzahl Meshes und Materialien
//    Materials            je Material Name (z.B. "figur-upperBody")
//    Skeleton             Namen, CRCs, Eltern, BasePoseMatrices
//    je Teilmesh ein Paar Objekte:
//      RenderMesh         MeshInfo: Material, BonePalette, NumWeights
//      PlatformGeometry   GeometryInfo, Indices, Stream0/1, VertexDecl
//
//  BasePoseMatrices sind LOKAL (relativ zum Elternknochen), 4x4 in
//  Zeilenvektor-Schreibweise: Zeilen 0-2 die Achsen, Zeile 3 die
//  Verschiebung. Das Spiel rechnet in Metern, Y oben.
//
//  Die Vertexdeklaration ist D3D9; die Usage steht als Bit
//  (1 << D3DDECLUSAGE): 1 Position, 2 Gewichte, 4 Indizes,
//  8 Normale, 32 UV, 64 Tangente, 128 Binormale, 1024 Farbe.
//  Gewichte und Indizes (D3DCOLOR) werden Byte fuer Byte gepaart,
//  ein Index zeigt in die BonePalette des Teilmeshes.
// ============================================================
#pragma once

#include "tfu_gto.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tfu {

struct Knochen {
    std::string name;
    uint32_t crc = 0;
    int eltern = -1;
    float lokal[16] = { 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1 };
};

struct Teilmesh {
    std::string name;          // eindeutiger Name aus der Datei (UniqueSubmeshName)
    std::string material;      // GTO-Materialname, z.B. "figur-upperBody"
    std::vector<float> pos;    // 3 je Vertex
    std::vector<float> nrm;    // 3 je Vertex (kann fehlen)
    std::vector<float> uv;     // 2 je Vertex (kann fehlen)
    std::vector<float> farbe;  // 4 je Vertex (kann fehlen)
    // bis zu vier Einfluesse je Vertex, Knochen schon als Skelettindex (-1 = keiner)
    std::vector<int> knochen;
    std::vector<float> gewicht;
    std::vector<uint32_t> dreiecke;   // 3 je Dreieck
    int primitivTyp = 0;
    size_t Vertices() const { return pos.size() / 3; }
};

struct Modell {
    std::string name;
    std::vector<Knochen> knochen;
    std::vector<Teilmesh> meshes;
    std::vector<std::string> materialien;
    // TFU1: je Material ein eingebettetes <materialDefinition>-XML (Data), sonst leer
    std::vector<std::string> materialDaten;

    bool Lies(const GtoDatei& gto, std::string& fehler);
    int KnochenNachName(const std::string& n) const;
};

} // namespace tfu
