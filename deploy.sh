#!/bin/bash
# Deploy script for LoRa Detector on Heltec WiFi LoRa 32 V3

set -e

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

# Board configuration for Heltec V3
FQBN="esp32:esp32:heltec_wifi_lora_32_V3"
PORT="${PORT:-/dev/ttyUSB0}"
BAUD="115200"
BUILD_DIR="./build"

# Find port if not specified
find_port() {
    if [ -e "/dev/ttyUSB0" ]; then
        PORT="/dev/ttyUSB0"
    elif [ -e "/dev/ttyACM0" ]; then
        PORT="/dev/ttyACM0"
    else
        # List available ports
        echo -e "${YELLOW}Available ports:${NC}"
        ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || echo "No serial ports found"
        echo -e "${RED}Error: No serial port found. Connect the Heltec board.${NC}"
        exit 1
    fi
    echo -e "${GREEN}Using port: $PORT${NC}"
}

# Check if RadioLib is installed
check_libraries() {
    echo -e "${YELLOW}Checking libraries...${NC}"

    # Check for RadioLib
    if ! arduino-cli lib list | grep -q "RadioLib"; then
        echo -e "${YELLOW}Installing RadioLib...${NC}"
        arduino-cli lib install "RadioLib"
    fi

    # Check for U8g2
    if ! arduino-cli lib list | grep -q "U8g2"; then
        echo -e "${YELLOW}Installing U8g2...${NC}"
        arduino-cli lib install "U8g2"
    fi

    echo -e "${GREEN}Libraries OK${NC}"
}

# Check if Heltec board is installed
check_board() {
    echo -e "${YELLOW}Checking board support...${NC}"

    if ! arduino-cli board listall | grep -q "heltec_wifi_lora_32_V3"; then
        echo -e "${YELLOW}Installing Heltec ESP32 board support...${NC}"
        # Add ESP32 board URL if not present
        arduino-cli config add board_manager.additional_urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
        arduino-cli core update-index
        arduino-cli core install esp32:esp32
    fi

    echo -e "${GREEN}Board support OK${NC}"
}

compile() {
    echo -e "${YELLOW}Compiling for Heltec WiFi LoRa 32 V3...${NC}"
    arduino-cli compile \
        --fqbn "$FQBN" \
        --build-path "$BUILD_DIR" \
        .
    echo -e "${GREEN}Compile successful!${NC}"
}

upload() {
    find_port
    echo -e "${YELLOW}Uploading to $PORT...${NC}"
    arduino-cli upload \
        -p "$PORT" \
        --fqbn "$FQBN" \
        --input-dir "$BUILD_DIR" \
        .
    echo -e "${GREEN}Upload successful!${NC}"
}

monitor() {
    find_port
    echo -e "${YELLOW}Starting serial monitor on $PORT at $BAUD baud...${NC}"
    echo -e "${YELLOW}Press Ctrl+C to exit${NC}"
    arduino-cli monitor -p "$PORT" -c baudrate=$BAUD
}

case "${1:-all}" in
    compile)
        check_board
        check_libraries
        compile
        ;;
    upload)
        upload
        ;;
    monitor)
        monitor
        ;;
    libs)
        check_libraries
        ;;
    all|"")
        check_board
        check_libraries
        compile
        upload
        echo -e "\n${GREEN}Done! Run './deploy.sh monitor' to see output${NC}"
        ;;
    *)
        echo "Usage: $0 [compile|upload|monitor|libs|all]"
        echo ""
        echo "  compile  - Compile only"
        echo "  upload   - Upload only (must compile first)"
        echo "  monitor  - Serial monitor"
        echo "  libs     - Install required libraries"
        echo "  all      - Compile and upload (default)"
        exit 1
        ;;
esac
