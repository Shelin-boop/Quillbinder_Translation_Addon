# The Quillbinder add-on "Translation and Proofreading": two libraries (see src/EngineApi.h) and the checks.
#
#   packaging/build-deps.sh                       # once: oneDNN, CTranslate2, SentencePiece into deps/install
#   cmake -S . -B build -G Ninja -DCMAKE_PREFIX_PATH=/opt/qt/6.12.0/gcc_64 [-DTP_TEST_SWITCHES=ON]
#   cmake --build build
#   packaging/make-addon.sh build dist            # the .qbpkg
#
# Quillbinder 0.80.0 and later run on The Qt Company's Qt 6.12.0; build against that Qt for a release (the CI
# workflow does). An older Qt 6 works for trying things out (a plugin built with an older Qt loads in a newer one).
cmake_minimum_required(VERSION 3.21)
project(QuillbinderTranslationAddon LANGUAGES CXX)

set(TP_VERSION "1.0")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

option(TP_TEST_SWITCHES "Test hooks (environment variables the checks use); off for releases" OFF)
set(TP_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps/install" CACHE PATH "oneDNN, CTranslate2 and SentencePiece (packaging/build-deps.sh)")

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Network)
message(STATUS "Qt ${Qt6_VERSION}")

# ---- the writing tool the program loads
add_library(qb-translation-proofreading MODULE
  src/TranslateTool.cpp src/TranslateTool.h
  src/Panel.cpp src/Panel.h
  src/OptionsPage.cpp src/OptionsPage.h
  src/ModelStore.cpp src/ModelStore.h
  src/Logic.cpp src/Logic.h
  src/EngineApi.h
  third_party/quillbinder/ToolApi.h
  resources/resources.qrc)
target_include_directories(qb-translation-proofreading PRIVATE src third_party/quillbinder)
target_link_libraries(qb-translation-proofreading PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network ${CMAKE_DL_LIBS})
target_compile_definitions(qb-translation-proofreading PRIVATE TP_VERSION="${TP_VERSION}")
set_target_properties(qb-translation-proofreading PROPERTIES PREFIX "lib" LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/addon/plugins)
if(TP_TEST_SWITCHES)
  target_compile_definitions(qb-translation-proofreading PRIVATE TP_TEST_SWITCHES)
endif()

# ---- the engine: CTranslate2 (with oneDNN) and SentencePiece linked in; needs Qt Core and libgomp only
find_path(TP_CT2_INCLUDE ctranslate2/translator.h PATHS ${TP_DEPS_DIR}/include NO_DEFAULT_PATH)
find_path(TP_SPM_INCLUDE sentencepiece_processor.h PATHS ${TP_DEPS_DIR}/include NO_DEFAULT_PATH)
find_library(TP_CT2_LIB NAMES libctranslate2.a PATHS ${TP_DEPS_DIR}/lib ${TP_DEPS_DIR}/lib64 NO_DEFAULT_PATH)
find_library(TP_DNNL_LIB NAMES libdnnl.a PATHS ${TP_DEPS_DIR}/lib ${TP_DEPS_DIR}/lib64 NO_DEFAULT_PATH)
find_library(TP_SPM_LIB NAMES libsentencepiece.a PATHS ${TP_DEPS_DIR}/lib ${TP_DEPS_DIR}/lib64 NO_DEFAULT_PATH)
find_library(TP_CPUF_LIB NAMES libcpu_features.a PATHS ${TP_DEPS_DIR}/lib ${TP_DEPS_DIR}/lib64 NO_DEFAULT_PATH)
find_package(OpenMP REQUIRED COMPONENTS CXX)
find_package(Threads REQUIRED)
if(TP_CT2_INCLUDE AND TP_SPM_INCLUDE AND TP_CT2_LIB AND TP_DNNL_LIB AND TP_SPM_LIB)
  add_library(qb-tp-engine MODULE src/engine/Engine.cpp src/EngineApi.h)
  target_include_directories(qb-tp-engine PRIVATE src)
  target_include_directories(qb-tp-engine SYSTEM PRIVATE ${TP_CT2_INCLUDE} ${TP_SPM_INCLUDE})
  set(_cpuf "")
  if(TP_CPUF_LIB)
    set(_cpuf ${TP_CPUF_LIB})
  endif()
  target_link_libraries(qb-tp-engine PRIVATE Qt6::Core ${TP_CT2_LIB} ${_cpuf} ${TP_DNNL_LIB} ${TP_SPM_LIB}
    OpenMP::OpenMP_CXX Threads::Threads ${CMAKE_DL_LIBS})
  # the static libraries' symbols stay inside (no clash with libraries the program or other add-ons load)
  # noexecstack: oneDNN's profiling stub (assembler) has no stack note, which would ask for an executable stack,
  # and newer systems (glibc 2.41) refuse to load such a library
  target_link_options(qb-tp-engine PRIVATE "LINKER:--exclude-libs,ALL" "LINKER:--as-needed" "LINKER:-z,noexecstack")
  # libgomp (OpenMP of GCC) beside the engine, as in the add-on: not every system has it
  execute_process(COMMAND ${CMAKE_CXX_COMPILER} -print-file-name=libgomp.so.1 OUTPUT_VARIABLE TP_GOMP OUTPUT_STRIP_TRAILING_WHITESPACE)
  add_custom_command(TARGET qb-tp-engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${TP_GOMP}" ${CMAKE_BINARY_DIR}/addon/translation-proofreading/lib/libgomp.so.1
    VERBATIM)
  set_target_properties(qb-tp-engine PROPERTIES PREFIX "lib" CXX_VISIBILITY_PRESET hidden
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/addon/translation-proofreading
    BUILD_RPATH "$ORIGIN/lib" INSTALL_RPATH "$ORIGIN/lib")
  message(STATUS "Engine: ${TP_CT2_LIB}")
