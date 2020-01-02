#include <cstring>
#include <iostream>
#include <chrono>
#include <algorithm>

#include "unistd.h"
#include "syslog.h"

#include "PyramidASRService.h"
#include "config.h"

PyramidASRService::PyramidASRService() : Buckey::ASRService(PYRAMID_VERSION, "pyramid"), running(true), listening(false), endLoop(false), paused(false) {
    //Load the config file
    configFile = g_key_file_new();
    GError * error = NULL;
    if(!g_key_file_load_from_file(configFile, CONFIG_FILENAME, G_KEY_FILE_NONE, &error)) {
        std::cerr << "Error opening configuration file " << CONFIG_FILENAME << ": " << error->message << std::endl;
        
        setState(Buckey::Service::State::ERROR); 
        signalError(error->message);
        
        g_error_free(error);
        running.store(false);
        exit(-1);
    }
    
    //Load the acoustic model path from the config file
    char * configHmmPath = g_key_file_get_string(configFile, "Default", "hmm", &error);
    if(error != NULL) {
        std::cerr << "Error while parsing the acoustic model from the config file! " << error->message << std::endl;
        g_error_free(error);
        error = NULL;     
    }
    if(configHmmPath == NULL) {
        hmmPath = DEFAULT_HMM_PATH;
    }
    else {
        hmmPath = std::string(configHmmPath);
    }
    delete configHmmPath;
    
    //Load the language model path from the config file
    char * configLmPath = g_key_file_get_string(configFile, "Default", "lm", &error);
    if(error != NULL) {
        std::cerr << "Error while parsing the language model path from the config file! " << error->message << std::endl;
        g_error_free(error);
        error = NULL;     
    }
    if(configLmPath == NULL) {
        lmPath = DEFAULT_LM_PATH;
    }
    else {
        lmPath = std::string(configLmPath);
    }
    delete configLmPath;
    
    //Load the dictionary file path from the config file
    char * configDictPath = g_key_file_get_string(configFile, "Default", "dict", &error);
    if(error != NULL) {
        std::cerr << "Error while parsing the dictionary path from the config file! " << error->message << std::endl;
        g_error_free(error);
        error = NULL;     
    }
    if(configDictPath == NULL) {
    	dictPath = DEFAULT_DICT_PATH;
    }
    else {
        dictPath = std::string(configDictPath);    
    }
    delete configDictPath;
    
    //Load the audio device from the config file
    char * deviceName = g_key_file_get_string(configFile, "Default", "device", &error);
    if(error != NULL) {
        std::cerr << "Error while parsing audio device name from the config file! " << error->message << std::endl;
        g_error_free(error);
        error = NULL;     
    }
    if(deviceName == NULL) {
        device = "default";
    }
    else {
        device = std::string(deviceName);
    }
    delete deviceName;
    
    //Load in the number of decoders from the config file 'decoder-count'
    unsigned short defaultMaxDecoders = 2;
    int m = g_key_file_get_integer(configFile, "Default", "decoder-count", &error);
    if(m == 0) {
        if(error->code == G_KEY_FILE_ERROR_KEY_NOT_FOUND) {
            maxDecoders = defaultMaxDecoders;
            g_error_free(error);
        }
        else {
            ///TODO: Emit warning signal?
            std::cerr << "Error while parsing decoder-count from the config file, assuming 2 decoders: " << error->message << std::endl;
            maxDecoders = defaultMaxDecoders;
            g_error_free(error);
        }
    }
    else {
        maxDecoders = m;
    }

    listeningMode = Buckey::ASRService::ListeningMode::CONTINUOUS;
    searchMode = Buckey::ASRService::RecognitionMode::LANGUAGE_MODEL;
    
    //Create our decoders
    for(unsigned short i = 0; i < maxDecoders; i++) {
		SphinxDecoder * sd = new SphinxDecoder("base-lm", hmmPath, dictPath, DEFAULT_LOG_PATH);
		decoders.push_back(sd);
	}
	syslog(LOG_DEBUG, "Created decoders");
}

PyramidASRService::~PyramidASRService() {
    endLoop.store(true);
    
    //Kill the recognition thread if it is running
    if(listening.load() || recognizerLoop.joinable()) {
		recognizerLoop.join();
    }
    
    for(std::thread & t : miscThreads) {
        t.join();
    }
    
    for(SphinxDecoder * sd : decoders) {
        delete sd;
    }
    
    g_key_file_free(configFile);
}

