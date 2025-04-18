#include "computer.h"


uint8_t eepromPageAddress = 0x50;

uint8_t numCalibrationPoints[CAL_MAX_CHANNELS];
CalPoint calibrationTable[CAL_MAX_CHANNELS][CAL_MAX_POINTS];
CalCoeffs calCoeffs[CAL_MAX_CHANNELS];

uint leds[NUM_LEDS] = { LED1, LED2, LED3, LED4, LED5, LED6 };

uint8_t GetBoardID()
{
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	sleep_us(1);
	uint8_t pd = gpio_get(BOARD_ID_0) | (gpio_get(BOARD_ID_1) << 2) | (gpio_get(BOARD_ID_2) << 4);
	gpio_set_pulls(BOARD_ID_0, true, false);
	gpio_set_pulls(BOARD_ID_1, true, false);
	gpio_set_pulls(BOARD_ID_2, true, false);
	sleep_us(1);
	uint8_t pu = (gpio_get(BOARD_ID_0) << 1) | (gpio_get(BOARD_ID_1) << 3) | (gpio_get(BOARD_ID_2) << 5);
	gpio_set_pulls(BOARD_ID_0, false, true);
	gpio_set_pulls(BOARD_ID_1, false, true);
	gpio_set_pulls(BOARD_ID_2, false, true);
	return pd | pu;
}

uint8_t ReadByteFromEEPROM(unsigned int eeAddress)
{
	uint8_t deviceAddress = eepromPageAddress | ((eeAddress >> 8) & 0x0F);
	uint8_t data = 0xFF;

	uint8_t addr_low_byte = eeAddress & 0xFF;
	i2c_write_blocking(i2c0, deviceAddress, &addr_low_byte, 1, false);

	i2c_read_blocking(i2c0, deviceAddress, &data, 1, false);
	return data;
}

int ReadIntFromEEPROM(unsigned int eeAddress)
{
	uint8_t highByte = ReadByteFromEEPROM(eeAddress);
	uint8_t lowByte = ReadByteFromEEPROM(eeAddress + 1);
	return (highByte << 8) | lowByte;
}

uint16_t CRCencode(const uint8_t *data, int length)
{
	uint16_t crc = 0xFFFF;
	for (int i = 0; i < length; i++)
	{
		crc ^= ((uint16_t)data[i]) << 8;
		for (uint8_t bit = 0; bit < 8; bit++)
		{
			if (crc & 0x8000)
			{
				crc = (crc << 1) ^ 0x1021;
			}
			else
			{
				crc = crc << 1;
			}
		}
	}
	return crc;
}

uint32_t midiToDac(int midiNote, int channel)
{
	int32_t dacValue = ((calCoeffs[channel].mi * (midiNote - 60)) >> 4) + calCoeffs[channel].bi;
	if (dacValue > 524287) dacValue = 524287;
	if (dacValue < 0) dacValue = 0;
	return dacValue;
}

void CalcCalCoeffs(int channel)
{
	float sumV = 0.0;
	float sumDAC = 0.0;
	float sumV2 = 0.0;
	float sumVDAC = 0.0;
	int N = numCalibrationPoints[channel];

	for (int i = 0; i < N; i++)
	{
		float v = calibrationTable[channel][i].voltage * 0.1;
		float dac = calibrationTable[channel][i].dacSetting;
		sumV += v;
		sumDAC += dac;
		sumV2 += v * v;
		sumVDAC += v * dac;
	}

	float denominator = N * sumV2 - sumV * sumV;
	if (denominator != 0)
	{
		calCoeffs[channel].m = (N * sumVDAC - sumV * sumDAC) / denominator;
	}
	else
	{
		calCoeffs[channel].m = 0.0;
	}
	calCoeffs[channel].b = (sumDAC - calCoeffs[channel].m * sumV) / N;

	calCoeffs[channel].mi = (calCoeffs[channel].m * 1.333333333333333f + 0.5f);
	calCoeffs[channel].bi = calCoeffs[channel].b + 0.5f;
	debug("%d %f %f\n", channel, calCoeffs[channel].m, calCoeffs[channel].b);
}

