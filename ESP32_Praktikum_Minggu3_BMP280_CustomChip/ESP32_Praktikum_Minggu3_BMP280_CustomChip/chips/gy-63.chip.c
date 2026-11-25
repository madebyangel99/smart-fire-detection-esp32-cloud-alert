// Wokwi Custom Chip - For docs and examples see:
// https://docs.wokwi.com/chips-api/getting-started
//
// SPDX-License-Identifier: MIT
// Copyright 2023

#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

////////////////////////////////////////////////////////////////////////////////
// BMP280_DEV Definitions
////////////////////////////////////////////////////////////////////////////////

#define BMP280_I2C_ADDR 0x77     // The BMP280 I2C address
#define BMP280_I2C_ALT_ADDR 0x76 // The BMP280 I2C alternate address
#define DEVICE_ID 0x58           // The BMP280 device ID
#define RESET_CODE 0xB6          // The BMP280 reset code

enum SPIPort
{
    BMP280_SPI0,
    BMP280_SPI1
};

////////////////////////////////////////////////////////////////////////////////
// BMP280_DEV Registers
////////////////////////////////////////////////////////////////////////////////

enum Registers
{
    BMP280_REGISTER_DIG_T1 = 0x88,
    BMP280_REGISTER_DIG_T2 = 0x8A,
    BMP280_REGISTER_DIG_T3 = 0x8C,
    BMP280_REGISTER_DIG_P1 = 0x8E,
    BMP280_REGISTER_DIG_P2 = 0x90,
    BMP280_REGISTER_DIG_P3 = 0x92,
    BMP280_REGISTER_DIG_P4 = 0x94,
    BMP280_REGISTER_DIG_P5 = 0x96,
    BMP280_REGISTER_DIG_P6 = 0x98,
    BMP280_REGISTER_DIG_P7 = 0x9A,
    BMP280_REGISTER_DIG_P8 = 0x9C,
    BMP280_REGISTER_DIG_P9 = 0x9E,
    BMP280_REGISTER_CHIPID = 0xD0,
    BMP280_REGISTER_VERSION = 0xD1,
    BMP280_REGISTER_SOFTRESET = 0xE0,
    BMP280_REGISTER_CAL26 = 0xE1, /**< R calibration = 0xE1-0xF0 */
    BMP280_REGISTER_STATUS = 0xF3,
    BMP280_REGISTER_CONTROL = 0xF4,
    BMP280_REGISTER_CONFIG = 0xF5,
    BMP280_REGISTER_PRESSUREDATA = 0xF7,
    BMP280_REGISTER_TEMPDATA = 0xFA,
};

////////////////////////////////////////////////////////////////////////////////
// BMP280_DEV Modes
////////////////////////////////////////////////////////////////////////////////

enum Mode
{
    SLEEP_MODE = 0x00, // Device mode bitfield in the control and measurement register
    FORCED_MODE = 0x01,
    NORMAL_MODE = 0x03
};

////////////////////////////////////////////////////////////////////////////////
// BMP280_DEV Register bit field Definitions
////////////////////////////////////////////////////////////////////////////////

enum Oversampling
{
    OVERSAMPLING_SKIP = 0x00, // Oversampling bit fields in the control and measurement register
    OVERSAMPLING_X1 = 0x01,
    OVERSAMPLING_X2 = 0x02,
    OVERSAMPLING_X4 = 0x03,
    OVERSAMPLING_X8 = 0x04,
    OVERSAMPLING_X16 = 0x05
};

enum IIRFilter
{
    IIR_FILTER_OFF = 0x00, // Infinite Impulse Response (IIR) filter bit field in the configuration register
    IIR_FILTER_2 = 0x01,
    IIR_FILTER_4 = 0x02,
    IIR_FILTER_8 = 0x03,
    IIR_FILTER_16 = 0x04
};

