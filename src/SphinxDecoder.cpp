#include "SphinxDecoder.h"
#include <iostream>
#include "syslog.h"

SphinxDecoder::SphinxDecoder(std::string decoderName, std::string pathToHMM, std::string pathToDictionary, std::string pathToLogFile) {
    name = decoderName;
    state.store(SphinxHelper::DecoderState::NOT_INITIALIZED);
    ready = false;
    jsgfFileSearchSet = false;
	jsgfStringSearchSet = false;
	lmSearchSet = false;
	inUtterance = false;
    
    strncpy(hmmPath, pathToHMM.c_str(), 255);
    hmmPath[255] = '\0';
    
	strncpy(logPath, pathToLogFile.c_str(), 255);
    logPath[255] = '\0';
    
    dictionaryPath = pathToDictionary;
	
	config = cmd_ln_init(NULL, ps_args(), TRUE,
				 "-hmm", hmmPath,
				 "-dict", dictionaryPath.c_str(),
				 "-logfn", logPath,
					NULL);
					
	ps_default_search_args(config);
	ps = ps_init(config);
	if(ps == NULL) {
	    ///TODO: Log error
		//Buckey::logError("Unable to initialize PS Decoder!");
		std::cerr << "Failed to initialize decoder!" << std::endl;
		state = SphinxHelper::DecoderState::ERROR;
	}
	#ifdef ENABLE_PS_STREAM
	   ps_start_stream(ps);
	#endif
		
    state = SphinxHelper::DecoderState::IDLE;
}

SphinxDecoder::~SphinxDecoder()
{
	state = SphinxHelper::DecoderState::NOT_INITIALIZED;
    ps_free(ps);
    //cmd_ln_free_r(config);
}

std::string SphinxDecoder::getName() {
	return name;
}

std::string SphinxDecoder::getHypothesis() {
	if(state == SphinxHelper::DecoderState::IDLE || state == SphinxHelper::DecoderState::NOT_INITIALIZED || state == SphinxHelper::DecoderState::ERROR) {
		//Buckey::logWarn("Attempting to get hypothesis from decoder that is not ready! Check to make sure it is not errored out!");
		///TODO: Log warn
		return "";
	}

	state = SphinxHelper::DecoderState::UTTERANCE_ENDING;
    const char* hyp = ps_get_hyp(ps, NULL);

    if (hyp != NULL) {
    	return std::string(hyp);
    }
    else {
		return "";
    }
}

void SphinxDecoder::startUtterance() {
	if(!(state == SphinxHelper::DecoderState::IDLE || state == SphinxHelper::DecoderState::UTTERANCE_ENDING)) {
		syslog(LOG_WARNING, "Attempting to start decoder that is not in the IDLE state! Check to make sure it is initialized!");
		return;
	}

	state = SphinxHelper::DecoderState::UTTERANCE_STARTED;
    if(ps_start_utt(ps) < 0) {
		state = SphinxHelper::DecoderState::ERROR;
        syslog(LOG_ERR, "Error while starting utterance for PS Decoder!");
    }
    else {
        ready = true;
        inUtterance = true;
    }
}

void SphinxDecoder::endUtterance() {
	if(state != SphinxHelper::DecoderState::UTTERANCE_STARTED) {
		syslog(LOG_WARNING, "Attempted to stop utterance of a decoder that did not start an utterance! Check that you started speech recognition!");
		return;
	}
	state = SphinxHelper::DecoderState::UTTERANCE_ENDING;
    ready = false;
    inUtterance = false;
    ps_end_utt(ps);
}

/// Returns true if speech was detected in the last frame
bool SphinxDecoder::processRawAudio(int16 adbuf[], int32 frameCount) {
    ps_process_raw(ps, adbuf, frameCount, FALSE, FALSE);
    return ps_get_in_speech(ps);
}

cmd_ln_t * SphinxDecoder::getConfig() {
    return config;
}

bool SphinxDecoder::isInUtterance() {
    return inUtterance;
}

void SphinxDecoder::addWord(std::string word, std::string phonemes) {
    ps_add_word(ps, word.c_str(), phonemes.c_str(), FALSE);
}

///Return true if the word is in the current dictionary
const bool SphinxDecoder::wordExists(std::string word) {
    return ps_lookup_word(ps, word.c_str()) != NULL;
}

void SphinxDecoder::updateAcousticModel(std::string pathToHMM, bool applyUpdate) {
    if(applyUpdate) {
        _updateAcousticModel(this, pathToHMM);    
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateAcousticModel, this, pathToHMM));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateAcousticModel(SphinxDecoder * d, std::string pathToHMM) {
    syslog(LOG_DEBUG, "_updateAcousticModel called");
    strncpy(d->hmmPath, pathToHMM.c_str(), 255);
    cmd_ln_set_str_r(d->config, "hmm", d->hmmPath);
    ps_reinit(d->ps, NULL);
}

void SphinxDecoder::updateDictionary(std::string pathToDict, bool applyUpdate) {
    if(applyUpdate) {
        _updateDictionary(this, pathToDict);    
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateDictionary, this, pathToDict));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateDictionary(SphinxDecoder * d, std::string pathToDict) {
    syslog(LOG_DEBUG, "_updateDictionary called");
    d->dictionaryPath = pathToDict;
    ps_load_dict(d->ps, pathToDict.c_str(), NULL, NULL);
    ///TODO: Read the output of the above function call to check for errors
}

