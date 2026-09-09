#pragma once

namespace TerrainCompositor
{
    // New Gem-owned system and module identities.
    inline constexpr const char* TerrainCompositorSystemComponentTypeId = "{9B3AF0A3-7C83-4BA2-B5FF-5505386979FE}";
    inline constexpr const char* TerrainCompositorModuleTypeId = "{91575516-7336-4A1E-AFBD-61B3D849E15C}";
    inline constexpr const char* TerrainCompositorEditorModuleTypeId = "{5FC5F31E-D5D6-4A86-885F-D4A3D43A14CA}";

    // Existing runtime component identities are retained for serialized compatibility.
    inline constexpr const char* HeightmapStampComponentTypeId = "{DE826E30-0D82-4B21-B998-1329F69CAC1E}";
    inline constexpr const char* TerrainMeshHeightStampComponentTypeId = "{C5E2CF4F-C921-4F67-ABBD-A33AB59A33EB}";
    inline constexpr const char* TerrainMeshCutoutComponentTypeId = "{D5E31A80-59A6-4B28-ADCE-D91255CFECE5}";
    inline constexpr const char* ProceduralGroundGradientComponentTypeId = "{D8F4B7D8-C6CB-449D-BE9C-D0D6427715FF}";
    inline constexpr const char* TerrainCompositionGradientComponentTypeId = "{A4D47171-07C9-4869-8177-93628DE4C6B8}";
    inline constexpr const char* TerrainCompositionSurfaceProviderComponentTypeId = "{270F5363-4D8E-48AA-A2D4-6802ECBF7600}";
    inline constexpr const char* TerrainCompositionHeightProviderComponentTypeId = "{E703F02E-0C7D-4FCF-B1E3-BA982E1A2DD1}";

    // Existing editor component identities are retained for serialized compatibility.
    inline constexpr const char* EditorHeightmapStampComponentTypeId = "{59EAF81A-7D54-4BD2-A115-829BDB8127F0}";
    inline constexpr const char* EditorTerrainMeshHeightStampComponentTypeId = "{D2F228FA-BE87-4F9F-95DA-4A09E2BFBDEC}";
    inline constexpr const char* EditorTerrainMeshCutoutComponentTypeId = "{FB5917BB-4A56-4DB4-B70A-1C28A27EA9A4}";
    inline constexpr const char* EditorTerrainCompositionGradientComponentTypeId = "{43588E85-B704-461F-AC94-8C9E1D916A9A}";
    inline constexpr const char* EditorTerrainCompositionSurfaceProviderComponentTypeId = "{6C42975D-3A9D-4408-903C-F8AF19BB6C57}";
    inline constexpr const char* EditorTerrainCompositionHeightProviderComponentTypeId = "{0998872A-565A-47E8-B5DA-C328BF5BE37C}";

    // Existing configuration identities are retained for serialized compatibility.
    inline constexpr const char* TerrainCompositionConfigTypeId = "{3E33FAE3-51F5-4E56-97AE-02E857E7257B}";
    inline constexpr const char* TerrainCompositionSurfaceProviderConfigTypeId = "{32D01945-9E91-4529-8F88-D9ABCE4C2C3B}";
    inline constexpr const char* TerrainCompositionHeightProviderConfigTypeId = "{81065872-D8B7-470A-B4B7-4867A75D2ABC}";
    inline constexpr const char* HeightmapStampConfigTypeId = "{B8F5F014-E660-419C-8D7F-1B083C4EE514}";
    inline constexpr const char* TerrainMeshHeightStampConfigTypeId = "{25D76510-C413-4EC4-99D6-6A1960A14441}";
    inline constexpr const char* TerrainMeshCutoutConfigTypeId = "{B310175A-F9C5-47C4-812A-28B2EAE72D5A}";

    // Existing cache interface identities are retained for binary and reflection compatibility.
    inline constexpr const char* HeightmapDataCacheTypeId = "{78BCDF15-016B-4A10-88B2-A10A867DB74D}";
    inline constexpr const char* TerrainMeshCutoutDataCacheTypeId = "{96621700-C031-4D72-856D-02C6FE597C77}";
    inline constexpr const char* TerrainMeshHeightDataCacheTypeId = "{5C3D268F-3A72-4776-8357-E773516F5262}";
} // namespace TerrainCompositor

