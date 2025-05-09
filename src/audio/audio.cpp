#include "Config.h"

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

  7,    // default voice 7 assignment 
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch 

  8,    // default voice 8 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  9,    // default voice 9 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  10,    // default voice 10 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  11,    // default voice 11 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  12,    // default voice 12 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  
  13,    // default voice 13 assignment
  250,   // level
  0,    // sampleindex
  4096 // initial pitch step - normal pitch
};  


#include "audio/samps.h"

void setup_samples() {
    for (int i = 0; i < NUM_VOICES; i++) { // silence all voices by setting sampleindex to last sample
        voice[i].sampleindex=sample[voice[i].sample].samplesize<<12; // sampleindex is a 20:12 fixed point number
        voice[i].level=sample[voice[i].sample].play_volume / 8 ; // set the level to the sample level
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

SamplePlayer sw;
void setup1()
{
  while (!started && !Serial) {
    Serial.println("setup() - waiting to start");
    delay(100);
  }
  delay(500);
  Serial.println(F("setup1 started!"));

  sw.Run();
}

void loop1() {

}