void PyramidASRService::continuousSpeechRecognition(PyramidASRService * sr) {    
	syslog(LOG_DEBUG, "continuousSpeechRecognition started");
	sr->listening.store(true);
	sr->updateLock.lock();
	//Buckey::logInfo("Decoder management thread started");
	sr->endLoop.store(false);
    ad_rec_t *ad = nullptr; // Audio source

    int16 adbuf[2048]; //buffer that audio frames are copied into
    int32 frameCount = 0; // Number of frames read into the adbuf

    sr->inUtterance.store(false);

    sr->voiceDetected.store(false); // Reset this as its used to keep track of state

    
	syslog(LOG_DEBUG, "Opening audio device for recognition");
    // TODO: Use ad_open_dev without pocketsphinx's terrible configuration functions
    //if ((ad = ad_open_dev(NULL,(int) cmd_ln_float32_r(sr->decoders[0]->getConfig(),"-samprate"))) == NULL) {
    if(sr->device == "default") {
	    if ((ad = ad_open_sps(16000)) == NULL) { ///TODO: make samples per second configurable
	            
	            syslog(LOG_ERR, "Failed to open audio device default!");
	            sr->listening.store(false);
            	return;
	    }
	}
	else if((ad = ad_open_dev(sr->device.c_str(), 1600)) == NULL) {///TODO: make samples per second configurable
	    syslog(LOG_ERR, "Failed to open audio device %s", sr->device.c_str()); 
        sr->listening.store(false);
        return;
    }

    if (ad_start_rec(ad) < 0) {
        syslog(LOG_ERR, "Failed to start recording!");
        sr->listening.store(false);
        return;
    }
    
    sr->currentDecoderIndex.store(0);
    
    syslog(LOG_DEBUG, "Starting up utterances");
    // Start up all of the utterances
    for(SphinxDecoder * sd : sr->decoders) {
        if(!sd->isInUtterance()) {
            sd->startUtterance();
        }
    }
    syslog(LOG_DEBUG, "Done starting utterances");

    while(sr->decoders[sr->currentDecoderIndex]->state == SphinxHelper::DecoderState::NOT_INITIALIZED) {
		//Wait until the decoder is ready
    }

    sr->listening.store(true);
    //sr->triggerEvents(ON_READY, new EventData());
    //Buckey::getInstance()->reply("Sphinx Speech Recognition Ready", ReplyType::CONSOLE);
    std::cout << "Sphinx Speech Recognition Ready" << std::endl;
	sr->updateLock.unlock();

	//sr->triggerEvents(ON_SERVICE_READY, new EventData());

    while(!sr->endLoop.load()) {

        // Read from the audio buffer
		if(sr->paused.load()) {
			sr->decoderIndexLock.lock();
			sr->decoders[sr->currentDecoderIndex]->endUtterance();
			sr->inUtterance.store(false);
			sr->decoders[sr->currentDecoderIndex]->startUtterance();
			sr->decoderIndexLock.unlock();
		}
		while(sr->paused.load() && !sr->endLoop) {
			//Wait until not paused, but continue reading frames so that we only read current frames when we resume recognition
			frameCount = ad_read(ad, adbuf, AUDIO_FRAME_SIZE);
		}
		if(sr->endLoop) {
			break;
		}

		frameCount = ad_read(ad, adbuf, AUDIO_FRAME_SIZE);

        if(frameCount < 0 ) {
            
            syslog(LOG_ERR, "Failed to read from audio device for sphinx recognizer!");
            // TODO: Maybe fail a bit more gracefully
            //sr->killThreads();
            //exit(-1);
            sr->listening.store(false);
            return;
        }

        // Check to make sure our current decoder has not errored out
        if(sr->decoders[sr->currentDecoderIndex]->state == SphinxHelper::DecoderState::ERROR) {
            syslog(LOG_ERR, "Decoder is errored out! Trying next decoder...");
			bool found = false;
			for(unsigned short i = sr->currentDecoderIndex; i < sr->maxDecoders - 1; i++) {
				if(sr->decoders[sr->currentDecoderIndex]->isReady()) {
					sr->currentDecoderIndex.store(sr->currentDecoderIndex + i);
					found = true;
					break;
				}
			}
			if(!found) {
				syslog(LOG_ERR, "No more good decoders to use! Stopping speech recognition!");
				sr->listening.store(false);
				return;
			}
		}

        // Process the frames
        sr->voiceDetected.store(sr->decoders[sr->currentDecoderIndex]->processRawAudio(adbuf, frameCount));

        // Silence to speech transition
        // Trigger onSpeechStart
        if(sr->voiceDetected && !sr->inUtterance) {
            syslog(LOG_DEBUG, "Silence to speech transition");
            //sr->triggerEvents(ON_START_SPEECH, new EventData());
            sr->inUtterance.store(true);
			//b->playSoundEffect(SoundEffects::READY, false);
        }

        //Speech to silence transition
        //Trigger onSpeechEnd
        //And get hypothesis
        if(!sr->voiceDetected && sr->inUtterance) {
            sr->decoderIndexLock.unlock();
            syslog(LOG_DEBUG, "Speech to silence transition");
            //sr->triggerEvents(ON_END_SPEECH, new EventData()); //TODO: Add event data
            sr->inUtterance.store(false);
            sr->decoders[sr->currentDecoderIndex]->ready = false;
            sr->miscThreads.push_back(std::thread(endAndGetHypothesis, sr, sr->decoders[sr->currentDecoderIndex]));
	        sr->decoderIndexLock.unlock();

            usleep(100); // TODO: Windows portability

	        sr->decoderIndexLock.lock();
            for(unsigned short i = 0; i < sr->maxDecoders; i++) {
                if(sr->decoders[i]->isReady()) {
                    sr->currentDecoderIndex.store(i);
                }
            }
            sr->decoderIndexLock.unlock();
        }
    }

    //Close the device audio source
    ad_close(ad);

    sr->listening.store(false);
}

