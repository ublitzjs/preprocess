{
  "targets": [
    {
      "target_name": "addon",
      "sources": [ "src/main2.cpp" ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "dependencies": [
         "<!(node -p \"require('node-addon-api').gyp\")"
       ],
       "cflags!": [ "-fno-exceptions" ],
       "cflags_cc!": [ "-fno-exceptions" ],
       "defines": [ "NAPI_CPP_EXCEPTIONS" ],
      "conditions": [
        ["OS=='win'", {
          "msvs_settings": {
            "VCCLCompilerTool": {
              "ExceptionHandling": 1
            }
          }
        }],
        ["OS=='mac'", {
          "xcode_settings": {
            "GCC_ENABLE_CPP_EXCEPTIONS": "YES",
            "CLANG_CXX_LIBRARY": "libc++",
            "MACOSX_DEPLOYMENT_TARGET": "10.15",
            "OTHER_CPLUSPLUSFLAGS": [ "-std=c++20", "-stdlib=libc++" ]
          }
        }],
        ["OS=='linux'", {
          "cflags_cc": [ "-std=c++20", "-pthread", "-DPLATFORM_APPROACH=1" ],
          "ldflags": [ "-pthread" ]
        }]
      ]
    }
  ]
}
