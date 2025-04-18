// pinched and adapted from 20_reverb

#ifndef COMPUTER_H
#define COMPUTER_H

#ifdef ENABLE_UART_DEBUGGING
// Use debug pins as UART
#include <stdio.h>
#define debug(f_, ...) printf((f_), __VA_ARGS__)
#define debugp(f_) printf(f_)
#define debug_pin(pin_, val_)
#elifdef ENABLE_GPIO_DEBUGGING
// Use UART pins as GPIO outputs
#define debug(f_, ...)
#define debugp(f_)
#define debug_pin(pin_, val_) gpio_put(pin_, val_)
#else
// No debugging
#define debug(f_, ...)
#define debugp(f_)
#define debug_pin(pin_, val_)
#endif

#include <hardware/gpio.h>
#include <hardware/i2c.h>
#include <hardware/adc.h>
#include <hardware/spi.h>
#include <hardware/pwm.h>

// Input normalisation probe
#define NORMALISATION_PROBE 4

// Mux pins
#define MX_A 24
#define MX_B 25

// ADC input pins
#define AUDIO_L_IN_1 26
#define AUDIO_R_IN_1 27
#define MUX_IO_1 28
#define MUX_IO_2 29

#define NUM_LEDS 6
#define LED1 10
#define LED2 11
#define LED3 12
#define LED4 13
#define LED5 14
#define LED6 15
extern uint leds[NUM_LEDS];

#define DAC_CHANNEL_A 0x0000
#define DAC_CHANNEL_B 0x8000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

#define EEPROM_SDA 16
#define EEPROM_SCL 17

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define KNOB_MAIN 0
#define KNOB_X 1
#define KNOB_Y 2
#define KNOB_SWITCH 3


#define CV_OUT_1 23
#define CV_OUT_2 22


#define DEBUG_1 0
#define DEBUG_2 1

#define SPI_PORT spi0
#define SPI_DREQ DREQ_SPI0_TX


#define BOARD_ID_0 7
#define BOARD_ID_1 6
#define BOARD_ID_2 5
#define BOARD_PROTO_1 0x2a
#define BOARD_PROTO_2_0 0x30

uint8_t GetBoardID();

#define EEPROM_ADDR_ID 0
#define EEPROM_ADDR_VERSION 2
#define EEPROM_ADDR_CRC_L 87
#define EEPROM_ADDR_CRC_H 86
#define EEPROM_VAL_ID 2001
#define EEPROM_NUM_BYTES 88

#define CAL_MAX_CHANNELS 2
#define CAL_MAX_POINTS 10
extern uint8_t eepromPageAddress;


typedef struct
{
	float m, b;
	int32_t mi, bi;
} CalCoeffs;

typedef struct
{
	int32_t dacSetting;
	int8_t voltage;
} CalPoint;

extern uint8_t numCalibrationPoints[CAL_MAX_CHANNELS];
extern CalPoint calibrationTable[CAL_MAX_CHANNELS][CAL_MAX_POINTS];
extern CalCoeffs calCoeffs[CAL_MAX_CHANNELS];

// Read a byte from EEPROM
uint8_t ReadByteFromEEPROM(unsigned int eeAddress);

// Read a 16-bit integer from EEPROM
int ReadIntFromEEPROM(unsigned int eeAddress);
uint16_t CRCencode(const uint8_t *data, int length);


uint32_t midiToDac(int midiNote, int channel);

void CalcCalCoeffs(int channel);

int ReadEEPROM();

void SetupComputerIO();
#endif
