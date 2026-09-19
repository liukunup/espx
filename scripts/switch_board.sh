#!/bin/bash
# ESP32 IoT Firmware - Board Configuration Switcher
# Usage: ./scripts/switch_board.sh <board>
# Example: ./scripts/switch_board.sh esp32c3

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

# Available boards
declare -A BOARDS=(
    ["esp32"]="ESP32 (Xtensa)"
    ["esp32c3"]="ESP32-C3 Super Mini (RISC-V)"
    ["esp32s3"]="ESP32-S3 (Xtensa + BLE)"
    ["esp32c6"]="ESP32-C6 (RISC-V + BLE 5.0)"
)

show_usage() {
    echo "ESP32 IoT Firmware - Board Configuration Switcher"
    echo ""
    echo "Usage: $0 <board>"
    echo ""
    echo "Available boards:"
    for board in "${!BOARDS[@]}"; do
        echo "  $board - ${BOARDS[$board]}"
    done
    echo ""
    echo "Examples:"
    echo "  $0 esp32c3          # Switch to ESP32-C3 Super Mini"
    echo "  $0 esp32            # Switch to ESP32"
    echo "  $0 build esp32c3    # Switch and build for ESP32-C3"
    echo "  $0 flash esp32c3    # Switch, build and flash"
}

# Check if IDF is activated
check_idf() {
    if [ -z "$IDF_PATH" ]; then
        echo "⚠️  IDF_PATH not set. Please run:"
        echo "   source /path/to/esp-idf/export.sh"
        return 1
    fi
    return 0
}

switch_board() {
    local board=$1
    
    if [ -z "$board" ]; then
        echo "❌ Error: Board not specified"
        show_usage
        exit 1
    fi
    
    if [ ! -f "$PROJECT_DIR/sdkconfig.defaults.$board" ]; then
        echo "❌ Error: Unknown board '$board'"
        echo ""
        show_usage
        exit 1
    fi
    
    echo "🔄 Switching to board: ${BOARDS[$board]}"
    
    # Backup current sdkconfig if exists
    if [ -f "$PROJECT_DIR/sdkconfig" ]; then
        cp "$PROJECT_DIR/sdkconfig" "$PROJECT_DIR/sdkconfig.backup"
        echo "   Backed up current sdkconfig"
    fi
    
    # Remove old build
    if [ -d "$BUILD_DIR" ]; then
        echo "   Removing old build..."
        rm -rf "$BUILD_DIR"
    fi
    
    # Copy board-specific defaults
    cp "$PROJECT_DIR/sdkconfig.defaults.$board" "$PROJECT_DIR/sdkconfig.defaults"
    
    # Set target
    echo "   Setting target to $board..."
    idf.py set-target "$board" >/dev/null 2>&1
    
    echo "✅ Switched to ${BOARDS[$board]}"
    echo ""
    echo "Run 'idf.py build' to build, then 'idf.py flash monitor' to flash."
}

build_board() {
    local board=$1
    
    check_idf || exit 1
    
    switch_board "$board"
    
    echo ""
    echo "🔨 Building for $board..."
    idf.py build
}

flash_board() {
    local board=$1
    local port="${2:-}"
    
    check_idf || exit 1
    
    switch_board "$board"
    
    echo ""
    echo "🔨 Building for $board..."
    idf.py build
    
    echo ""
    echo "⚡ Flashing to device..."
    if [ -n "$port" ]; then
        idf.py -p "$port" flash monitor
    else
        idf.py flash monitor
    fi
}

# Main
case "${1:-}" in
    -h|--help)
        show_usage
        ;;
    build)
        build_board "$2"
        ;;
    flash)
        flash_board "$2" "$3"
        ;;
    *)
        if [ -n "${1:-}" ]; then
            switch_board "$1"
        else
            show_usage
        fi
        ;;
esac
