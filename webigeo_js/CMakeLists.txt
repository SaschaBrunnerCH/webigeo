#############################################################################
# weBIGeo
# Copyright (C) 2025
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#############################################################################

project(webigeo_js LANGUAGES CXX)

# Only build for Emscripten
if(NOT EMSCRIPTEN)
    message(STATUS "webigeo_js: Skipping (Emscripten only)")
    return()
endif()

set(SOURCES
    AvalancheSimulator.h AvalancheSimulator.cpp
    Settings.h
    bindings.cpp

    nodes/LoadTextureFromMemoryNode.h nodes/LoadTextureFromMemoryNode.cpp
    nodes/LoadAabbFromMemoryNode.h nodes/LoadAabbFromMemoryNode.cpp
    nodes/BufferReadbackNode.h nodes/BufferReadbackNode.cpp
    nodes/TextureReadbackNode.h nodes/TextureReadbackNode.cpp
)

# Use plain add_executable instead of qt_add_executable
# to avoid Qt's main() wrapper that calls callMain
add_executable(webigeo_js
    ${SOURCES}
)

target_link_libraries(webigeo_js PRIVATE
    webgpu
    webgpu_engine
    Qt::Core
)

target_include_directories(webigeo_js PRIVATE
    .
    ${CMAKE_SOURCE_DIR}/webigeo_eval  # Reuse Settings from webigeo_eval
    ${CMAKE_SOURCE_DIR}/webgpu_engine  # For Buffer.h and other engine internals
)

# Emscripten-specific link options
target_link_options(webigeo_js PRIVATE
    -lembind
    -sASYNCIFY=1
    -sALLOW_MEMORY_GROWTH=1
    -sMODULARIZE=1
    -sEXPORT_ES6=1
    -sEXPORT_NAME=createWebigeoModule
    -sENVIRONMENT=web
    -sWASM_BIGINT=1
    -msimd128
    -sINITIAL_MEMORY=50MB
    -sMAXIMUM_MEMORY=4GB
    -sSTACK_SIZE=5MB
    -sERROR_ON_UNDEFINED_SYMBOLS=1
    -sINVOKE_RUN=0
    -sNO_EXIT_RUNTIME=1
)

# Use emdawnwebgpu local package (same as webgpu_app)
set(EMDAWNWEBGPU_PORT "${ALP_EMDAWNWEBGPU_DIR}/emdawnwebgpu.port.py")
if(EXISTS "${EMDAWNWEBGPU_PORT}")
    message(STATUS "webigeo_js: Using local emdawnwebgpu package")
    target_link_options(webigeo_js PRIVATE "--use-port=${EMDAWNWEBGPU_PORT}")
else()
    message(STATUS "webigeo_js: Using Emscripten built-in emdawnwebgpu port")
    target_link_options(webigeo_js PRIVATE "--use-port=emdawnwebgpu")
endif()

# SDL2 for event loop (minimal)
target_link_options(webigeo_js PRIVATE "--use-port=sdl2")

# Output name
set_target_properties(webigeo_js PROPERTIES
    OUTPUT_NAME "webigeo"
    SUFFIX ".js"
)
