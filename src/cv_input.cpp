//#include "Config.h"

#include "clock.h"
#include "bpm.h"

#ifdef ENABLE_PARAMETERS

#include "ParameterManager.h"

ParameterManager *parameter_manager = new ParameterManager(LOOP_LENGTH_TICKS);

#ifdef ENABLE_CV_INPUT

//#include "cv_input.h"

#include "ParameterManager.h"
#ifdef ENABLE_SCREEN
    #include "colours.h"
    #include "submenuitem.h"
#endif

//#include "devices/ADCPimoroni24v.h"
#include "voltage_sources/PicoSDKADCVoltageSource.h"

#include "parameter_inputs/VirtualParameterInput.h"

#include "sequencer/sequencing.h"

//#include "Wire.h"
bool cv_input_enabled = true;

#define tft_print(X) Serial.print(X)

// initialise the voltage-reading hardware/libraries and the ParameterManager
FLASHMEM
void setup_cv_input() {
    //Serial.println("setup_cv_input...");
    tft_print("...setup_cv_input...\n");

    parameter_manager->init();

    //Wire.begin();

    #ifdef ENABLE_CV_INPUT
        //parameter_manager->addADCDevice(new ADCPimoroni24v(ENABLE_CV_INPUT, &Wire, 5.0));
    #endif

    parameter_manager->auto_init_devices();

    tft_print("..done setup_cv_input\n");
}

// initialise the input voltage ParameterInputs that can be mapped to Parameters
FLASHMEM 
void setup_parameter_inputs() {
    //parameter_manager = new ParameterManager();
    // add the available parameters to a list used globally and later passed to each selector menuitem
    //Serial.println(F("==== begin setup_parameter_inputs ====")); Serial_flush();
    tft_print("..setup_parameter_inputs...");

    parameter_manager->addVoltageSource(new WorkshopVoltageSource(0, 0, 2, 10.0, false));
    parameter_manager->addVoltageSource(new WorkshopVoltageSource(1, 0, 3, 10.0, false));
    parameter_manager->addVoltageSource(new WorkshopVoltageSource(2, 1, 2, 10.0, false));
    parameter_manager->addVoltageSource(new WorkshopVoltageSource(3, 1, 3, 10.0, false));
    parameter_manager->addVoltageSource(new WorkshopVoltageSource(4, 1, 2, 10.0, false));

    // initialise the voltage source inputs
    Serial.println(F("==== begin setup_parameter_inputs ====")); Serial_flush();
    VoltageParameterInput *vpi1 = new VoltageParameterInput((char*)"Main", "CV Inputs", parameter_manager->voltage_sources->get(0));    
    VoltageParameterInput *vpi2 = new VoltageParameterInput((char*)"CV1", "CV Inputs",  parameter_manager->voltage_sources->get(1));

    Serial.println(F("==== begin setup_parameter_inputs 2====")); Serial_flush();
    VoltageParameterInput *vpi3 = new VoltageParameterInput((char*)"X", "CV Inputs",    parameter_manager->voltage_sources->get(2));
    VoltageParameterInput *vpi4 = new VoltageParameterInput((char*)"CV2", "CV Inputs",  parameter_manager->voltage_sources->get(3));

    Serial.println(F("==== begin setup_parameter_inputs 3====")); Serial_flush();

    VoltageParameterInput *vpi5 = new VoltageParameterInput((char*)"Y", "CV Inputs",    parameter_manager->voltage_sources->get(4));
    //VoltageParameterInput *vpi6 = new VoltageParameterInput((char*)"CV2", "CV Inputs",  new WorkshopVoltageSource(3, 1, 3, 5.0, false));
    
    //vpi3->input_type = UNIPOLAR;
    // todo: set up 1v/oct inputs to map to MIDI source_ids...

    // tell the parameter manager about them
    Serial.printf("telling parameter_manager (%p) about it\n", parameter_manager); Serial_flush();

    delay(100);
    Serial.println("about to add inputs.."); Serial_flush();
    delay(100);

    parameter_manager->addInput(vpi1);
    parameter_manager->addInput(vpi2);
    parameter_manager->addInput(vpi3);
    parameter_manager->addInput(vpi4);
    parameter_manager->addInput(vpi5);

    /*VirtualParameterInput *virtpi1 = new VirtualParameterInput((char*)"LFO sync", "LFOs", LFO_LOCKED);
    VirtualParameterInput *virtpi2 = new VirtualParameterInput((char*)"LFO free", "LFOs", LFO_FREE);
    VirtualParameterInput *virtpi3 = new VirtualParameterInput((char*)"Random",   "LFOs", RAND);
    parameter_manager->addInput(virtpi1);
    parameter_manager->addInput(virtpi2);
    parameter_manager->addInput(virtpi3);*/

    Serial.println("about to do setDefaultParameterConnections().."); Serial.flush();
    parameter_manager->setDefaultParameterConnections();
    Serial.println("just did do setDefaultParameterConnections().."); Serial.flush();

    // todo: parameters that control Euclidian density, etc...
    Serial.printf("about to setup parameters for sequencer %p\n", sequencer); Serial.flush();
    Serial.printf("about to setup parameters for sequencer %p\n", sequencer); Serial.flush();
    Serial.printf("about to setup parameters for sequencer %p\n", sequencer); Serial.flush();
    Serial.printf("about to setup parameters for sequencer %p\n", sequencer); Serial.flush();
    FloatParameter *euclidian_density = sequencer->getParameters()->get(0);
    euclidian_density->debug = true;
    Serial.printf("got a pointer to euclidian_density %p\n", euclidian_density); Serial.flush();
    Serial.printf("got a pointer to euclidian_density %p\n", euclidian_density); Serial.flush();
    euclidian_density->set_slot_0_input(vpi1);
    euclidian_density->set_slot_1_input(vpi2);
    euclidian_density->set_slot_0_amount(1.0f);
    euclidian_density->set_slot_1_amount(1.0f);

    FloatParameter *euclidian_density_2 = sequencer->getParameters()->get(1);
    euclidian_density_2->debug = true;
    euclidian_density->set_slot_0_input(vpi4);
    euclidian_density->set_slot_1_input(vpi5);
    euclidian_density->set_slot_0_amount(1.0f);
    euclidian_density->set_slot_1_amount(1.0f);

    FloatParameter *mutation_amount = sequencer->getParameters()->get(NUM_GLOBAL_DENSITY_CHANNELS);
    mutation_amount->debug = true;
    mutation_amount->set_slot_0_input(vpi3);
    mutation_amount->set_slot_1_input(vpi4);
    mutation_amount->set_slot_0_amount(1.0f);
    mutation_amount->set_slot_1_amount(1.0f);

    tft_print("Finished setup_parameter_inputs()\n");
}

#endif

#endif