void PyramidASRService::pushToSpeakRecognition(PyramidASRService * sr) {

}

/// Applies previous updates and also initializes decoders if there weren't already when this object was constructed.
void PyramidASRService::applyUpdates() {
	updateLock.lock();
	syslog(LOG_DEBUG, "Starting to apply updates.");
	auto start = std::chrono::high_resolution_clock::now();
    if(isListening()) { // Decoders are in use so reload the ones not in use
		syslog(LOG_DEBUG, "Attempting to update while recognizing...");
		unsigned short k = decoders.size();
		unsigned short decodersDone[k];
		unsigned short decoderDoneCount = 0;
		while(decoderDoneCount != decoders.size()) {
			for(unsigned short i = 0; i < decoders.size(); i++) {
				unsigned short * c = std::find(decodersDone, decodersDone+k, i);
				if(c != decodersDone+k) {
					// This one was already updated, let it go
				}
				else {
					//Not updated yet
					if(i == currentDecoderIndex) {
						if(!inUtterance) {
							if(decoderDoneCount > 0) {
								decoderIndexLock.lock();
								currentDecoderIndex.store(decodersDone[0]);
								decoderIndexLock.unlock();
							}
						}
					}
					else {
						if(decoders[i]->getState() == SphinxHelper::DecoderState::UTTERANCE_STARTED) {
							decoders[i]->applyUpdateQueue();
							decoders[i]->startUtterance();
							decodersDone[decoderDoneCount] = i;
							decoderDoneCount++;
						}
					}
				}
			}
		}
    }
    else { // Decoders are not in use so restart them all now
    	syslog(LOG_DEBUG, "Applying updates while not recognizing...");
    	syslog(LOG_DEBUG, "%i decoders to update", decoders.size());
		for(unsigned short i = 0; i < decoders.size(); i++) {
		    syslog(LOG_DEBUG, "applying update...");
			decoders[i]->applyUpdateQueue();
			syslog(LOG_DEBUG, "starting utterance...");
			decoders[i]->startUtterance();

			if(i == 0) { // Select the first decoder that we update so it is ready ASAP
			    syslog(LOG_DEBUG, "locking decoderIndexLock...");
				decoderIndexLock.lock();
				syslog(LOG_DEBUG, "locked decoderIndexLock!");
				currentDecoderIndex.store(0);
				decoderIndexLock.unlock();
			}
		}

    }
    syslog(LOG_DEBUG, "Decoder updates applied.");
    updateLock.unlock();
    auto stop = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start);
    std::cout << "Time to apply decoder update: " << duration.count() << std::endl;
}

void PyramidASRService::startListening() {
    syslog(LOG_DEBUG, "startListening Called");
    if(listeningMode == Buckey::ASRService::ListeningMode::PUSH_TO_SPEAK) {
        paused.store(false);     
    }
    if(!isListening()) {
        endLoop.store(false);
        voiceDetected.store(false);
        listening.store(true);
        
        if(listeningMode == Buckey::ASRService::ListeningMode::CONTINUOUS) {
            recognizerLoop = std::thread(continuousSpeechRecognition, this);
        }
        else if(listeningMode == Buckey::ASRService::ListeningMode::PUSH_TO_SPEAK) {
            recognizerLoop = std::thread(pushToSpeakRecognition, this);
        }
        else {
            ///TODO: Warn that the other listening behaviors are currently unimplemented        
        }
    }
    else {
        if(listeningMode == Buckey::ASRService::ListeningMode::PUSH_TO_SPEAK) {
            ///TODO: Emit unpaused signal here
        }  
    }
}

void PyramidASRService::stopListening() {
    syslog(LOG_DEBUG, "stopListening called");
    if(listening.load()) {
        voiceDetected.store(false);
        
        if(listeningMode == Buckey::ASRService::ListeningMode::PUSH_TO_SPEAK) {
            paused.store(true);
            ///TODO: Emit pause signal        
        }
        else {  
            endLoop.store(true);
            listening.store(false);
            recognizerLoop.join();
        }
    }
}

