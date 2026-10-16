# CMakeLists.txt —— lai-translate 构建（Step 1 骨架约定 + Step 2–4 源码）
#
# 对应计划条款：
#   - ralplan O-E（E1）：VS2022 + CMake ≥3.26 + Ninja + C++17。
#   - ralplan C2：CMake + 隐式链接；DLL 与 exe 同目录发布。
#   - llama.cpp 官方 win-cuda 包约定布局（Step 1 落盘到 third_party/llama）：
#       third_party/llama/include/llama.h  （头，须与 DLL 同版本，Guardrails）
#       third_party/llama/lib/llama.lib    （隐式链接导入库）
#       third_party/llama/bin/*.dll         （llama.dll + ggml-*.dll + 后端插件 +
#                                            libomp.dll + CUDA 运行时，随 exe 发布）
#   - exe 名 lai-translate（P5 待转正，默认提案）。
cmake_minimum_required(VERSION 3.26)

project(lai-translate CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++ 运行时静态链接（/MT；Debug 自动映射 /MTd）：exe 自身零 VC redist 依赖。
# 安全前提：与 llama.dll 的边界是纯 C API，无 CRT 对象跨越（堆内存谁分配谁释放，
# 见 engine.cpp：model/batch/sampler 的 alloc/free 全在 DLL 侧配对）。注意 upstream
# DLL 本身仍是 /MD 构建（见 README-build.md 运行机要求），目标机仍需 VC++ Redist。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# —— llama 预编译包位置（Step 1 pin；尚在下载中，此处只按约定写路径）——
set(LLAMA_DIR "${CMAKE_SOURCE_DIR}/third_party/llama")
set(LLAMA_INCLUDE_DIR "${LLAMA_DIR}/include")
set(LLAMA_LIB "${LLAMA_DIR}/lib/llama.lib")
set(LLAMA_BIN_DIR "${LLAMA_DIR}/bin")

add_executable(lai-translate
    src/main.cpp
    src/engine.cpp
    src/prompt_spec.cpp
    src/loop.cpp
    src/i18n_count.cpp
)

target_include_directories(lai-translate PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${LLAMA_INCLUDE_DIR}"
)

# C2：隐式链接导入库（C++ 对象不过 DLL 边界，见 engine.h 注释）。
target_link_libraries(lai-translate PRIVATE "${LLAMA_LIB}")

# MSVC：/utf-8 保证中文注释与 stderr 文案按 UTF-8 编译（pytest 按 UTF-8 匹配文案）；
# /W4 警告；/EHsc 标准异常语义。
if(MSVC)
    target_compile_options(lai-translate PRIVATE /utf-8 /W4 /EHsc)
endif()

# DLL 随 exe：构建后把 bin/*.dll 拷贝到 exe 同目录（C2 + AC7 pre-flight 前提）。
# 注意：file(GLOB) 在 configure 期求值——Step 1 包下载完成后需重新 configure。
file(GLOB LLAMA_DLLS "${LLAMA_BIN_DIR}/*.dll")
if(LLAMA_DLLS)
    add_custom_command(TARGET lai-translate POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${LLAMA_DLLS} $<TARGET_FILE_DIR:lai-translate>
        COMMENT "Copying llama runtime DLLs next to lai-translate.exe"
    )
else()
    message(WARNING "No DLLs found in ${LLAMA_BIN_DIR} (Step 1 package not landed yet?); re-configure after download.")
endif()
