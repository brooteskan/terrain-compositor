{
    "Source": "overlap_terrain_Depth.azsl",
    "DepthStencilState": {
        "Depth": {
            "Enable": true,
            "CompareFunc": "LessEqual"
        }
    },
    "DrawList": "shadow",
    "RasterState": {
        "depthBias": "10",
        "depthBiasSlopeScale": "4"
    },
    "ProgramSettings": {
        "EntryPoints": [
            {
                "name": "MainVS",
                "type": "Vertex"
            },
            {
                "name": "MainPS",
                "type": "Fragment"
            }
        ]
    },
    "AddBuildArguments": {
        "dxc": [
            "-enable-16bit-types"
        ]
    }
}
