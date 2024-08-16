// Wokwi Custom Chip - For docs and examples see:
// https://docs.wokwi.com/chips-api/getting-started
//
// SPDX-License-Identifier: MIT
// Copyright 2024 Andy Duncan

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

#define MCP9808_REG_CONFIG 0x01      ///< MCP9808 config register
#define MCP9808_REG_UPPER_TEMP 0x02   ///< upper alert boundary
#define MCP9808_REG_LOWER_TEMP 0x03   ///< lower alert boundery
#define MCP9808_REG_CRIT_TEMP 0x04    ///< critical temperature
#define MCP9808_REG_AMBIENT_TEMP 0x05 ///< ambient temperature
#define MCP9808_REG_MANUF_ID 0x06     ///< manufacture ID
#define MCP9808_REG_DEVICE_ID 0x07    ///< device ID
#define MCP9808_REG_RESOLUTION 0x08   ///< resolution

#define NUM_REGISTERS 9

const int I2C_BASE_ADDRESS = 0x18;

#define NUM_ADDR_BITS 3

typedef struct {
    char *name;
    bool readOnly;
    uint8_t totalBytes;
    uint16_t value;
} register_t;

typedef struct {
    pin_t addressPins[NUM_ADDR_BITS];
    uint32_t temperature;
    uint32_t lastReadingTime;
    register_t *currentRegister;
    uint8_t currentByte;
    register_t registers[NUM_REGISTERS];
} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);

uint8_t read_address(chip_state_t* chip) {
    uint8_t configuredAddress = I2C_BASE_ADDRESS;
    uint8_t lsbits = 0;

    for (uint8_t i=0; i < NUM_ADDR_BITS; i++) {
        if (pin_read(chip->addressPins[i])) {
            lsbits |= (1 << i);
        }
    }

    configuredAddress = configuredAddress | lsbits;

    printf("Chip LSB bits set to %i.  Address is 0x%02x\n", lsbits, configuredAddress);

    return configuredAddress;
}

void setupRegisters(chip_state_t *chip) {
    chip->registers[MCP9808_REG_CONFIG].name = "CONFIG";
    chip->registers[MCP9808_REG_CONFIG].readOnly = false;
    chip->registers[MCP9808_REG_CONFIG].totalBytes = 2;
    chip->registers[MCP9808_REG_CONFIG].value = 0;

    chip->registers[MCP9808_REG_UPPER_TEMP].name = "UPPER_TEMP";
    chip->registers[MCP9808_REG_UPPER_TEMP].readOnly = false;
    chip->registers[MCP9808_REG_UPPER_TEMP].totalBytes = 2;
    chip->registers[MCP9808_REG_UPPER_TEMP].value = 0;

    chip->registers[MCP9808_REG_LOWER_TEMP].name = "LOWER_TEMP";
    chip->registers[MCP9808_REG_LOWER_TEMP].readOnly = false;
    chip->registers[MCP9808_REG_LOWER_TEMP].totalBytes = 2;
    chip->registers[MCP9808_REG_LOWER_TEMP].value = 0;

    chip->registers[MCP9808_REG_CRIT_TEMP].name = "CRIT_TEMP";
    chip->registers[MCP9808_REG_CRIT_TEMP].readOnly = false;
    chip->registers[MCP9808_REG_CRIT_TEMP].totalBytes = 2;
    chip->registers[MCP9808_REG_CRIT_TEMP].value = 0;

    chip->registers[MCP9808_REG_AMBIENT_TEMP].name = "AMBIENT_TEMP";
    chip->registers[MCP9808_REG_AMBIENT_TEMP].readOnly = true;
    chip->registers[MCP9808_REG_AMBIENT_TEMP].totalBytes = 2;
    chip->registers[MCP9808_REG_AMBIENT_TEMP].value = 0;

    chip->registers[MCP9808_REG_MANUF_ID].name = "MANUF_ID";
    chip->registers[MCP9808_REG_MANUF_ID].readOnly = true;
    chip->registers[MCP9808_REG_MANUF_ID].totalBytes = 2;
    chip->registers[MCP9808_REG_MANUF_ID].value = 0x0054;

    chip->registers[MCP9808_REG_DEVICE_ID].name = "DEVICE_ID";
    chip->registers[MCP9808_REG_DEVICE_ID].readOnly = true;
    chip->registers[MCP9808_REG_DEVICE_ID].totalBytes = 2;
    chip->registers[MCP9808_REG_DEVICE_ID].value = 0x0400;

    chip->registers[MCP9808_REG_RESOLUTION].name = "RESOLUTION";
    chip->registers[MCP9808_REG_RESOLUTION].readOnly = false;
    chip->registers[MCP9808_REG_RESOLUTION].totalBytes = 1;
    chip->registers[MCP9808_REG_RESOLUTION].value = 0;
}

