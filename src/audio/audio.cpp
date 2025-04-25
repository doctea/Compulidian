#include "audio/audio.h"
#include "computer.h"

#include <atomic>

extern std::atomic<bool> started;

/* sample player stuff starts */

voice_t voice[NUM_VOICES] = {
  0,      // default voice 0 assignment - typically a kick but you can map them any way you want
  250,  // initial level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  1,      // default voice 1 assignment 
  250,
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  2,    // default voice 2 assignment 
  250, // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  3,    // default voice 3 assignment 
  250, // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  4,    // default voice 4 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  5,    // default voice 5 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  6,    // default voice 6 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  10,    // default voice 7 assignment 
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch 
};  


#include "audio/samps.h"

void setup_samples() {
    for (int i=0; i< NUM_VOICES; ++i) { // silence all voices by setting sampleindex to last sample
        voice[i].sampleindex=sample[voice[i].sample].samplesize<<12; // sampleindex is a 20:12 fixed point number
    } 
}


void dacWrite(int lchan, int rchan) { 
    //lchan >>= 7;
    //rchan >>= 7;

    //Serial.printf("dacWrite(%i, %i)\n", lchan, rchan); Serial.flush();

    uint16_t DAC_dataL = DAC_config_chan_A_gain | (lchan & 0xFFF);
    uint16_t DAC_dataR = DAC_config_chan_B_gain | (rchan & 0xFFF);
  
    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
    digitalWrite(DAC_CS, LOW); // Select the SPI device
  
    SPI.transfer(DAC_dataL >> 8); // Send first byte
    SPI.transfer(DAC_dataL & 0xFF); // Send second byte
  
    digitalWrite(DAC_CS, HIGH); // Deselect the SPI device
    SPI.endTransaction(); // End SPI transaction
  
    SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
    digitalWrite(DAC_CS, LOW); // Select the SPI device
  
    SPI.transfer(DAC_dataR >> 8); // Send first byte
    SPI.transfer(DAC_dataR & 0xFF); // Send second byte
  
    digitalWrite(DAC_CS, HIGH); // Deselect the SPI device
    SPI.endTransaction(); // End SPI transaction
  
}
/*

void setup1() {

    while(!Serial) {}
    
    // Setup SPI
    //SPI.setCS(DAC_CS);
    SPI.begin(); // Initialize the SPI bus
  
    pinMode(DAC_CS, OUTPUT); // Set the Chip Select pin as an output
    digitalWrite(DAC_CS, HIGH); // Deselect the SPI device to start
  
    //pass settings to SPI class
    //SPI.beginTransaction(SPISettings(20000000, MSBFIRST, SPI_MODE0)); // Start SPI transaction
    //SPI.endTransaction();
  
}
  
// second core calculates samples and sends to DAC
void loop1(){
    while (!started) {
      Serial.println("loop1() - waiting to start");
      return;
    } 
    //Serial.println("loop1() - looped!");
  
    //Serial.println("loop1()");
    //delay(500);
  
    //return;
  
    int32_t newsample,samplesum=0;
    uint32_t index;
    int16_t samp0,samp1,delta,tracksample;
  
   // oct 22 2023 resampling code
  // to change pitch we step through the sample by .5 rate for half pitch up to 2 for double pitch
  // sample.sampleindex is a fixed point 20:12 fractional number
  // we step through the sample array by sampleincrement - sampleincrement is treated as a 1 bit integer and a 12 bit fraction
  // for sample lookup sample.sampleindex is converted to a 20 bit integer which limits the max sample size to 2**20 or about 1 million samples, about 45 seconds @22khz
    // oct 24/2023 - scan through voices instead of sample array
    // faster because there are only 8 voices vs typically 45 or more samples
  
    for (int track = 0 ; track < NUM_VOICES ; ++track) {  // look for samples that are playing, scale their volume, and add them up
      tracksample=voice[track].sample; // precompute for a little more speed below
      index=voice[track].sampleindex>>12; // get the integer part of the sample increment
      if (index <= sample[tracksample].samplesize) { // if sample is playing, do interpolation   
        //Serial.printf("track %i is playing sample %i\n", track, tracksample); Serial.flush();
        samp0=sample[tracksample].samplearray[index]; // get the first sample to interpolate
        samp1=sample[tracksample].samplearray[index+1];// get the second sample
        delta=samp1-samp0;
        newsample=(int32_t)samp0+((int32_t)delta*((int32_t)voice[track].sampleindex & 0x0fff))/4096; // interpolate between the two samples
        samplesum+=(newsample*voice[track].level); // changed to MIDI velocity levels 0-127
        voice[track].sampleindex+=voice[track].sampleincrement; // add step increment
      }
    }
  
  
    samplesum=samplesum>>7;  // adjust for volume multiply above
    if  (samplesum>32767) samplesum=32767; // clip if sample sum is too large
    if  (samplesum<-32767) samplesum=-32767;
  
    //const uint16_t samplesum16 = DAC_config_chan_A_gain | (samplesum & 0xFFF); // convert to 16 bit signed int for DAC
  
    #ifdef MONITOR_CPU1  
        digitalWrite(CPU_USE,0); // low - CPU not busy
    #endif
   // write samples to DMA buffer - this is a blocking call so it stalls when buffer is full
      //DAC.write(int16_t(samplesum)); // left
    //dacWrite(0, int(samplesum)); // left
    //dacWrite(1, int(samplesum)); // left
  
    //Serial.println(samplesum);
  
    dacWrite(samplesum, samplesum); // left and right
  
    //return;
    uint16_t ret;
  
    #ifdef MONITOR_CPU1
        digitalWrite(CPU_USE,1); // hi = CPU busy
    #endif
}
*/