enum TimeStandby
{
    TIME_STANDBY_05MS = 0x00, // Time standby bit field in the configuration register
    TIME_STANDBY_62MS = 0x01,
    TIME_STANDBY_125MS = 0x02,
    TIME_STANDBY_250MS = 0x03,
    TIME_STANDBY_500MS = 0x04,
    TIME_STANDBY_1000MS = 0x05,
    TIME_STANDBY_2000MS = 0x06,
    TIME_STANDBY_4000MS = 0x07
};

// /*!
//  *  Struct to hold calibration data.
//  */
// typedef struct
// {
//     uint16_t dig_T1; /**< dig_T1 cal register. */
//     int16_t dig_T2;  /**<  dig_T2 cal register. */
//     int16_t dig_T3;  /**< dig_T3 cal register. */

//     uint16_t dig_P1; /**< dig_P1 cal register. */
//     int16_t dig_P2;  /**< dig_P2 cal register. */
//     int16_t dig_P3;  /**< dig_P3 cal register. */
//     int16_t dig_P4;  /**< dig_P4 cal register. */
//     int16_t dig_P5;  /**< dig_P5 cal register. */
//     int16_t dig_P6;  /**< dig_P6 cal register. */
//     int16_t dig_P7;  /**< dig_P7 cal register. */
//     int16_t dig_P8;  /**< dig_P8 cal register. */
//     int16_t dig_P9;  /**< dig_P9 cal register. */
// } bmp280_calib_data;

// union
// { // Copy of the BMP280's configuration register
//     struct
//     {
//         uint8_t spi3w_en : 1;
//         uint8_t : 1;
//         uint8_t filter : 3;
//         uint8_t t_sb : 3;
//     } bit;
//     uint8_t reg;
// } config = {.reg = 0};

// union
// { // Copy of the BMP280's control and measurement register
//     struct
//     {
//         uint8_t mode : 2;
//         uint8_t osrs_p : 3;
//         uint8_t osrs_t : 3;
//     } bit;
//     uint8_t reg;
// } ctrl_meas = {.reg = 0};

// union
// { // Copy of the BMP280's status register
//     struct
//     {
//         uint8_t im_update : 1;
//         uint8_t : 2;
//         uint8_t measuring : 1;
//     } bit;
//     uint8_t reg;
// } status = {.reg = 0};

typedef struct
{
    uint32_t pressure_attr;
    uint8_t protocol;
    pin_t CSB_pin;
    pin_t PS_pin;
    uint8_t base_address;
    uint8_t registers[255];
    uint32_t dev_address;
    uint32_t spi;
    timer_t timer_id;
    uint8_t spi_buffer[1];

} chip_state_t;

static bool on_i2c_connect(void *user_data, uint32_t address, bool connect);
static uint8_t on_i2c_read(void *user_data);
static bool on_i2c_write(void *user_data, uint8_t data);
static void on_i2c_disconnect(void *user_data);
static void chip_pin_change(void *user_data, pin_t pin, uint32_t value);
static void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count);
static void chip_timer_callback(void *user_data);

