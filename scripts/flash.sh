#!/bin/bash
# ESP32 IoT Firmware - Unified Flash & Monitor Script
# Supports: ESP32S3-Pico, ESP32C3-SuperMini

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"

# Color output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

# Detect all connected ESP devices
detect_devices() {
    echo -e "${BLUE}🔍 Detecting ESP devices...${NC}"
    echo ""
    
    local devices=()
    local chip_types=()
    
    for port in /dev/tty.usbserial* /dev/tty.usbmodem*; do
        if [ -e "$port" ]; then
            devices+=("$port")
            local chip=$(python3 -m esptool --port "$port" chip_id 2>/dev/null | grep -i "chip" | head -1 || echo "Unknown")
            chip_types+=("$chip")
        fi
    done
    
    if [ ${#devices[@]} -eq 0 ]; then
        echo -e "${RED}❌ No ESP devices found${NC}"
        exit 1
    fi
    
    echo "Found ${#devices[@]} device(s):"
    echo ""
    for i in "${!devices[@]}"; do
        echo "  [$i] ${devices[$i]} - ${chip_types[$i]}"
    done
    echo ""
    
    echo "${devices[0]}"
}

show_help() {
    echo "ESP32 IoT Firmware - Flash & Monitor"
    echo ""
    echo "Usage: $0 [command] [board] [port]"
    echo ""
    echo "Commands:"
    echo "  detect  - Detect connected devices"
    echo "  flash   - Build and flash firmware"
    echo "  monitor - Open serial monitor"
    echo "  all     - Flash and monitor"
    echo "  build   - Build firmware only"
    echo ""
    echo "Boards:"
    echo "  esp8266  - NodeMCU-12F (ESP8266)"
    echo "  esp32c3  - Nologo ESP32C3 SuperMini"
    echo "  esp32s3  - Nologo ESP32S3 Pico"
    echo "  esp32_mp - MicroPython ESP32"
    echo "  auto     - Auto-detect (default)"
    echo ""
    echo "Examples:"
    echo "  $0 detect"
    echo "  $0 flash esp32c3"
    echo "  $0 flash esp8266"
    echo "  $0 all auto"
}

check_idf() {
    source /Users/liukunup/.espressif/python_env/idf6.1_py3.13_env/bin/activate
    export IDF_PATH=/Users/liukunup/.espressif/v6.1/esp-idf
    export IDF_PYTHON_ENV_PATH=/Users/liukunup/.espressif/python_env/idf6.1_py3.13_env
    export IDF_TOOLS_PATH=/Users/liukunup/.espressif
    export ESP_IDF_VERSION=6.1
    export IDF_PYTHON_ENV_PATH=/Users/liukunup/.espressif/python_env/idf6.1_py3.13_env
    export PATH=$IDF_PATH/tools:$IDF_PATH/components/esptool_py/esptool:$PATH
    # Add RISC-V toolchain for ESP32-C3/C6
    export PATH=/Users/liukunup/.espressif/tools/riscv32-esp-elf/esp-15.2.0_20251204/riscv32-esp-elf/bin:$PATH
}

switch_board() {
    local board=$1
    local board_name=""
    
    case "$board" in
        esp8266) board_name="NodeMCU-12F (ESP8266)" ;;
        esp32c3) board_name="Nologo ESP32C3 SuperMini" ;;
        esp32s3) board_name="Nologo ESP32S3 Pico" ;;
        esp32_mp) board_name="MicroPython ESP32" ;;
        *) board_name="$board" ;;
    esac
    
    echo -e "${BLUE}🔄 Switching to $board_name${NC}"
    
    if [ ! -f "$PROJECT_DIR/sdkconfig.defaults.$board" ]; then
        echo -e "${RED}❌ Config not found: sdkconfig.defaults.$board${NC}"
        exit 1
    fi
    
    cp "$PROJECT_DIR/sdkconfig.defaults.$board" "$PROJECT_DIR/sdkconfig.defaults"
    rm -rf "$PROJECT_DIR/build"
    idf.py set-target "$board" 2>/dev/null
    
    echo -e "${GREEN}✅ Switched to $board_name${NC}"
}

build() {
    local board=$1
    check_idf
    
    if [ -n "$board" ] && [ "$board" != "auto" ]; then
        switch_board "$board"
    fi
    
    echo -e "${BLUE}🔨 Building...${NC}"
    idf.py build
    echo -e "${GREEN}✅ Build complete${NC}"
}

flash_device() {
    local board=$1
    local port=$2
    
    build "$board"
    
    if [ -z "$port" ]; then
        port=$(detect_devices)
    fi
    
    echo -e "${BLUE}⚡ Flashing to $port...${NC}"
    idf.py -p "$port" flash
}

monitor_device() {
    local port=$1
    
    if [ -z "$port" ]; then
        port=$(detect_devices)
    fi
    
    echo -e "${BLUE}📟 Opening monitor on $port (Ctrl+] to exit)${NC}"
    idf.py -p "$port" monitor
}

flash_all() {
    local board=$1
    local port=$2
    
    flash_device "$board" "$port"
    echo ""
    monitor_device "$port"
}

# Main
COMMAND="${1:-}"
BOARD="${2:-auto}"
PORT="${3:-}"

case "$COMMAND" in
    detect|d) detect_devices ;;
    flash|f) flash_device "$BOARD" "$PORT" ;;
    monitor|m) monitor_device "$PORT" ;;
    all|a) flash_all "$BOARD" "$PORT" ;;
    build|b) build "$BOARD" ;;
    -h|--help|h) show_help ;;
    *) show_help ;;
esac
