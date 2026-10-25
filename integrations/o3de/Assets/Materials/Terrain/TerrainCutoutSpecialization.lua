function GetMaterialPropertyDependencies()
    return {"settings.meshCutoutCount", "settings.meshHeightGapCount"}
end
function GetShaderOptionDependencies()
    return {"o_terrainHasMeshCutouts", "o_terrainHasHeightGaps"}
end
function Process(context)
    context:SetShaderOptionValue_bool("o_terrainHasMeshCutouts", context:GetMaterialPropertyValue_uint("settings.meshCutoutCount") > 0)
    context:SetShaderOptionValue_bool("o_terrainHasHeightGaps", context:GetMaterialPropertyValue_uint("settings.meshHeightGapCount") > 0)
end