void chip_init()
{
    chip_state_t *chip = malloc(sizeof(chip_state_t));
    // chip->CSB_pin = pin_init("CSB", INPUT_PULLDOWN);
    chip->PS_pin = pin_init("PS", INPUT_PULLUP);
    if (pin_read(chip->PS_pin) == HIGH) // I2c Protocol (default)
    {
        // Check to see if CSB pin is tied to Vcc to select alternate device address.
        chip->CSB_pin = pin_init("CSB", INPUT_PULLDOWN);
        if (pin_read(chip->CSB_pin) == LOW)
        {
            chip->dev_address = BMP280_I2C_ADDR;
        }
        else
        {
            chip->dev_address = BMP280_I2C_ALT_ADDR;
        }

        // TODO: Initialize the chip, set up IO pins, create timers, etc.
        const i2c_config_t i2c_config = {
            .user_data = chip,
            .address = chip->dev_address,
            .scl = pin_init("SCL", INPUT),
            .sda = pin_init("SDA", INPUT),
            .connect = on_i2c_connect,
            .read = on_i2c_read,
            .write = on_i2c_write,
            .disconnect = on_i2c_disconnect, // Optional
        };
        i2c_dev_t i2c = i2c_init(&i2c_config);
        printf("I'm using I2c Protocol\n");
    }
    else
    {
        chip->CSB_pin = pin_init("CS", INPUT_PULLUP);

        const pin_watch_config_t watch_config = {
            .edge = BOTH,
            .pin_change = chip_pin_change,
            .user_data = chip,
        };
        pin_watch(chip->CSB_pin, &watch_config);

        const spi_config_t spi_config = {
            .user_data = chip,
            .sck = pin_init("SCK", INPUT),
            .mosi = pin_init("MOSI", INPUT),
            .miso = pin_init("MISO", INPUT),
            .mode = 0,
            .done = chip_spi_done,
        };
        chip->spi = spi_init(&spi_config);
        printf("I'm using SPI Protocol\n");
    }

    const timer_config_t timer_config = {
        .callback = chip_timer_callback,
        .user_data = chip,
    };
    chip->timer_id = timer_init(&timer_config);

    // These are sample trimming values from BMP280 Datasheet Page 23
    chip->registers[0x88] = 0x70; // DIG_T1 LSB 27504
    chip->registers[0x89] = 0x6B; // DIG_T1 MSB
    chip->registers[0x8A] = 0x43; // DIG_T2 LSB 26435
    chip->registers[0x8B] = 0x67; // DIG_T2 MSB
    chip->registers[0x8C] = 0x18; // DIG_T3 LSB -1000
    chip->registers[0x8D] = 0xFC; // DIG_T3 MSB
    chip->registers[0x8E] = 0x7D; // DIG_P1 LSB 36477
    chip->registers[0x8F] = 0x8E; // DIG_P1 MSB

    chip->registers[0x90] = 0x43; // DIG_P2 LSB -10685
    chip->registers[0x91] = 0xD6; // DIG_P2 MSB
    chip->registers[0x92] = 0xD0; // DIG_P3 LSB 3024
    chip->registers[0x93] = 0x0B; // DIG_P3 MSB
    chip->registers[0x94] = 0x27; // DIG_P4 LSB 2855
    chip->registers[0x95] = 0x0B; // DIG_P4 MSB
    chip->registers[0x96] = 0x8C; // DIG_P5 LSB 140
    chip->registers[0x97] = 0x00; // DIG_P5 MSB

    chip->registers[0x98] = 0xF9; // DIG_P6 LSB -7
    chip->registers[0x99] = 0xFF; // DIG_P6 MSB
    chip->registers[0x9A] = 0x8C; // DIG_P7 LSB 15500
    chip->registers[0x9B] = 0x3C; // DIG_P7 MSB
    chip->registers[0x9C] = 0xF8; // DIG_P8 LSB -14600
    chip->registers[0x9D] = 0xC6; // DIG_P8 MSB
    chip->registers[0x9E] = 0x70; // DIG_P9 LSB 6000
    chip->registers[0x9F] = 0x17; // DIG_P9 MSB

    chip->registers[0xD0] = 0x58; // ID
    chip->registers[0xE0] = 0x00; // RESET
    chip->registers[0xF3] = 0x00; // STATUS
    chip->registers[0xF4] = 0x00; // CTRL_MEAS
    chip->registers[0xF5] = 0x00; // CONFIG

    // These are sample measurement values from BMP280 Datasheet Page 23
    chip->registers[0xF7] = 0x65; // PRESS_MSB
    chip->registers[0xF8] = 0x90; // PRESS_LSB
    chip->registers[0xF9] = 0xC0; // PRESS_XLSB
    chip->registers[0xFA] = 0x7E; // TEMP_MSB
    chip->registers[0xFB] = 0xED; // TEMP_LSB
    chip->registers[0xFC] = 0x00; // TEMP_XLSB

    chip->pressure_attr = attr_init("barometricPressure", 1000);

    timer_start(chip->timer_id, 100000, true);
    printf("Hello from GY-63!\n");
}

