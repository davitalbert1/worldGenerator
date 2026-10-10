# Guia de Compilação e Execução

Este projeto contém a conversão nativa em C++ de alta performance do gerador de mundos (`worldGenerator.exe`) e o novo simulador atmosférico e climático (`climateCalculator.exe`), ambos configuráveis via arquivo `.ini` (`clima.ini`).

---

## 1. Visão Geral dos Executáveis

1. **`worldGenerator.exe`**:
   - Gera mapas procedurais completos baseados em ruído coerente multi-escala.
   - Constrói relevo, bacias hidrográficas e rios com dinâmica de partículas, biomas, cidades e vilas baseadas em atratividade ecológica.
   - Calcula malha viária com algoritmo A* (menor custo de terreno).
   - Renderiza os mapas nativamente em alta resolução (`.bmp` e `.ppm` na pasta `mapas/`) sem depender de Python ou bibliotecas pesadas.
   - Exporta os dados completos para `json/`.

2. **`climateCalculator.exe`**:
   - Recebe mapas procedurais ou gerados previamente (JSON ou relevo procedural).
   - Simula fisicamente a temperatura (por latitude, insolação e gradiente adiabático de altitude).
   - Modela células de circulação atmosférica (Hadley, Ferrel, Polar) e ventos planetários com Força de Coriolis e desvios topográficos.
   - Calcula evaporação oceânica, advecção de massas de ar e **chuvas orográficas** (chuva nas encostas e efeito de sombra de chuva / sotavento).
   - Classifica automaticamente zonas térmicas e biomas de Whittaker calibrados.
   - Exporta imagens das camadas (`clima_output/`) e um relatório estatístico completo em JSON.
   - **Totalmente parametrizável pelo arquivo `clima.ini`** (temperatura média global, equador, polos, vento, evaporação, etc.).

---

## 2. Requisitos de Compilação

- **Compilador C++**: GCC (MinGW-w64) 9+, Clang 10+ ou MSVC (Visual Studio 2019/2022) com suporte a **C++17** ou **C++20**.
- **CMake**: versão 3.16 ou superior.
- **Gerador de Build (opcional/recomendado)**: `Ninja` ou `MinGW Makefiles`.
- **Dependências Externas**: Nenhuma obrigatória! Toda a renderização gráfica, parser INI, leitor/escritor JSON e ruído Perlin são **header-only e integrados na pasta `include/`**.

---

## 3. Como Compilar com CMake

### Opção A: Usando MinGW64 / MSYS2 com Ninja (Recomendado)

Abra o terminal (PowerShell ou Bash do MSYS2) e certifique-se de que o CMake e o compilador estão no `PATH` (exemplo: `$env:PATH = "D:\msys64\mingw64\bin;" + $env:PATH`):

```powershell
# 1. Configurar o projeto com CMake e gerador Ninja
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# 2. Compilar os executáveis
cmake --build build
```

### Opção B: Usando MinGW Makefiles

```powershell
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

### Opção C: Usando Visual Studio (MSVC)

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### Opção D: Compilação Direta via Linha de Comando (Sem CMake)

Se preferir compilar diretamente com o `g++`:

```powershell
# Compilar worldGenerator.exe
g++ -std=c++17 -O3 -Iinclude src/worldGenerator.cpp -o worldGenerator.exe

# Compilar climateCalculator.exe
g++ -std=c++17 -O3 -Iinclude src/climateCalculator.cpp -o climateCalculator.exe
```

---

## 4. Como Executar os Executáveis (`.exe`)

Após a compilação, os executáveis estarão localizados na pasta `build/` (ou na raiz do projeto, caso tenha compilado diretamente):

### 4.1 Executar o Gerador de Mundos (`worldGenerator.exe`)

```powershell
# Execução padrão (gera a quantidade configurada no clima.ini, padrão 3 a 5 mapas):
.\build\worldGenerator.exe

# Ou passar a quantidade de mapas desejada como argumento:
.\build\worldGenerator.exe 3
```

**Arquivos gerados**:
- Imagens do mapa em alta definição: `mapas/mapa_X_YYYYMMDD_HHMMSS.bmp` e `.ppm`.
- Dados vetoriais, estradas e matriz de relevo: `json/mapa_X_YYYYMMDD_HHMMSS.json`.

---

### 4.2 Executar o Calculador Climático (`climateCalculator.exe`)

O `climateCalculator.exe` lê automaticamente as variáveis físicas definidas em `clima.ini`.

```powershell
# Modo 1: Execução automática (carrega automaticamente o mapa mais recente da pasta json/ e usa clima.ini):
.\build\climateCalculator.exe

# Modo 2: Especificar um mapa JSON específico:
.\build\climateCalculator.exe -m json/mapa_1_20260408_103548.json

# Modo 3: Especificar um arquivo de configuração .ini customizado:
.\build\climateCalculator.exe -c clima.ini -m json/mapa_1_20260408_103548.json
```

**Arquivos gerados em `clima_output/`**:
- `*_temperatura.bmp`: Gradiente térmico (cores polares frias a equatoriais quentes com resfriamento em montanhas).
- `*_chuva.bmp`: Mapa de precipitação anual em mm (destacando áreas úmidas e sombras de chuva secas).
- `*_ventos.bmp`: Campo vetorial e intensidade dos ventos planetários (Hadley, Alísios e ventos de oeste).
- `*_biomas_climaticos.bmp`: Biomas recalibrados a partir da física de temperatura e umidade.
- `*_relatorio.json`: Relatório estatístico com médias de temperatura, chuva, ventos e contagem de biomas.

---

## 5. Personalizando o Arquivo `clima.ini`

Abra o arquivo `clima.ini` com qualquer editor de texto. As variáveis mais importantes são:

```ini
[temperatura]
temperatura_media_global = 15.0      ; Média do planeta em °C
temperatura_equador = 30.0           ; Temperatura no equador
temperatura_polos = -22.0            ; Temperatura nos polos
taxa_lapse_altitude = 6.5            ; Queda de temperatura em montanhas (°C/1000m)
moderacao_termica_oceano = 0.35      ; Efeito moderador do oceano

[ventos]
forca_vento_base = 28.0              ; Velocidade base dos ventos em km/h
influencia_coriolis = 0.85           ; Efeito de rotação do planeta

[chuvas]
evaporacao_oceano = 1.05             ; Taxa de umidade gerada nos oceanos
taxa_orografica = 0.88               ; Intensidade de chuva ao colidir com montanhas
chuva_minima_mm = 30.0               ; Mínimo de chuva anual
chuva_maxima_mm = 3800.0             ; Máximo de chuva anual
```

Ao modificar os valores no `.ini` e rodar novamente `.\build\climateCalculator.exe`, todo o clima do mundo é recalculado instantaneamente sem necessidade de recompilar o código!