else()
  message(WARNING "No engine: oneDNN, CTranslate2 or SentencePiece missing in ${TP_DEPS_DIR} (packaging/build-deps.sh)")
endif()

# the add-on's folder in the build tree as in the installed add-on: icon, licences, libgomp beside the engine
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/addon/translation-proofreading/icons ${CMAKE_BINARY_DIR}/addon/translation-proofreading/lib)
configure_file(resources/icons/translation-proofreading.svg ${CMAKE_BINARY_DIR}/addon/translation-proofreading/icons/ COPYONLY)

# ---- checks
if(TP_TEST_SWITCHES)
  enable_testing()
  find_package(Qt6 REQUIRED COMPONENTS Test)
  add_executable(tp_logic_tests tests/logic_tests.cpp src/Logic.cpp src/Logic.h resources/resources.qrc)
  target_include_directories(tp_logic_tests PRIVATE src)
  target_link_libraries(tp_logic_tests PRIVATE Qt6::Core Qt6::Test)
  add_test(NAME logic COMMAND tp_logic_tests)

  # the panel, the options and the model store with a stand-in for the program (tests/FakeHost.h), the engine and
  # real models: the tool's sources are compiled in, so the checks can look inside
  add_executable(tp_ui_tests tests/ui_tests.cpp tests/FakeHost.h
    src/TranslateTool.cpp src/TranslateTool.h src/Panel.cpp src/Panel.h src/OptionsPage.cpp src/OptionsPage.h
    src/ModelStore.cpp src/ModelStore.h src/Logic.cpp src/Logic.h src/EngineApi.h third_party/quillbinder/ToolApi.h
    resources/resources.qrc)
  target_include_directories(tp_ui_tests PRIVATE src third_party/quillbinder tests)
  target_link_libraries(tp_ui_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test ${CMAKE_DL_LIBS})
  target_compile_definitions(tp_ui_tests PRIVATE TP_BUILD_DIR="${CMAKE_BINARY_DIR}" TP_VERSION="${TP_VERSION}" TP_TEST_SWITCHES)
  add_test(NAME ui COMMAND tp_ui_tests)

  # the built plugin as the program loads it: its interfaces, commands, tab and options through QPluginLoader
  add_executable(tp_plugin_tests tests/plugin_tests.cpp tests/FakeHost.h third_party/quillbinder/ToolApi.h)
  target_include_directories(tp_plugin_tests PRIVATE third_party/quillbinder tests)
  target_link_libraries(tp_plugin_tests PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
  target_compile_definitions(tp_plugin_tests PRIVATE TP_BUILD_DIR="${CMAKE_BINARY_DIR}")
  add_test(NAME plugin COMMAND tp_plugin_tests)
endif()