uint8_t rot13(uint8_t value)
{
    const uint8_t ROT = 13;
    if (value >= 'A' && value <= 'Z')
    {
        return (value + ROT) <= 'Z' ? value + ROT : value - ROT;
    }
    if (value >= 'a' && value <= 'z')
    {
        return (value + ROT) <= 'z' ? value + ROT : value - ROT;
    }
    return value;
}

void chip_pin_change(void *user_data, pin_t pin, uint32_t value)
{
    chip_state_t *chip = (chip_state_t *)user_data;
    // Handle CS pin logic
    if (pin == chip->CSB_pin)
    {
        if (value == LOW)
        {
            printf("SPI chip selected\n");
            chip->spi_buffer[0] = ' '; // Some dummy data for the first character
            spi_start(chip->spi, chip->spi_buffer, sizeof(chip->spi_buffer));
        }
        else
        {
            printf("SPI chip deselected\n");
            spi_stop(chip->spi);
        }
    }
}
void chip_spi_done(void *user_data, uint8_t *buffer, uint32_t count)
{
    chip_state_t *chip = (chip_state_t *)user_data;
    if (!count)
    {
        // This means that we got here from spi_stop, and no data was received
        return;
    }

    // Apply the ROT13 transformation, and store the result in the buffer.
    // The result will be read back during the next SPI transfer.
    buffer[0] = rot13(buffer[0]);

    if (pin_read(chip->CSB_pin) == LOW)
    {
        // Continue with the next character
        spi_start(chip->spi, chip->spi_buffer, sizeof(chip->spi_buffer));
    }
}

bool on_i2c_connect(void *user_data, uint32_t address, bool connect)
{
    // `address` parameter contains the 7-bit address that was received on the I2C bus.
    // `read` indicates whether this is a read request (true) or write request (false).
    return true; // true means ACK, false NACK
}

uint8_t on_i2c_read(void *user_data)
{
    chip_state_t *chip = user_data;
    chip->base_address = chip->base_address + 1;
    printf("Address: %x, Data: %x\n", chip->base_address - 1, chip->registers[chip->base_address - 1]);
    return chip->registers[chip->base_address - 1]; // The byte to be returned to the microcontroller
}

bool on_i2c_write(void *user_data, uint8_t data)
{
    chip_state_t *chip = user_data;

    // `data` is the byte received from the microcontroller
    switch (data)
    {
    case BMP280_REGISTER_SOFTRESET:
        printf("Reset\n");
        chip->base_address = BMP280_REGISTER_SOFTRESET;
        break;
    case BMP280_REGISTER_DIG_T1:
        printf("Params\n");
        chip->base_address = BMP280_REGISTER_DIG_T1;
        break;
    case BMP280_REGISTER_CONFIG:
        printf("Config\n");
        chip->base_address = BMP280_REGISTER_CONFIG;
        break;
    case BMP280_REGISTER_CONTROL:
        printf("Control\n");
        chip->base_address = BMP280_REGISTER_CONTROL;
        break;
    case BMP280_REGISTER_STATUS:
        printf("Status\n");
        chip->base_address = BMP280_REGISTER_STATUS;
        break;
    case BMP280_REGISTER_PRESSUREDATA:
        printf("Pressure Data\n");
        chip->base_address = BMP280_REGISTER_PRESSUREDATA;
        break;
    case BMP280_REGISTER_TEMPDATA:
        printf("Temperature Data\n");
        chip->base_address = BMP280_REGISTER_TEMPDATA;
        break;
    default:
        printf("Write data to register address\n");
        chip->registers[chip->base_address] = data;
        printf("Address: %x, Data: %x\n", chip->base_address, chip->registers[chip->base_address]);
        break;
    }

    return true; // true means ACK, false NACK
}

void on_i2c_disconnect(void *user_data)
{
    // This method is optional. Useful if you need to know when the I2C transaction has concluded.
}

static void chip_timer_callback(void *user_data)
{
    chip_state_t *chip = user_data;
    chip->registers[0xFB] = rand() % 75 + 75;
};