void PyramidASRService::endAndGetHypothesis(PyramidASRService * sr, SphinxDecoder * sd) {
    sd->endUtterance();
    std::string hyp = sd->getHypothesis();
    if(hyp != "") { // Ignore false alarms
        sr->hypothesisCallback(hyp);
        syslog(LOG_DEBUG, "Got hypothesis: %s", hyp.c_str());
    }
    sd->startUtterance();
}

void PyramidASRService::setRecognitionMode(Buckey::ASRService::RecognitionMode mode) {
    //This changed the search mode
    
    syslog(LOG_DEBUG, "setRecognitionMode called");
    SphinxHelper::SearchMode m;
    switch(mode) {
        case Buckey::ASRService::RecognitionMode::LANGUAGE_MODEL:
            m = SphinxHelper::SearchMode::LM;
            break;
        case Buckey::ASRService::RecognitionMode::JSGF:
            m = SphinxHelper::SearchMode::JSGF_STRING;
            break;
        default:
            m = SphinxHelper::SearchMode::JSGF_STRING;
            break;
    }
    
    for(SphinxDecoder * sd : decoders) {
        sd->selectSearchMode(m);
    }
    
    applyUpdates();
}

void PyramidASRService::setListeningBehavior(Buckey::ASRService::ListeningMode mode) {
    //This changes the management thread
    syslog(LOG_DEBUG, "setListeningBehavior called");
    if(listeningMode != mode) {
        if(listening.load()) { // If we were recognizing before this, continue recognition
            endLoop.store(true); // Kill the old recognition loop      
            recognizerLoop.join();    
        
            listeningMode = mode; // Switch modes
            
            //Start up the new recognition thread
            if(listeningMode == Buckey::ASRService::ListeningMode::CONTINUOUS) {
                recognizerLoop = std::thread(continuousSpeechRecognition, this);
            }
            else if(listeningMode == Buckey::ASRService::ListeningMode::PUSH_TO_SPEAK) {
                recognizerLoop = std::thread(pushToSpeakRecognition, this);
            }
            else {
                ///TODO: Warn that the other listening behaviors are currently unimplemented        
            }
        }
        else { // We weren't recognizing before this, so just change the mode
            listeningMode = mode; // Switch modes     
        }
    }
}

bool PyramidASRService::isListening() {
    return listening.load();
}

void PyramidASRService::setGrammar(std::string jsgf) {
    syslog(LOG_DEBUG, "setGrammar called");
    for(SphinxDecoder * sd : decoders) {
        sd->updateJSGFString(jsgf);
    }
}

void PyramidASRService::setLanguageModel(std::string lmpath) {
    syslog(LOG_DEBUG, "setLanguageModel called");
    for(SphinxDecoder * sd : decoders) {
        sd->updateLM(lmpath);
    }
}

void PyramidASRService::setKeyword(std::string keyword) {
    //This modifies the search mode
    ///TOOD: Currently unsupported, you should really put your keywords into the JSGF grammar
}

void PyramidASRService::updateDictionary(std::string pathToDictionary) {
    for(SphinxDecoder * sd : decoders) {
        sd->updateDictionary(pathToDictionary);
    }
}

void PyramidASRService::updateAcousticModel(std::string pathToHMM) {
    for(SphinxDecoder * sd : decoders) {
        sd->updateAcousticModel(pathToHMM);
    }
}

void PyramidASRService::updateJSGFPath(std::string pathToJSGF) {
    for(SphinxDecoder * sd : decoders) {
        sd->updateJSGFFile(pathToJSGF);
    }
}

void PyramidASRService::updateLogPath(std::string pathToLog) {
    for(SphinxDecoder * sd : decoders) {
        sd->updateLoggingFile(pathToLog);
    }
}

bool PyramidASRService::wordExists(std::string word) {
    bool res = false;
	bool found = false;
	while(!found) {
		for(unsigned short i = 0; i < maxDecoders - 1; i++) {
			if(decoders[currentDecoderIndex]->isReady()) {
				res = decoders[currentDecoderIndex]->wordExists(word);
				found = true;
				break;
			}
		}
	}
	return res;
}

bool PyramidASRService::addWord(std::string word, std::string phones) {
	for(unsigned short i = 0; i < maxDecoders - 1; i++) {
		if(decoders[currentDecoderIndex]->getState() != SphinxHelper::DecoderState::ERROR && decoders[currentDecoderIndex]->getState() != SphinxHelper::DecoderState::NOT_INITIALIZED) {
			decoders[currentDecoderIndex]->addWord(word, phones);
			return true;
		}
		else {
			syslog(LOG_WARNING, "Unable to add word %s to decoder because it was not initialized or errored out!", word.c_str());
			return false;
		}
	}
}
