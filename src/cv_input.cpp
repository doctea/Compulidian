#include "Config.h"

#include "clock.h"
#include "bpm.h"

#ifdef ENABLE_PARAMETERS

#include "ParameterManager.h"

#include "workshop_output.h"

ParameterManager *parameter_manager = new ParameterManager(LOOP_LENGTH_TICKS);

extern WorkshopOutputWrapper output_wrapper;

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
#include "parameter_inputs/ThresholdToggleParameterInput.h"

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

#include "audio/audio.h"

// initialise the input voltage ParameterInputs that can be mapped to Parameters
FLASHMEM 
void setup_parameter_inputs() {
    //parameter_manager = new ParameterManager();
    // add the available parameters to a list used globally and later passed to each selector menuitem
    //Serial.println(F("==== begin setup_parameter_inputs ====")); Serial_flush();
    tft_print("..setup_parameter_inputs...");

    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(0, &sw, 0, 5.0, false));    // main knob
    //parameter_manager->voltage_sources->tail()->debug = true;
    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(1, &sw, 4, 5.0, false));    // cv1
    //parameter_manager->voltage_sources->tail()->debug = true;
    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(2, &sw, 1, 5.0, false));    // knob x
    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(3, &sw, 5, 5.0, false));    // cv2
    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(4, &sw, 2, 5.0, false));    // knob y
    //parameter_manager->voltage_sources->tail()->debug = true;
    parameter_manager->addVoltageSource(new ComputerCardVoltageSource(5, &sw, 3, 5.0, false));    // switch ?
    //parameter_manager->voltage_sources->tail()->debug = true; // switch

    #ifdef ENABLE_SHUFFLE
        parameter_manager->addVoltageSource(new ComputerCardVoltageSource(6, &sw, 6, 8.0, false));    // audio in 1 used as CV
        parameter_manager->addVoltageSource(new ComputerCardVoltageSource(7, &sw, 7, 8.0, false));    // audio in 2 used as CV
    #endif

    // initialise the voltage source inputs
    // CVs are bipolar input, knobs are unipolar
    Serial.println(F("==== begin setup_parameter_inputs ====")); Serial_flush();
    VoltageParameterInput *vpi_knob_main = new VoltageParameterInput((char*)"Main", "CV Inputs", parameter_manager->voltage_sources->get(0), 0.005, UNIPOLAR, true);    
    VoltageParameterInput *vpi_cv_1 = new VoltageParameterInput((char*)"CV1", "CV Inputs",       parameter_manager->voltage_sources->get(1), 0.005, BIPOLAR, true);    
    Serial.println(F("==== begin setup_parameter_inputs 2====")); Serial_flush();
    VoltageParameterInput *vpi_knob_x = new VoltageParameterInput((char*)"X", "CV Inputs",       parameter_manager->voltage_sources->get(2), 0.005, UNIPOLAR, true);    
    VoltageParameterInput *vpi_cv_2 = new VoltageParameterInput((char*)"CV2", "CV Inputs",       parameter_manager->voltage_sources->get(3), 0.005, BIPOLAR, true);    
    Serial.println(F("==== begin setup_parameter_inputs 3====")); Serial_flush();
    VoltageParameterInput *vpi_knob_y = new VoltageParameterInput((char*)"Y", "CV Inputs",       parameter_manager->voltage_sources->get(4), 0.01, UNIPOLAR, true);    

    //VoltageParameterInput *vpi_switch = new VoltageParameterInput((char*)"Switch", "CV Inputs",  parameter_manager->voltage_sources->get(5), 0.005, UNIPOLAR, false);    
    ThresholdToggleParameterInput *vpi_hold_switch = new ThresholdToggleParameterInput(
        (char*)"Hold", "CV Inputs",    
        parameter_manager->voltage_sources->get(5), 
        0.000005, 
        UNIPOLAR, 
        true,
        //-0.4, // under 0.4, switch is HELD
        0.0003,
        [=](bool state) -> void { 
            if (Serial) Serial.printf("Hold switch %s\n", state ? "ON - disabling on-phrase changes" : "OFF - enabling on-phrase changes"); 
            if (!state) {
                if (Serial) Serial.printf("Hold switch going OFF - seed was %u\n", sequencer->get_euclidian_seed());
            }
            sequencer->set_euclidian_seed_lock(state);
            if (Serial) Serial.printf("Euclidian seed lock is now %s, seed is now %u\n", sequencer->is_euclidian_seed_lock() ? "ON" : "OFF", sequencer->get_euclidian_seed());
        }
    ); 
    //vpi_hold_switch->debug = true;
    ThresholdToggleParameterInput *vpi_mome_switch = new ThresholdToggleParameterInput(
        (char*)"Switch", "CV Inputs",  parameter_manager
        ->voltage_sources->get(5), 
        0.000005, 
        UNIPOLAR, 
        true, 
        //0.7, // over 0.7, switch is MOMENTARY
        -0.00005,
        [=](bool state) -> void { 
            //if (Serial) Serial.printf("Momentary switch %s\n", state ? "ON" : "OFF");
            output_wrapper.set_muted(state);
        }
    ); 
    //vpi_mome_switch->debug = true;
    
    #ifdef ENABLE_SHUFFLE
        VoltageParameterInput *vpi_audio_in_1 = new VoltageParameterInput((char*)"Audio In 1", "CV Inputs", parameter_manager->voltage_sources->get(6), 0.005, BIPOLAR, true);  
        VoltageParameterInput *vpi_audio_in_2 = new VoltageParameterInput((char*)"Audio In 2", "CV Inputs", parameter_manager->voltage_sources->get(7), 0.005, BIPOLAR, true);
    #endif

    //parameter_manager->voltage_sources->get(0)->debug = true;

    /*
    vpi_knob_main->debug = true;
    vpi_cv_1->debug = true;
    vpi_cv_2->debug = true;
    vpi_knob_x->debug = true;
    vpi_knob_y->debug = true;
    */

    // tell the parameter manager about them
    parameter_manager->addInput(vpi_knob_main);
    parameter_manager->addInput(vpi_cv_1);
    parameter_manager->addInput(vpi_knob_x);
    parameter_manager->addInput(vpi_cv_2);
    parameter_manager->addInput(vpi_knob_y);
    parameter_manager->addInput(vpi_hold_switch);
    parameter_manager->addInput(vpi_mome_switch);
    #ifdef ENABLE_SHUFFLE
        parameter_manager->addInput(vpi_audio_in_1);
        parameter_manager->addInput(vpi_audio_in_2);
    #endif

    /*VirtualParameterInput *virtpi1 = new VirtualParameterInput((char*)"LFO sync", "LFOs", LFO_LOCKED);
    VirtualParameterInput *virtpi2 = new VirtualParameterInput((char*)"LFO free", "LFOs", LFO_FREE);
    VirtualParameterInput *virtpi3 = new VirtualParameterInput((char*)"Random",   "LFOs", RAND);
    parameter_manager->addInput(virtpi1);
    parameter_manager->addInput(virtpi2);
    parameter_manager->addInput(virtpi3);*/

    /*
    Serial.println("about to do setDefaultParameterConnections().."); Serial.flush();
    parameter_manager->setDefaultParameterConnections();
    Serial.println("just did do setDefaultParameterConnections().."); Serial.flush();
    */

    int parameter_index = 0;
    #ifdef ENABLE_SHUFFLE
        FloatParameter *shuffle_amount_parameter = sequencer->getParameters()->get(parameter_index);
        shuffle_amount_parameter->set_slot_0_input(vpi_audio_in_1);
        shuffle_amount_parameter->set_slot_0_amount(1.0f);
        shuffle_amount_parameter->set_slot_0_polarity(BIPOLAR);
        parameter_index++;
    #endif

    FloatParameter *euclidian_density = sequencer->getParameters()->get(parameter_index++);
    //euclidian_density->debug = true;
    euclidian_density->set_slot_0_input(vpi_knob_main);
    euclidian_density->set_slot_0_amount(1.0f);
    euclidian_density->set_slot_0_polarity(UNIPOLAR);
    euclidian_density->set_slot_1_input(vpi_cv_1);
    euclidian_density->set_slot_1_amount(1.0f);
    euclidian_density->set_slot_1_polarity(BIPOLAR);

    FloatParameter *euclidian_density_2 = sequencer->getParameters()->get(parameter_index++);
    //euclidian_density_2->debug = true;
    euclidian_density_2->set_slot_0_input(vpi_knob_x);
    euclidian_density_2->set_slot_0_amount(1.0f);
    euclidian_density_2->set_slot_0_polarity(UNIPOLAR);
    euclidian_density_2->set_slot_1_input(vpi_cv_2);
    euclidian_density_2->set_slot_1_amount(1.0f);
    euclidian_density_2->set_slot_1_polarity(BIPOLAR);
        
    /*
    FloatParameter *mutation_amount = sequencer->getParameters()->get(NUM_GLOBAL_DENSITY_CHANNELS);
    //mutation_amount->debug = true;
    mutation_amount->set_slot_0_input(vpi_knob_y);
    mutation_amount->set_slot_0_amount(1.0f);
    mutation_amount->set_slot_0_polarity(UNIPOLAR);
    */

    LDataParameter<float> *bpm_parameter = new LDataParameter<float>(
        (char*)"BPM", 
        [=](float v) -> void {
            // don't allow BPM to be set unless clock is internal
            if (clock_mode == CLOCK_INTERNAL) {
                set_bpm(v);
            }
        },
        [=]() -> float { return get_bpm(); },
        KNOB_MIN_BPM, KNOB_MAX_BPM
    );
    bpm_parameter->set_slot_0_input(vpi_knob_y);
    bpm_parameter->set_slot_0_amount(1.0f);
    bpm_parameter->set_slot_0_polarity(UNIPOLAR);
    parameter_manager->addParameter(bpm_parameter);

    /*LDataParameter<float> *pitch_parameter = new LDataParameter<float>(
        (char*)"Pitch", 
        [=](float v) -> void {
            sw.global_pitch = 4096.0 * v;
            Serial.printf("Got v %f, so global_pitch set to %i\n", v, sw.global_pitch);
        },
        [=]() -> float { return sw.global_pitch / 4096; },
        0.5, 1.5
    );*/
    LDataParameter<int16_t> *pitch_parameter = new LDataParameter<int16_t>(
        (char*)"Pitch", 
        [=](int16_t v) -> void {
            sw.global_pitch = v;
            //Serial.printf("Setting global_pitch to %i\n", sw.global_pitch);
        },
        [=]() -> int16_t { return sw.global_pitch; },
        (2048), (4096+2048)
    );
    pitch_parameter->set_slot_0_input(vpi_audio_in_2);
    pitch_parameter->set_slot_0_amount(1.0f);
    pitch_parameter->set_slot_0_polarity(UNIPOLAR);
    parameter_manager->addParameter(pitch_parameter);
    
    tft_print("Finished setup_parameter_inputs()\n");
}

#endif

#endif