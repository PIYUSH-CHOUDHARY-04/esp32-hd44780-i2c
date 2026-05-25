#!/bin/bash

#!/bin/bash
# ==========================================
# ESP32 / ESP-IDF Central Activation Script
# Usage: source ~/embedded/envs/esp32_activate.sh
# ==========================================

# ----------------------------
# [0] Safety check (must be sourced)
# ----------------------------
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "ERROR: This script must be sourced!"
    echo "Use: source $0"
    exit 1
fi

# ----------------------------
# [1] Base paths
# ----------------------------
export EMBEDDED_ROOT=$HOME/embedded

export IDF_PATH=$EMBEDDED_ROOT/sdks/esp/esp-idf

# Optional: keep tools inside embedded instead of ~/.espressif
export IDF_TOOLS_PATH=$EMBEDDED_ROOT/toolchains/esp-idf-tools

# ----------------------------
# [2] Clean old ESP env (if any)
# ----------------------------
unset IDF_PYTHON_ENV_PATH 2>/dev/null
unset ESPPORT 2>/dev/null

hash -r

# ----------------------------
# [3] Load ESP-IDF environment
# ----------------------------
if [ -f "$IDF_PATH/export.sh" ]; then
    source "$IDF_PATH/export.sh"
else
    echo "ERROR: ESP-IDF not found at $IDF_PATH"
    return 1
fi

# ----------------------------
# [4] Project helpers
# ----------------------------
export ESP_PROJECT_ROOT=$PWD
export ESP_BUILD_DIR=$PWD/build
export ESP_BIN_DIR=$PWD/build

# ----------------------------
# [5] Default serial port (edit as needed)
# ----------------------------
export ESPPORT=/dev/ttyUSB0

# ----------------------------
# [6] esp32 board type
# ----------------------------
export IDF_TARGET

# ----------------------------
# [7] Pretty info
# ----------------------------
echo "===================================="
echo "ESP-IDF Environment Active"
echo "------------------------------------"
echo "IDF_PATH        : $IDF_PATH"
echo "TOOLS_PATH      : $IDF_TOOLS_PATH"
echo "PROJECT         : $ESP_PROJECT_ROOT"
echo "SERIAL PORT     : $ESPPORT"
echo "------------------------------------"
echo "Compiler        : $(which xtensa-esp32-elf-gcc 2>/dev/null)"
echo "Python          : $(which python3)"
echo "CMake           : $(which cmake)"
echo "Ninja           : $(which ninja)"
echo "idf.py          : $(which idf.py)"
echo "===================================="

echo "Commands:"
echo "  idf.py build"
echo "  idf.py flash"
echo "  idf.py monitor"

# ----------------------------
# [8] Unload function
# ----------------------------
unset_env() {
    echo "Unloading ESP-IDF environment..."

    unset EMBEDDED_ROOT
    unset IDF_PATH
    unset IDF_TOOLS_PATH
    unset ESP_PROJECT_ROOT
    unset ESP_BUILD_DIR
    unset ESP_BIN_DIR
    unset ESPPORT
    unset IDF_TARGET

    hash -r

    echo "Environment cleared"
}

# ----------------------------
# [X] ESP32 Target Selection
# ----------------------------
export IDF_TARGET=esp32

echo "===================================="

echo "IDF_TARGET        : $IDF_TARGET"    
# call unset_var in current shell for unsetting the current environ vars
