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
  0,    // left channel
  
  1,      // default voice 1 assignment 
  250,
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel

  2,    // default voice 2 assignment 
  250, // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel

  3,    // default voice 3 assignment 
  250, // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel

  4,    // default voice 4 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel

  5,    // default voice 5 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel

  6,    // default voice 6 assignment 
  250,  // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel

  7,    // default voice 7 assignment 
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch 
  1,    // right channel

  8,    // default voice 8 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel

  9,    // default voice 9 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel

  10,    // default voice 10 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel

  11,    // default voice 11 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel

  12,    // default voice 12 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  0,    // left channel
  
  13,    // default voice 13 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch
  1,    // right channel
};  


#include "audio/samps.h"

void setup_samples() {
    for (int i=0; i< NUM_VOICES; ++i) { // silence all voices by setting sampleindex to last sample
        voice[i].sampleindex=sample[voice[i].sample].samplesize<<12; // sampleindex is a 20:12 fixed point number
    } 
}

void dacWrite(int channel, int value)
{
    uint16_t DAC_data = (!channel) ? (DAC_config_chan_A_gain | ((value) & 0xffff)) : (DAC_config_chan_B_gain | ((value) & 0xffff));
    spi_write16_blocking(SPI_PORT, &DAC_data, 1);
}
  
uint16_t dacval(int16_t value, uint16_t dacChannel)
{
    if (value<-2048) value = -2048;
    if (value > 2047) value = 2047;
    return (dacChannel | 0x3000) | (((uint16_t)((value & 0x0FFF) + 0x800)) & 0x0FFF);
}

bool play_sound(__attribute__((unused)) repeating_timer_t *rt) { 
  int32_t newsample,samplesum_l=0,samplesum_r=0;
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
      if (voice[track].channel==0) { // left channel
        samplesum_l+=(newsample*voice[track].level); // changed to MIDI velocity levels 0-127
      } else { // right channel
        samplesum_r+=(newsample*voice[track].level); // changed to MIDI velocity levels 0-127
      }
      samplesum_l+=(newsample*voice[track].level); // changed to MIDI velocity levels 0-127
      voice[track].sampleindex+=voice[track].sampleincrement; // add step increment
      voice[track].level=sample[tracksample].play_volume; // set the volume for the sample
    }
  }
  
  samplesum_l=samplesum_l>>7;  // adjust for volume multiply above
  if  (samplesum_l>32767) samplesum_l=32767; // clip if sample sum is too large
  if  (samplesum_l<-32767) samplesum_l=-32767;
  samplesum_l >>= 6;  // scale down to 12 bit range
  
  samplesum_r=samplesum_r>>7;  // adjust for volume multiply above
  if  (samplesum_r>32767) samplesum_r=32767; // clip if sample sum is too large
  if  (samplesum_r<-32767) samplesum_r=-32767;
  samplesum_r >>= 6;  // scale down to 12 bit range

  #ifdef MONITOR_CPU1  
      digitalWrite(CPU_USE,0); // low - CPU not busy
  #endif

  dacWrite(0, dacval(int(samplesum_l), DAC_CHANNEL_A)); // left
  dacWrite(1, dacval(int(samplesum_r), DAC_CHANNEL_B)); // right
  
  #ifdef MONITOR_CPU1
      digitalWrite(CPU_USE,1); // hi = CPU busy
  #endif

  return true;
}

repeating_timer_t timer1;
void setup1() {
  //while(!Serial) {}
  
  // Setup SPI
  SPI.begin(); // Initialize the SPI bus

  pinMode(DAC_CS, OUTPUT); // Set the Chip Select pin as an output
  digitalWrite(DAC_CS, HIGH); // Deselect the SPI device to start

  while (!started) {
    if (Serial) Serial.println("loop1() - waiting to start");
    return;
  }
  
  #ifdef PLAY_SOUNDS_WITH_INTERRUPTS
    bool v = add_repeating_timer_us(40, play_sound, NULL, &timer1); // 48kHz sample rate
    if (!v) {
      while (1) {
        if (Serial) Serial.println("Failed to add repeating timer");
        sleep_us(1000);
      }
    } else {
      while (1) {
        if (Serial) Serial.println("Added repeating timer");
      }
    }
  #endif
}

// second core calculates samples and sends to DAC
void loop1() {
  while (!started) {
    if (Serial) Serial.println("loop1() - waiting to start");
    return;
  }

  #ifndef PLAY_SOUNDS_WITH_INTERRUPTS
    play_sound(NULL);
    sleep_us(80);
  #endif
}