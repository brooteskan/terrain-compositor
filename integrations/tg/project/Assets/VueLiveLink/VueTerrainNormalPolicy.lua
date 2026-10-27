-- Terrain quality policy; authored texture bindings remain available for fallback.
function GetMaterialPropertyDependencies()
    return {"normal.textureMap", "normal.useTexture",
            "contributorSelection.enabled", "contributorSelection.detailNormals"}
end

function GetShaderOptionDependencies()
    return {"o_normal_useTexture"}
end

local function DetailNormalsEnabled(context)
    local optimized = context:GetMaterialPropertyValue_bool("contributorSelection.enabled")
    local detail = context:GetMaterialPropertyValue_bool("contributorSelection.detailNormals")
    return not optimized or detail
end

function Process(context)
    local texture = context:GetMaterialPropertyValue_Image("normal.textureMap")
    local useTexture = context:GetMaterialPropertyValue_bool("normal.useTexture")
    context:SetShaderOptionValue_bool("o_normal_useTexture",
        texture ~= nil and useTexture and DetailNormalsEnabled(context))
end

function ProcessEditor(context)
    local optimized = context:GetMaterialPropertyValue_bool("contributorSelection.enabled")
    context:SetMaterialPropertyVisibility("contributorSelection.detailNormals",
        optimized and MaterialPropertyVisibility_Enabled or MaterialPropertyVisibility_Disabled)
    local texture = context:GetMaterialPropertyValue_Image("normal.textureMap")
    local enabled = texture ~= nil and context:GetMaterialPropertyValue_bool("normal.useTexture")
        and DetailNormalsEnabled(context)
    local visibility = enabled and MaterialPropertyVisibility_Enabled or MaterialPropertyVisibility_Disabled
    for _, name in ipairs({"normal.textureMapUv", "normal.factor", "normal.flipX", "normal.flipY"}) do
        context:SetMaterialPropertyVisibility(name, visibility)
    end
end
