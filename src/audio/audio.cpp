#include "Config.h"

#include "audio/audio.h"
#include "audio/bank_manager.h"
#include "audio/sample_store.h"
//#include "computer.h"

BaseSampleData **sample_data = nullptr;

#include <atomic>

extern std::atomic<bool> started;

std::atomic<bool> g_usbmidi_tx_busy{false};

/* sample player stuff starts */

voice_t voice[] = {
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
  4096, // initial pitch step - normal pitch

  14,    // default voice 13 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  15,    // default voice 13 assignment
  250,   // level
  0,    // sampleindex
  4096, // initial pitch step - normal pitch

  16,    // default voice 13 assignment
  250,   // level
  0,    // sampleindex
  4096 // initial pitch step - normal pitch
};  

int NUM_VOICES = sizeof(voice)/sizeof(voice[0]); // number of voices in the system


#include "audio/samps.h"

void setup_samples() {
    // Phase B: scan the content-addressed sample store index first (read-only)
    // so BankManager can resolve bank config sample_id references against it
    // - see audio/sample_store.h.
    sampleStore.setup();

    // All bank detection, sample_data allocation, and compiled-in/bank
    // config loading is handled by BankManager. It reads Settings::active_bank
    // from flash and loads the appropriate bank automatically.
    bankManager.setup();
}

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