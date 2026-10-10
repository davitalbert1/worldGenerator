cmake_minimum_required(VERSION 3.16)
project(WorldGeneratorAndClimate LANGUAGES CXX)

# Padrao C++17 ou C++20 para compatibilidade moderna de filesystem, streams e STL
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Diretorio de includes do projeto
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# Diretorio de saida dos executaveis
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

# Flags de otimizacao para calculo vetorial e matricial
if(MSVC)
    add_compile_options(/O2 /W4 /utf-8 /EHsc)
else()
    add_compile_options(-O3 -Wall -Wextra -pedantic)
endif()

# ==============================================================================
# Executavel 1: worldGenerator
# Gera mapas procedurais completos com relevo, rios, biomas, cidades e estradas
# ==============================================================================
add_executable(worldGenerator
    src/worldGenerator.cpp
)
target_include_directories(worldGenerator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# ==============================================================================
# Executavel 2: climateCalculator
# Recebe mapas e simula temperaturas, ventos globais, umidade, chuvas e biomas
# ==============================================================================
add_executable(climateCalculator
    src/climateCalculator.cpp
)
target_include_directories(climateCalculator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Copiar arquivos auxiliares (como clima.ini) para o diretorio de build se necessario
add_custom_command(TARGET climateCalculator POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/clima.ini"
    "$<TARGET_FILE_DIR:climateCalculator>/clima.ini"
)
