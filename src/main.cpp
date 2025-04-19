#include <Arduino.h>

#include <SPI.h>

/* sample player stuff starts */
#define NUM_VOICES 8
struct voice_t {
  int16_t sample;   // index of the sample structure in sampledefs.h
  int16_t level;   // 0-1000 for legacy reasons
  volatile uint32_t sampleindex; // 20:12 fixed point index into the sample array
  uint16_t sampleincrement; // 1:12 fixed point sample step for pitch changes 
} voice[NUM_VOICES] = {
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


#include "testkit/samples.h"
#define NUM_SAMPLES (sizeof(sample)/sizeof(sample_t)) 

/* sample player stuff ends */


#include "sequencer/sequencing.h"
#include "outputs/output_processor.h"

#include "workshop_output.h"

#include <clock.h>
#include <bpm.h>

#ifdef ENABLE_PARAMETERS
  #include "ParameterManager.h"
#endif

#ifdef ENABLE_CV_INPUT
    #include "cv_input.h"
    #ifdef ENABLE_CLOCK_INPUT_CV
        #include "cv_input_clock.h"
    #endif

    //#include "outputs/output_voice.h"
#endif

#define WAIT_FOR_SERIAL

#include "computer.h"

WorkshopOutputWrapper output_wrapper;

std::atomic<bool> started = false;
std::atomic<bool> ticked = false;

void do_tick(uint32_t ticks);

// serial console to host, for debug etc
void setup_serial() {
  Serial.begin(115200);
  Serial.setTimeout(0);

  #ifdef WAIT_FOR_SERIAL
      while(!Serial) {};
      //while (1)
        //Serial.println(F("Serial started!"));
  #endif
}

// call this when global clock should be reset
void global_on_restart() {
  set_restart_on_next_bar(false);

  //Serial.println(F("on_restart()==>"));
  clock_reset();
  last_processed_tick = -1;
  //Serial.println(F("<==on_restart()"));
}


void setup() {
  setup_serial();

  for (int i = 1000 ; i >0 ; i--) {
    Serial.print(i); Serial.flush();
    delay(1);
  }

  Serial.println(F("done setup_serial; now gonna SetupComputerIO()")); Serial.flush();
  SetupComputerIO();
  Serial.println(F("done SetupComputerIO; now gonna setup_uclock()")); Serial.flush();

  for (int i=0; i< NUM_VOICES; ++i) { // silence all voices by setting sampleindex to last sample
    voice[i].sampleindex=sample[voice[i].sample].samplesize<<12; // sampleindex is a 20:12 fixed point number
  } 

  setup_uclock(do_tick, uClock.PPQN_24);
  set_global_restart_callback(global_on_restart);

  set_bpm(60);

  Serial.println(F("done setup_uclock()"));
  
  #ifdef ENABLE_EUCLIDIAN
    //Serial.println("setting up sequencer..");
    setup_output(&output_wrapper);
    setup_sequencer();
    output_processor->configure_sequencer(sequencer);
  #endif

  #ifdef ENABLE_PARAMETERS
    #ifdef ENABLE_CV_INPUT
      Serial.println(F("setting up cv input..")); Serial.flush();
      setup_cv_input();
      Serial.println(F("..done setup_cv_input")); Serial.flush();
      
      Serial.println(F("setting up parameter inputs..")); Serial.flush();
      setup_parameter_inputs();
      Serial.println(F("..done setup_parameter_inputs")); Serial.flush();
      Debug_printf("after setup_parameter_inputs(), free RAM is\t%u\n", freeRam());
    #endif
    /*#ifdef ENABLE_CV_INPUT  // these are midi outputs!
        setup_parameter_outputs(output_wrapper);
        Debug_printf("after setup_parameter_outputs(), free RAM is\t%u\n", freeRam());
    #endif*/
    setup_output_processor_parameters();
    Debug_printf("after setup_output_processor_parameters(), free RAM is\t%u\n", freeRam());
  #endif

  #if defined(ENABLE_PARAMETERS)
    //Serial.println("..calling sequencer.getParameters()..");
    LinkedList<FloatParameter*> *params = sequencer->getParameters();
    Debug_printf("after setting up sequencer parameters, free RAM is %u\n", freeRam());
  #endif

  #ifdef ENABLE_PARAMETERS
    parameter_manager->setDefaultParameterConnections();
  #endif

  #ifdef USE_UCLOCK
    //if (Serial) Serial.println("Starting uClock...");
    //Serial_flush();
    clock_reset();
    clock_start();
    //if (Serial) Serial.println("Started uClock!");
    //Serial_flush();
  #endif
  started = true;

  Serial.println(F("setup() done - starting!"));

}

void do_tick(uint32_t in_ticks) {
  #ifdef USE_UCLOCK
      ::ticks = in_ticks;
      // todo: hmm non-USE_UCLOCK mode doesn't actually use the in_ticks passed in here..?
  #endif
  //if (Serial) Serial.printf("ticked %u\n", ticks);
  if (is_restart_on_next_bar() && is_bpm_on_bar(ticks)) {
      //if (debug) Serial.println(F("do_tick(): about to global_on_restart"));
      global_on_restart();

      set_restart_on_next_bar(false);
  }

  //output_wrapper->sendClock();

  #ifdef ENABLE_EUCLIDIAN
      if (sequencer->is_running()) sequencer->on_tick(ticks);
      if (is_bpm_on_sixteenth(ticks) && output_processor->is_enabled()) {
          output_processor->process();
      }
  #endif
}

void loop() {

  ATOMIC() 
  {
      ticked = update_clock_ticks();
  }

  ATOMIC() 
  {
      /*if (playing && clock_mode==CLOCK_INTERNAL && last_ticked_at_micros>0 && micros() + loop_average >= last_ticked_at_micros + micros_per_tick) {
          // don't process anything else this loop, since we probably don't have time before the next tick arrives
          //Serial.printf("early return because %i + %i >= %i + %i\n", micros(), loop_average, last_ticked_at_micros, micros_per_tick);
          //Serial.flush();
      } else {*/
          if (output_processor->is_enabled()) {
              output_processor->loop();
          }
      /*
          #ifdef ENABLE_SCREEN
          if (!is_locked()) {
              //acquire_lock();
              menu->update_inputs();
              //release_lock();
          }
          #endif

          add_loop_length(micros()-mics_start);
      }
      */
  }

  if (cv_input_enabled) {
    if (parameter_manager->ready_for_next_update()) {
        //parameter_manager->process_calibration();

        parameter_manager->throttled_update_cv_input__all(5, false, false);
    }
  }

}


// DAC parameters
//  A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
//  A-channel, 2x, active -- NB 2x = ~3.27v max, not 4.096
#define DAC_config_chan_A_gain 0b0001000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000
#define DAC_config_chan_B_gain 0b1001000000000000


void dacWrite(int lchan, int rchan) { 
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

void setup1() {

  while(!Serial) {}
  /*for(int i = 0 ; i < 10 ; ++i) {
    Serial.print("core1: "); Serial.print(i); Serial.println(); Serial.flush();
    delay(1000);
  }*/
  
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

  /*hw_write_masked(&spi_get_hw(spi0)->cr0, (16 - 1) << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS); // Fast set to 16-bits
  spi_write16_read16_blocking(spi0, &samplesum16, &ret, 1);
  hw_write_masked(&spi_get_hw(spi0)->cr0, (16 - 1) << SPI_SSPCR0_DSS_LSB, SPI_SSPCR0_DSS_BITS); // Fast set to 16-bits
  spi_write16_read16_blocking(spi0, &samplesum16, &ret, 1);*/


#ifdef MONITOR_CPU1
  digitalWrite(CPU_USE,1); // hi = CPU busy
#endif
}