void SphinxDecoder::updateJSGFFile(std::string jsgfPath, bool applyUpdate) {  
    if(applyUpdate) {
        _updateJSGFFile(this, jsgfPath);
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateJSGFFile, this, jsgfPath));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateJSGFFile(SphinxDecoder * d, std::string pathToJSGF) {
    syslog(LOG_DEBUG, "_updateJSGFFile called");
    if(d->jsgfFileSearchSet) {
        ps_unset_search(d->ps, JSGF_FILE_SEARCH_NAME);
    }
    d->jsgfFileSearchSet = true;
	d->jsgfPath = pathToJSGF;
	ps_set_jsgf_file(d->ps, JSGF_FILE_SEARCH_NAME, pathToJSGF.c_str());
}

void SphinxDecoder::updateJSGFString(std::string jsgf, bool applyUpdate) {  
    if(applyUpdate) {
        _updateJSGFString(this, jsgf);
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateJSGFString, this, jsgf));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateJSGFString(SphinxDecoder * d, std::string jsgf) {
    syslog(LOG_DEBUG, "_updateJSGFString called"); 
    d->jsgfString = jsgf;
    if(d->inUtterance) {
        d->endUtterance();    
    }
    if(d->jsgfStringSearchSet) {
        ps_unset_search(d->ps, JSGF_STRING_SEARCH_NAME);
    }
    d->jsgfStringSearchSet = true;
	ps_set_jsgf_string(d->ps, JSGF_STRING_SEARCH_NAME, jsgf.c_str());
    ///TODO: Error out about invalid JSGF string
}

void SphinxDecoder::updateLM(std::string lmPath, bool applyUpdate) {  
    if(applyUpdate) {
        _updateLM(this, lmPath);
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateLM, this, lmPath));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateLM(SphinxDecoder * d, std::string pathToLM) {
    syslog(LOG_DEBUG, "_updateLM called");
	d->lmPath = pathToLM;
	if(d->lmSearchSet) {
	    ps_unset_search(d->ps, LM_SEARCH_NAME);
	}
	d->lmSearchSet = true;
	ps_set_lm_file(d->ps, LM_SEARCH_NAME, pathToLM.c_str());
}

void SphinxDecoder::updateLoggingFile(std::string logPath, bool applyUpdate) {  
    if(applyUpdate) {
        _updateLoggingFile(this, logPath);
    }
    else {
        queueLock.lock();
        updateQueue.push(std::bind(_updateLoggingFile, this, logPath));
        queueLock.unlock();
    }
}

void SphinxDecoder::_updateLoggingFile(SphinxDecoder * d, std::string pathToLog) {
    syslog(LOG_DEBUG, "_updateLoggingFile called");
	strncpy(d->logPath, pathToLog.c_str(), 255);
	cmd_ln_set_str_r(d->config, "logfn", d->logPath);
	ps_reinit(d->ps, NULL);
}

void SphinxDecoder::selectSearchMode(SphinxHelper::SearchMode mode, bool applyUpdate) {
	if(applyUpdate) {
        _selectSearchMode(this, mode);	
	}
	else {
        queueLock.lock();
        updateQueue.push(std::bind(_selectSearchMode, this, mode));
        queueLock.unlock();	
	}
}

void SphinxDecoder::_selectSearchMode(SphinxDecoder * d, SphinxHelper::SearchMode mode) {
    d->recognitionMode = mode;
	int res;
	if(d->inUtterance) {
	   d->endUtterance();
	}
	if(mode == SphinxHelper::SearchMode::JSGF_FILE) {
	   res = ps_set_search(d->ps, JSGF_FILE_SEARCH_NAME);
	}
	else if(mode == SphinxHelper::SearchMode::JSGF_STRING) {
	   res = ps_set_search(d->ps, JSGF_STRING_SEARCH_NAME);
	}
	else if(mode == SphinxHelper::SearchMode::LM) {
	   res = ps_set_search(d->ps, LM_SEARCH_NAME);
	}
	else if(mode == SphinxHelper::SearchMode::ALLPHONE) {
	   res = ps_set_search(d->ps, ALLPHONE_SEARCH_NAME);
	}
	
	if(res != 0) { ///TODO: Maybe better error reporting than this?
        d->state.store(SphinxHelper::DecoderState::ERROR);
        syslog(LOG_ERR, "Error while switching to new search mode!");
	}
}

void SphinxDecoder::applyUpdateQueue() {
    syslog(LOG_DEBUG, "applyUpdateQueue called");
    queueLock.lock();
    syslog(LOG_DEBUG, "locked queueLock");
    while(!updateQueue.empty()) {
        std::function<void()> f = updateQueue.front();
        f();
        updateQueue.pop();
    }
    queueLock.unlock();
}

//Getters
const bool SphinxDecoder::isReady() {
    return ready;
}

const SphinxHelper::DecoderState SphinxDecoder::getState() {
	return state.load();
}

std::string SphinxDecoder::getDictionaryPath() {
	return dictionaryPath;
}

std::string SphinxDecoder::getLMPath() {
	return lmPath;
}

std::string SphinxDecoder::getJSGFPath() {
	return jsgfPath;
}

std::string SphinxDecoder::getJSGFString() {
    return jsgfString;
}

char * SphinxDecoder::getHMMPath() {
	return hmmPath;
}

char * SphinxDecoder::getLogPath() {
	return logPath;
}