#include "ComputerCard.h"
#include <cmath>


class SineWaveLookup : public ComputerCard
{
public:
	// 512-point (9-bit) lookup table
	// If memory was a concern we could reduce this to ~1/4 of the size,
	// by exploiting symmetry of sine wave, but this only uses 2KB of ~250KB on the RP2040
	constexpr static unsigned tableSize = 512;
	int16_t sine[tableSize];

	// Bitwise AND of index integer with tableMask will wrap it to table size
	constexpr static uint32_t tableMask = tableSize - 1;

	// Sine wave phase (0-2^32 gives 0-2pi phase range)
	uint32_t phase;
	
	SineWaveLookup()
	{
		// Initialise phase of sine wave to 0
		phase = 0;
		
		for (unsigned i=0; i<tableSize; i++)
		{
			// just shy of 2^15 * sin
			sine[i] = int16_t(32000*sin(2*i*M_PI/double(tableSize)));
		}

	}
	
	virtual void ProcessSample()
	{
		uint32_t index = phase >> 23; // convert from 32-bit phase to 9-bit lookup table index
		int32_t r = (phase & 0x7FFFFF) >> 7; // fractional part is last 23 bits of phase, shifted to 16-bit 

		// Look up this index and next index in lookup table
		int32_t s1 = sine[index];
		int32_t s2 = sine[(index+1) & tableMask];

		// Linear interpolation of s1 and s2, using fractional part
		// Shift right by 20 bits
		// (16 bits of r, and 4 bits to reduce 16-bit signed sine table to 12-bit output)
		int32_t out = (s2 * r + s1 * (65536 - r)) >> 20;

		AudioOut1(out);
		AudioOut2(out);
		
		// Want 440Hz sine wave
		// Phase is a 32-bit integer, so 2^32 steps
		// We will increment it at 48kHz, and want it to wrap at 440Hz
		// 
		// Increment = 2^32 * freq / samplerate
		//           = 2^32 * 440 / 48000
		//           = 39370533.54666...
		//          ~= 39370534
		phase += 39370534;
	}
};


void setup()
{
	SineWaveLookup sw;
    sw.Run();
}

void loop() {

}