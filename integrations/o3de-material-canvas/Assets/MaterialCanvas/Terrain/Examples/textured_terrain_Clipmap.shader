{
    "Source": "textured_terrain_Clipmap.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": false
        }
    },
    "RasterState": {
        "CullMode": "None"
    },
    "ProgramSettings": {
        "EntryPoints": [
            {
                "name": "TC_GraphCacheVS",
                "type": "Vertex"
            },
            {
                "name": "TC_GraphCachePS",
                "type": "Fragment"
            }
        ]
    },
    "DrawList": "terrain-graph-cache",
    "DisabledRHIBackends": [
        "metal"
    ],
    "AddBuildArguments": {
        "dxc": [
            "-enable-16bit-types"
        ]
    }
}
