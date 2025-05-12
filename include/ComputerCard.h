/*
ComputerCard  - by Chris Johnson

ComputerCard is a header-only C++ library, providing a class that
manages the hardware aspects of the Music Thing Modular Workshop
System Computer.

It aims to present a very simple C++ interface for card programmers 
to use the jacks, knobs, switch and LEDs, for programs running at
a fixed 48kHz audio sample rate.

See examples/ directory
*/


#ifndef COMPUTERCARD_H
#define COMPUTERCARD_H

#include "hardware/gpio.h"
#include "hardware/pwm.h"

#define PULSE_1_RAW_OUT 8
#define PULSE_2_RAW_OUT 9

#define CV_OUT_1 23
#define CV_OUT_2 22

// USB host status pin
#define USB_HOST_STATUS 20

class ComputerCard
{
	constexpr static uint8_t leds[] = { 10, 11, 12, 13, 14, 15 };
public:
	constexpr static int numLeds = sizeof(leds) / sizeof(leds[0]);

	/// Knob index, used by KnobVal
	enum Knob {Main, X, Y};
	/// Switch position, used by SwitchVal
	enum Switch {Down, Middle, Up};
	/// Input jack socket, used by Connected and Disconnected
	enum Input {Audio1, Audio2, CV1, CV2, Pulse1, Pulse2};
	/// Hardware version
	enum HardwareVersion_t {Proto1=0x2a, Proto2_Rev1=0x30, Rev1_1=0x0C, Unknown=0xFF};
	/// USB Power state
	enum USBPowerState_t {DFP, UFP, Unsupported};
	
	ComputerCard();

	/** \brief Start audio processing.

        The Run method starts audio processing, calling ProcessSample using an interrupt.
        Run is a blocking function (it never returns)
	*/
	void Run()
	{
		ComputerCard::thisptr = this;
		AudioWorker();
	}

	/// Use before Run() to enable Connected/Disconnected detection
	void EnableNormalisationProbe() {useNormProbe = true;}

protected:
	/// Callback, called once per sample at 48kHz
	virtual void ProcessSample() = 0;



public:
	/// Read knob position (returns 0-4095)
	int32_t __not_in_flash_func(KnobVal)(Knob ind) {return knobs[ind];}

	/// Read switch position
	Switch __not_in_flash_func(SwitchVal)() {return switchVal;}

//protected:
	/// Read switch position
	bool __not_in_flash_func(SwitchChanged)() {return switchVal != lastSwitchVal;}


	/// Set Audio output (values -2048 to 2047)
	void __not_in_flash_func(AudioOut)(int i, int16_t val)
	{
		dacOut[i] = val;
	}
	
	/// Set Audio 1 output (values -2048 to 2047)
	void __not_in_flash_func(AudioOut1)(int16_t val)
	{
		dacOut[0] = val;
	}
	
