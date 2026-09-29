cmake_minimum_required(VERSION 3.14)

project(ObjectSlot
    VERSION 1.0.0
    DESCRIPTION "C++17 向けのヘッダーオンリー オブジェクトプールライブラリ"
    LANGUAGES CXX
)

# ============================================================
# ライブラリ本体（ヘッダーオンリーなのでビルドするものは無い）
# ============================================================
# 利用側は次のどちらかで取り込む。
#   add_subdirectory(ObjectSlot)   または   FetchContent
#   target_link_libraries(myapp PRIVATE ObjectSlot::ObjectSlot)
add_library(ObjectSlot INTERFACE)
add_library(ObjectSlot::ObjectSlot ALIAS ObjectSlot)

target_include_directories(ObjectSlot INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_compile_features(ObjectSlot INTERFACE cxx_std_17)

# ============================================================
# テスト・ベンチマーク（このプロジェクトを直接開いた時だけ作る）
# ============================================================
# OBJECT_SLOT_BUILD_TESTS=ON で tests/main.cpp をビルドする。
# OBJECT_SLOT_USE_EXPERIMENTAL=ON で実験版（4 バイトポインタ）を使う。
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(OBJECT_SLOT_BUILD_TESTS "テストとベンチマークをビルドする" ON)
    option(OBJECT_SLOT_USE_EXPERIMENTAL "実験版（4 バイトポインタ）を使う" OFF)

    if(OBJECT_SLOT_BUILD_TESTS)
        add_executable(objectSlot_test tests/main.cpp)
        target_link_libraries(objectSlot_test PRIVATE ObjectSlot::ObjectSlot)
        if(OBJECT_SLOT_USE_EXPERIMENTAL)
            target_compile_definitions(objectSlot_test PRIVATE OBJECT_SLOT_USE_EXPERIMENTAL)
        endif()
        if(MSVC)
            target_compile_options(objectSlot_test PRIVATE /W4 /utf-8)
        else()
            target_compile_options(objectSlot_test PRIVATE -Wall -Wextra)
        endif()
    endif()
endif()
