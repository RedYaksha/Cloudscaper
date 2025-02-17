cmake_minimum_required(VERSION 3.24.0)

project(Cloudscaper)

set(THIRD_PARTY_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party)

add_subdirectory(${THIRD_PARTY_SOURCE_DIR}/DirectX-Headers)

add_subdirectory(src)