	/// Set Audio 2 output (values -2048 to 2047)
	void __not_in_flash_func(AudioOut2)(int16_t val)
	{
		dacOut[1] = val;
	}

	
	/// Set CV output (values -2048 to 2047)
	void __not_in_flash_func(CVOut)(int i, int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_1 - i, (2047-val)>>1);
	}
	
	/// Set CV 1 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut1)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_1, (2047-val)>>1);
	}
	
	/// Set CV 2 output (values -2048 to 2047)
	void __not_in_flash_func(CVOut2)(int16_t val)
	{
		pwm_set_gpio_level(CV_OUT_2, (2047-val)>>1);
	}

	/// Set CV 1 output from calibrated MIDI note number (values 0 to 127)
	void __not_in_flash_func(CVOutMIDINote)(int i, uint8_t noteNum)
	{
		pwm_set_gpio_level(CV_OUT_1 - i, MIDIToDac(noteNum, 0) >> 8);
	}
	
	/// Set CV 1 output from calibrated MIDI note number (values 0 to 127)
	void __not_in_flash_func(CVOut1MIDINote)(uint8_t noteNum)
	{
		pwm_set_gpio_level(CV_OUT_1, MIDIToDac(noteNum, 0) >> 8);
	}
	
	/// Set CV 2 output from calibrated MIDI note number (values 0 to 127)
	void __not_in_flash_func(CVOut2MIDINote)(uint8_t noteNum)
	{
		pwm_set_gpio_level(CV_OUT_2, MIDIToDac(noteNum, 1) >> 8);
	}
	
	/// Set Pulse output (true = on)
	void __not_in_flash_func(PulseOut)(int i, bool val)
	{
		gpio_put(PULSE_1_RAW_OUT + i, !val);
	}
	
	/// Set Pulse 1 output (true = on)
	void __not_in_flash_func(PulseOut1)(bool val)
	{
		gpio_put(PULSE_1_RAW_OUT, !val);
	}
	
	/// Set Pulse 2 output (true = on)
	void __not_in_flash_func(PulseOut2)(bool val)
	{
		gpio_put(PULSE_2_RAW_OUT, !val);
	}
	
	public:
	/// Return audio in (-2048 to 2047)
	int16_t __not_in_flash_func(AudioIn)(int i){return i?adcInR:adcInL;}
	
	/// Return audio in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(AudioIn1)(){return adcInL;}

	/// Return audio in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(AudioIn2)(){return adcInR;}

	/// Return CV in (-2048 to 2047)
	int16_t __not_in_flash_func(CVIn)(int i){return cv[i];}
	
	/// Return CV in 1 (-2048 to 2047)
	int16_t __not_in_flash_func(CVIn1)(){return cv[0];}

	/// Return CV in 2 (-2048 to 2047)
	int16_t __not_in_flash_func(CVIn2)(){return cv[1];}

	/// Read pulse in
	bool __not_in_flash_func(PulseIn)(int i){return pulse[i];}
	/// Return true for one sample on pulse rising edge
	bool __not_in_flash_func(PulseInRisingEdge)(int i){return pulse[i] && !last_pulse[i];}
	/// Return true for one sample on pulse falling edge
	bool __not_in_flash_func(PulseInFallingEdge)(int i){return !pulse[i] && last_pulse[i];}

	/// Read pulse in 1
	bool __not_in_flash_func(PulseIn1)(){return pulse[0];}
	/// Return true for one sample on pulse 1 rising edge
	bool __not_in_flash_func(PulseIn1RisingEdge)(){return pulse[0] && !last_pulse[0];}
	/// Return true for one sample on pulse 1 falling edge
	bool __not_in_flash_func(PulseIn1FallingEdge)(){return !pulse[0] && last_pulse[0];}

	/// Read pulse in 2
	bool __not_in_flash_func(PulseIn2)(){return pulse[1];}
	/// Return true for one sample on pulse 2 falling edge
	bool __not_in_flash_func(PulseIn2FallingEdge)(){return !pulse[1] && last_pulse[1];}
	/// Return true for one sample on pulse 2 rising edge
	bool __not_in_flash_func(PulseIn2RisingEdge)(){return pulse[1] && !last_pulse[1];}


	/// Return true if jack connected to input
	bool __not_in_flash_func(Connected)(Input i){return connected[i];}
	/// Return true if no jack connected to input
	bool __not_in_flash_func(Disconnected)(Input i){return !connected[i];}


	/// Set LED brightness, values 0-4095
	// Led numbers are:
	// 0 1
	// 2 3
	// 4 5
	void __not_in_flash_func(LedBrightness)(uint32_t index, uint16_t value)
	{
		pwm_set_gpio_level(leds[index], (value*value)>>8);
	}
	
	/// Turn LED on/off
	void __not_in_flash_func(LedOn)(uint32_t index, bool value = true)
	{
		pwm_set_gpio_level(leds[index], value?65535:0);
	}

	/// Turn LED off
	void __not_in_flash_func(LedOff)(uint32_t index)
	{
		pwm_set_gpio_level(leds[index], 0);
	}

	// Return power state of USB port
	USBPowerState_t USBPowerState()
	{
		if (HardwareVersion() != Rev1_1)
			return Unsupported;
		else if (gpio_get(USB_HOST_STATUS))
			return UFP;
		else
			return DFP;
	}

	/// Return hardware version
	HardwareVersion_t HardwareVersion() {return hw;}

	/// Return ID number unique to flash card
	uint64_t UniqueCardID()	{return uniqueID;}
	
	static ComputerCard *ThisPtr() {return thisptr;}

	
	void Abort();
	
