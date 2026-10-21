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

# E1 工具链 pin：本工程仅支持 MSVC（VS2022 + NMake，见 README-build.md）。
# MinGW 在此配置下会静默产出坏包：exe 动态依赖 libgcc_s_seh-1.dll/libstdc++-6.dll
# （下面的 POST_BUILD 只处理 MSVC 的 VC Redist，不拷贝 GCC 运行时，且它们非系统组件），
# 拷走即 0xC0000135 无法启动；在 dev 机上即使能启动也可能因 PATH 里混入外来版本
# libstdc++ 而 0xC0000005 崩溃（2026-10-16 实测）。非 MSVC 直接 fail-fast。
if(NOT MSVC)
    message(FATAL_ERROR "仅支持 MSVC 工具链（VS2022 + NMake）；当前 C++ 编译器：${CMAKE_CXX_COMPILER}。CLion 请把 toolchain 切到 Visual Studio 后重新 configure。")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# C++ 运行时静态链接（/MT；Debug 自动映射 /MTd）：exe 自身零 VC redist 依赖。
# 安全前提：与 llama.dll 的边界是纯 C API，无 CRT 对象跨越（堆内存谁分配谁释放，
# 见 engine.cpp：model/batch/sampler 的 alloc/free 全在 DLL 侧配对）。注意 upstream
# DLL 本身仍是 /MD 构建（见 README-build.md 运行机要求），目标机仍需 VC++ Redist。
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

# —— 产物目录：dist/（exe + 全部运行时 DLL）；build/ 只留 CMake 中间文件 ——
# （单配置生成器 NMake，此变量直接生效；多配置生成器需改用按配置变量。）
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/dist")

# —— VC Redist app-local：upstream DLL 为 /MD 构建，产物自带 3 个 VC DLL，
# 目标机免装 Redist。UCRT（api-ms-win-crt-*）是 Win10+ 收件箱组件，不随包。
# 来源优先级：随 cl 同安装的 VC/Redist（多版本取最新）→ System32（须本机构建机已装）。
if(MSVC)
    get_filename_component(_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_vc_dir "${_cl_dir}/../../../../../.." ABSOLUTE)
    file(GLOB _vc_crt_dirs "${_vc_dir}/Redist/MSVC/*/x64/Microsoft.VC143.CRT")
    set(_vc_dll_names msvcp140.dll vcruntime140.dll vcruntime140_1.dll)
    set(VC_REDIST_DLLS "")
    if(_vc_crt_dirs)
        list(SORT _vc_crt_dirs COMPARE NATURAL ORDER DESCENDING)
        list(GET _vc_crt_dirs 0 _vc_crt_latest)
        foreach(_n IN LISTS _vc_dll_names)
            if(EXISTS "${_vc_crt_latest}/${_n}")
                list(APPEND VC_REDIST_DLLS "${_vc_crt_latest}/${_n}")
            endif()
        endforeach()
    endif()
    if(NOT VC_REDIST_DLLS)
        foreach(_n IN LISTS _vc_dll_names)
            if(EXISTS "$ENV{SystemRoot}/System32/${_n}")
                list(APPEND VC_REDIST_DLLS "$ENV{SystemRoot}/System32/${_n}")
            endif()
        endforeach()
        if(VC_REDIST_DLLS)
            message(STATUS "VC Redist: fallback to System32")
        endif()
    endif()
    if(NOT VC_REDIST_DLLS)
        message(WARNING "VC Redist DLLs not found; dist/ will lack msvcp140/vcruntime140(_1) (target machine needs VC++ Redist).")
    endif()
endif()

# —— llama 预编译包位置（Step 1 pin；VERSION.pin 锁定 tag/URL/sha256）——
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

# VC Redist 随 exe：与上面同一目标目录（dist/），目标机免装 Redist。
if(VC_REDIST_DLLS)
    add_custom_command(TARGET lai-translate POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${VC_REDIST_DLLS} $<TARGET_FILE_DIR:lai-translate>
        COMMENT "Copying VC Redist DLLs next to lai-translate.exe"
    )
endif()
