#include <Bela.h>
#include <Midi.h>

#include <mec_api.h>
#include <processors/mec_mpe_processor.h>

Midi 			gMidi;
mec::MecApi* 		gMecApi=NULL;
mec::Callback* 	gMecCallback=NULL;
const char* 	gMidiPort0 = "hw:1,0,0";


AuxiliaryTask gMecProcessTask;

/*

struct MidiVoiceData {
	unsigned startNote_;
	unsigned note_;		 //0
	unsigned pitchbend_; //1
	unsigned timbre_;    //2
	unsigned pressure_;  //3
	bool active_;
	int changeMask_;
};
MidiVoiceData gMidiVoices[16];

class BelaMidiMecCallback : public mec::Callback {
public:
	BelaMidiMecCallback() :	pitchbendRange_(48.0) {
		;
	}
	
    virtual void touchOn(int touchId, float note, float x, float y, float z) {
    	// rt_printf("touchOn %i , %f, %f %f %f", touchId,note,x,y,z);
    	int ch = touchId + 1;

	    MidiVoiceData& voice = gMidiVoices[ch];
    
    	voice.startNote_ = note;
    	
	    float semis = note - float(voice.startNote_); 
	    unsigned pb = bipolar14bit(semis / pitchbendRange_);
		unsigned pressure = unipolar7bit(z); // note on, we use as velocity
		unsigned timbre = bipolar7bit(y);
		
		gMidi.writePitchBend(ch, pb);
    	gMidi.writeNoteOn(ch ,note, pressure);
    	gMidi.writeControlChange(ch,74,timbre);
    	gMidi.writeChannelPressure(ch,0);

    	voice.note_ = note;
    	voice.pitchbend_ = pb;
    	voice.timbre_ = timbre;
    	voice.pressure_ = 0; // we used it for velocity
    	voice.changeMask_ = 0;
    	voice.active_ = true;
    }

    virtual void touchContinue(int touchId, float note, float x, float y, float z) {
    	// only send out one set of updates per render call
    	int ch = touchId + 1;
	    MidiVoiceData& voice = gMidiVoices[ch];

	    float semis = note - float(voice.startNote_); 
	    unsigned pb = bipolar14bit(semis / pitchbendRange_);
    	unsigned timbre = bipolar7bit(y);
    	unsigned pressure = unipolar7bit(z);

    	voice.changeMask_ = 		voice.changeMask_ 
    								|   ((pb != voice.pitchbend_) << 1) 
    								|   ((timbre != voice.timbre_) << 2) 
    								|   ((pressure != voice.pressure_) << 3);
		// voice.startNote_ = 0;
    	voice.note_ = note;
    	voice.pitchbend_ = pb;
    	voice.timbre_ = timbre;
    	voice.pressure_ = pressure;
    }

    virtual void touchOff(int touchId, float note, float x, float y, float z) {
    	// rt_printf("touchOff %i , %f, %f %f %f", touchId,note,x,y,z);
    	int ch = touchId + 1;
	    MidiVoiceData& voice = gMidiVoices[ch];

  	    float semis = note - float(voice.startNote_); 
	    unsigned pb = bipolar14bit(semis / pitchbendRange_);

		// unsigned pressure = unipolar7bit(z); 
		unsigned timbre = bipolar7bit(y);

		gMidi.writePitchBend(ch, pb);
    	gMidi.writeControlChange(ch,74,timbre);
    	gMidi.writeChannelPressure(ch,0);
    	gMidi.writeNoteOff(ch,voice.startNote_,0);

		voice.startNote_ = 0;
    	voice.note_ = 0;
    	voice.pitchbend_ = 0;
    	voice.timbre_ = 0;
    	voice.pressure_ = 0;
    	voice.changeMask_ = 0;
    	voice.active_ = true;
    }
    
    virtual void control(int ctrlId, float v)  {
    	gMidi.writeControlChange(0,ctrlId,v*127);
    }
    
    int bipolar14bit(float v) {return ((v * 0x2000) + 0x2000);}
    int bipolar7bit(float v) {return ((v / 2) + 0.5)  * 127; }
    int unipolar7bit(float v) {return v * 127;}
    
private:
    float pitchbendRange_;
};
*/

// this replaces the above, and can all be removed after testing
class MecMpeProcessor : public mec::MPE_Processor {
public:
    MecMpeProcessor() {
         // p.getInt("voices", 15);
        setPitchbendRange(48.0);
    }   

    bool isValid() { return output_.isOpen();}

    void  process(mec::MidiProcessor::MidiMsg& m) {
        if(output_.isOpen()) {
            midi_byte_t msg[4];
            msg_[0] = m.data[0];
            msg_[1] = m.data[1];
            msg_[2] = m.data[2];
            msg_[3] = m.data[3];
            gMidi.writeOutput(msg,m.size);
        }
    }
};

void mecProcess(void* pvMec) {
	MecApi *pMecApi = (MecApi*) pvMec;
	pMecApi->process();
}


bool setup(BelaContext *context, void *userData)
{
	gMidi.writeTo(gMidiPort0);

	gMecApi=new mec::MecApi();
	gMecCallback=new MecMpeProcessor();
	gMecApi->init();
	gMecApi->subscribe(gMecCallback);
	
    // Initialise auxiliary tasks

	if((gMecProcessTask = Bela_createAuxiliaryTask(&mecProcess, BELA_AUDIO_PRIORITY - 1, "mecProcess", gMecApi)) == 0)
		return false;

	return true;
}

// render is called 2750 per second (44000/16)
const int decimation = 5;  // = 550/seconds
long renderFrame = 0;
void render(BelaContext *context, void *userData)
{
	Bela_scheduleAuxiliaryTask(gMecProcessTask);
	
	renderFrame++;
	// silence audio buffer
	for(unsigned int n = 0; n < context->audioFrames; n++) {
		for(unsigned int channel = 0; channel < context->audioOutChannels; channel++) {
			audioWrite(context, n, channel, 0.0f);
		}
	}

	if(decimation <= 1 || ((renderFrame % decimation) ==0) ) {	
		for(int ch=0;ch<16;ch++) {
		    MidiVoiceData& voice = gMidiVoices[ch];
		    if(voice.active_) {
				if(voice.changeMask_ & (1 << 1)) {
					gMidi.writePitchBend(ch, voice.pitchbend_);
				}
				if(voice.changeMask_ & (1 << 2)) {
			    	gMidi.writeControlChange(ch,74,voice.timbre_);
				}
				if(voice.changeMask_ & (1 << 3)) {
			    	gMidi.writeChannelPressure(ch,voice.pressure_);
				}
		    }
			voice.changeMask_ = 0;
		}
	}
}

void cleanup(BelaContext *context, void *userData)
{
	gMecApi->unsubscribe(gMecCallback);
	delete gMecCallback;
	delete gMecApi;
}