private:
	
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

	static constexpr int calMaxChannels = 2;
	static constexpr int calMaxPoints = 10;
	
	uint8_t numCalibrationPoints[calMaxChannels];
	CalPoint calibrationTable[calMaxChannels][calMaxPoints];
	CalCoeffs calCoeffs[calMaxChannels];

	uint64_t uniqueID;
	
	uint8_t ReadByteFromEEPROM(unsigned int eeAddress);
	int ReadIntFromEEPROM(unsigned int eeAddress);
	uint16_t CRCencode(const uint8_t *data, int length);
	void CalcCalCoeffs(int channel);
	int ReadEEPROM();
	uint32_t MIDIToDac(int midiNote, int channel);
	
	HardwareVersion_t hw;
	HardwareVersion_t ProbeHardwareVersion();
	
	int16_t dacOut[2];
	
	volatile int32_t knobs[4] = { 0, 0, 0, 0 }; // 0-4095
	volatile bool pulse[2] = { 0, 0 };
	volatile bool last_pulse[2] = { 0, 0 };
	volatile int32_t cv[2] = { 0, 0 }; // -2047 - 2048
	volatile int16_t adcInL = 0x800, adcInR = 0x800;

	volatile uint8_t mxPos = 0; // external multiplexer value

	volatile int32_t plug_state[6] = {0,0,0,0,0,0};
	volatile bool connected[6] = {0,0,0,0,0,0};
	bool useNormProbe;

	Switch switchVal, lastSwitchVal;
	
	volatile uint8_t runADCMode;


// Buffers that DMA reads into / out of
	uint16_t ADC_Buffer[2][8];
	uint16_t SPI_Buffer[2][2];

	uint8_t adc_dma, spi_dma; // DMA ids



	uint8_t dmaPhase = 0;

	// Convert signed int16 value into data string for DAC output
	uint16_t __not_in_flash_func(dacval)(int16_t value, uint16_t dacChannel)
	{
		if (value<-2048) value = -2048;
		if (value > 2047) value = 2047;
		return (dacChannel | 0x3000) | (((uint16_t)((value & 0x0FFF) + 0x800)) & 0x0FFF);
	}
	uint32_t next_norm_probe();

	void BufferFull();

	void AudioWorker();
	
	static void AudioCallback()
	{
		thisptr->BufferFull();
	}
	static ComputerCard *thisptr;

};


#ifndef COMPUTERCARD_NOIMPL


#include "hardware/adc.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/flash.h"
#include "hardware/i2c.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

// Input normalisation probe pin
#define NORMALISATION_PROBE 4

// Mux pins
#define MX_A 24
#define MX_B 25

// ADC input pins
#define AUDIO_L_IN_1 27
#define AUDIO_R_IN_1 26
#define MUX_IO_1 28
#define MUX_IO_2 29

#define DAC_CHANNEL_A 0x0000
#define DAC_CHANNEL_B 0x8000

#define DAC_CS 21
#define DAC_SCK 18
#define DAC_TX 19

#define EEPROM_SDA 16
#define EEPROM_SCL 17

#define PULSE_1_INPUT 2
#define PULSE_2_INPUT 3

#define DEBUG_1 0
#define DEBUG_2 1

#define SPI_PORT spi0
#define SPI_DREQ DREQ_SPI0_TX


#define BOARD_ID_0 7
#define BOARD_ID_1 6
#define BOARD_ID_2 5

// The ADC (/DMA) run mode, used to stop DMA in a known state before writing to flash
#define RUN_ADC_MODE_RUNNING 0
#define RUN_ADC_MODE_REQUEST_ADC_STOP 1
#define RUN_ADC_MODE_ADC_STOPPED 2
#define RUN_ADC_MODE_REQUEST_ADC_RESTART 3


#define EEPROM_ADDR_ID 0
#define EEPROM_ADDR_VERSION 2
#define EEPROM_ADDR_CRC_L 87
#define EEPROM_ADDR_CRC_H 86
#define EEPROM_VAL_ID 2001
#define EEPROM_NUM_BYTES 88

#define EEPROM_PAGE_ADDRESS 0x50


#endif

#endif
