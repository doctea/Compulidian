//#include "Config.h"

#include "sequencer/Euclidian.h"
#include "outputs/base_outputs.h"

#ifdef ENABLE_SCREEN
    #include "mymenu.h"
#endif

#include "outputs/output_processor.h"

#ifdef ENABLE_EUCLIDIAN
    EuclidianSequencer *sequencer = nullptr;

    // call this after the menu has already been set up
    void setup_sequencer() {
        // create sequencer and assign the output processor's nodes; also creates the patterns
        sequencer = new EuclidianSequencer(output_processor->nodes, output_processor->nodes->size());
        sequencer->debug = true;
        //sequencer->initialise_patterns();
        //sequencer->reset_patterns();
    }

#endif