void chip_init() {
    chip_state_t *chip = malloc(sizeof(chip_state_t));

    const char * addrPinNames[] = {"A0", "A1", "A2"};

    chip->temperature = attr_init_float("temperature", 0.0f);

    chip->lastReadingTime = 0xffff;

    setupRegisters(chip);

    chip->currentRegister = NULL;

    for (uint8_t i=0; i < NUM_ADDR_BITS; i++) {
        chip->addressPins[i] = pin_init(addrPinNames[i], INPUT_PULLDOWN);
    }

    const i2c_config_t i2c_config = {
            .user_data = chip,
            .address = read_address(chip),
            .scl = pin_init("SCL", INPUT_PULLUP),
            .sda = pin_init("SDA", INPUT_PULLUP),
            .connect = on_i2c_connect,
            .read = on_i2c_read,
            .write = on_i2c_write,
            .disconnect = on_i2c_disconnect, // Optional
    };

    i2c_init(&i2c_config);

    printf("Hello from MCP9808 chip!\n");
}

bool on_i2c_connect(void *user_data, uint32_t address, bool read) {
    // printf("i2c connect\n");
    // `address` parameter contains the 7-bit address that was received on the I2C bus.
    // `read` indicates whether this is a read request (true) or write request (false).
    return true; // true means ACK, false NACK
}

uint8_t readRegister(register_t *registerToRead, uint8_t byteToRead) {
    return (registerToRead->value >> (8 * byteToRead)) & 0xff;
}

void writeRegister(register_t *registerToWrite, uint8_t byteToWrite, uint8_t value) {
    registerToWrite->value |= (value << (8 * byteToWrite));
}

uint32_t getResolution(chip_state_t *chip) {
    uint8_t resolution = chip->registers[MCP9808_REG_RESOLUTION].value & 0x0003;

    switch (resolution) {
        case 0:
            return 30;
        case 1:
            return 65;
        case 2:
            return 130;
        case 3:
            return 250;
        default:
            return 0;
    }
}

bool inCriticalAlert(chip_state_t *chip, uint16_t ambientTemp) {
    uint16_t criticalTemp = chip->registers[MCP9808_REG_CRIT_TEMP].value;

    int16_t signedTemp = (int16_t) criticalTemp;

    printf("criticalTemp: %i signedTemp: %d\n", criticalTemp, signedTemp);

    return ambientTemp >= criticalTemp;
}

void sampleTemperature(chip_state_t *chip) {
    uint32_t currentTime = get_sim_nanos() / 1000000;

    uint32_t resolution = getResolution(chip);

    if (currentTime - chip->lastReadingTime < resolution) {
        return;
    }

    chip->lastReadingTime = currentTime;

    float tempTemp = attr_read_float(chip->temperature);

    uint16_t newTemp = (int16_t) (tempTemp * 16);

    newTemp |= (inCriticalAlert(chip, newTemp) << 15); // t_crit flag
    newTemp |= ((90 > 100) << 14); // t_upper flag
    newTemp |= (0 << 13); // t_lower flag

    chip->registers[MCP9808_REG_AMBIENT_TEMP].value = newTemp;
}

uint8_t on_i2c_read(void *user_data) {
    chip_state_t *chip = user_data;

    register_t *currentRegister = chip->currentRegister;

    uint8_t currentByte = chip->currentByte--;

    uint8_t registerValue = readRegister(currentRegister, currentByte);

    printf("Read register %s value %#.4x byte %i value %#.2x\n", currentRegister->name, currentRegister->value, currentByte, registerValue);

    if (currentByte == 0) {
        chip->currentRegister = NULL;
    }

    return registerValue; // The byte to be returned to the microcontroller
}

bool on_i2c_write(void *user_data, uint8_t data) {
    chip_state_t *chip = user_data;

    if (chip->currentRegister) {
        uint8_t currentByte = chip->currentByte--;
        writeRegister(chip->currentRegister, currentByte, data);
        if (currentByte == 0) {
            printf("Write register %s value %#.4x\n", chip->currentRegister->name, chip->currentRegister->value);
            chip->currentRegister = NULL;
        }
    } else {
        register_t *currentRegister = &(chip->registers[data]);

        printf("Select register %s\n", currentRegister->name);

        sampleTemperature(chip);

        chip->currentByte = currentRegister->totalBytes - 1;
        chip->currentRegister = currentRegister;
    }

    return true; // true means ACK, false NACK
}

void on_i2c_disconnect(void *user_data) {
    // This method is optional. Useful if you need to know when the I2C transaction has concluded.
}