int ReadEEPROM()
{
	calibrationTable[0][0].voltage = -20;
	calibrationTable[0][0].dacSetting = 347700;
	calibrationTable[0][1].voltage = 0;
	calibrationTable[0][1].dacSetting = 261200;
	calibrationTable[0][2].voltage = 20;
	calibrationTable[0][2].dacSetting = 174400;

	calibrationTable[1][0].voltage = -20;
	calibrationTable[1][0].dacSetting = 347700;
	calibrationTable[1][1].voltage = 0;
	calibrationTable[1][1].dacSetting = 261200;
	calibrationTable[1][2].voltage = 20;
	calibrationTable[1][2].dacSetting = 174400;

	if (ReadIntFromEEPROM(EEPROM_ADDR_ID) != EEPROM_VAL_ID)
	{
		debugp("Failed to read EEPROM ID\n");
		return 1;
	}
	uint8_t buf[EEPROM_NUM_BYTES];
	for (int i = 0; i < EEPROM_NUM_BYTES; i++)
	{
		buf[i] = ReadByteFromEEPROM(i);
	}

	int eepMajor = (buf[EEPROM_ADDR_VERSION] >> 4) & 0x0F;
	int eepMinor = (buf[EEPROM_ADDR_VERSION] >> 2) & 0x03;
	int eepPoint = buf[EEPROM_ADDR_VERSION] & 0x03;
	debug("EEPROM version %d.%d.%d\n", eepMajor, eepMinor, eepPoint);

	uint16_t calculatedCRC = CRCencode(buf, 86);
	uint16_t foundCRC = ((uint16_t)buf[EEPROM_ADDR_CRC_H] << 8) | buf[EEPROM_ADDR_CRC_L];

	if (calculatedCRC != foundCRC)
	{
		debugp("EEPROM CRC check failed\n");
		return 1;
	}

	int bufferIndex = 4;

	for (uint8_t channel = 0; channel < CAL_MAX_CHANNELS; channel++)
	{
		int channelOffset = bufferIndex + (41 * channel);
		numCalibrationPoints[channel] = buf[channelOffset++];
		for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++)
		{
			int8_t targetVoltage = (int8_t)buf[channelOffset++];
			uint32_t dacSetting = 0;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 24;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 16;
			dacSetting |= ((uint32_t)buf[channelOffset++]) << 8;
			dacSetting |= ((uint32_t)buf[channelOffset++]);

			calibrationTable[channel][point].voltage = targetVoltage;
			calibrationTable[channel][point].dacSetting = dacSetting;
		}
		CalcCalCoeffs(channel);
	}
	for (uint8_t channel = 0; channel < CAL_MAX_CHANNELS; channel++)
	{
		for (uint8_t point = 0; point < numCalibrationPoints[channel]; point++)
		{
			debug("%d %d %d %d\n",
			      channel,
			      point,
			      calibrationTable[channel][point].voltage,
			      calibrationTable[channel][point].dacSetting);
		}
	}
	return 0;
}

void SetupComputerIO()
{
	for (int i = 0; i < NUM_LEDS; i++)
	{
		gpio_init(leds[i]);
		gpio_set_dir(leds[i], GPIO_OUT);
	}

	gpio_set_dir(BOARD_ID_0, GPIO_IN);
	gpio_set_dir(BOARD_ID_1, GPIO_IN);
	gpio_set_dir(BOARD_ID_2, GPIO_IN);

	gpio_init(NORMALISATION_PROBE);
	gpio_set_dir(NORMALISATION_PROBE, GPIO_OUT);
	gpio_put(NORMALISATION_PROBE, false);

	adc_init();

	adc_gpio_init(AUDIO_L_IN_1);
	adc_gpio_init(AUDIO_R_IN_1);
	adc_gpio_init(MUX_IO_1);
	adc_gpio_init(MUX_IO_2);

	gpio_init(MX_A);
	gpio_init(MX_B);
	gpio_set_dir(MX_A, GPIO_OUT);
	gpio_set_dir(MX_B, GPIO_OUT);

	gpio_init(PULSE_1_RAW_OUT);
	gpio_set_dir(PULSE_1_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_1_RAW_OUT, true);

	gpio_init(PULSE_2_RAW_OUT);
	gpio_set_dir(PULSE_2_RAW_OUT, GPIO_OUT);
	gpio_put(PULSE_2_RAW_OUT, true);

	gpio_init(PULSE_1_INPUT);
	gpio_set_dir(PULSE_1_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_1_INPUT);

	gpio_init(PULSE_2_INPUT);
	gpio_set_dir(PULSE_2_INPUT, GPIO_IN);
	gpio_pull_up(PULSE_2_INPUT);

	spi_init(SPI_PORT, 15625000);
	spi_set_format(SPI_PORT, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
	gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
	gpio_set_function(DAC_TX, GPIO_FUNC_SPI);
	gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

	i2c_init(i2c0, 100 * 1000);
	gpio_set_function(EEPROM_SDA, GPIO_FUNC_I2C);
	gpio_set_function(EEPROM_SCL, GPIO_FUNC_I2C);

	gpio_set_function(CV_OUT_1, GPIO_FUNC_PWM);
	gpio_set_function(CV_OUT_2, GPIO_FUNC_PWM);

	pwm_config config = pwm_get_default_config();
	pwm_config_set_wrap(&config, 2047);

	pwm_init(pwm_gpio_to_slice_num(CV_OUT_1), &config, true);
	pwm_init(pwm_gpio_to_slice_num(CV_OUT_2), &config, true);

	pwm_set_gpio_level(CV_OUT_1, 1024);
	pwm_set_gpio_level(CV_OUT_2, 1024);

#ifndef ENABLE_UART_DEBUGGING
	gpio_init(DEBUG_1);
	gpio_set_dir(DEBUG_1, GPIO_OUT);

	gpio_init(DEBUG_2);
	gpio_set_dir(DEBUG_2, GPIO_OUT);
#endif
}