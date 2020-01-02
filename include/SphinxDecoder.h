#ifndef SPHINXDECODER_H
#define SPHINXDECODER_H

#include <string>
#include <queue>
#include <iostream>
#include <atomic>
#include <functional>
#include <mutex>

#include <sphinxbase/err.h>
#include <sphinxbase/ad.h>
#include "SphinxHelper.h"
#include "pocketsphinx.h"
#include "cmd_ln.h"

#include "config.h"

#define JSGF_STRING_SEARCH_NAME "jsgf-string-search"
#define JSGF_FILE_SEARCH_NAME "jsgf-file-search"
#define LM_SEARCH_NAME "lm-search"
#define KEYWORD_SEARCH_NAME "keyword-search"
#define ALLPHONE_SEARCH_NAME "allphone-search"

/// All functions (and constructors and destructors) are synchronous. Any asynchronous tasks should be carried out by a managing class (SphinxRecognizer).
/// This class serves as a bare bones C++ wrapper for the CMU pocketsphinx library with a few added convenience functions.
class SphinxDecoder
{
    friend class PyramidASRService;
    public:
        /// The pathToSearchFile is either the path to the language model or the path to the JSGF grammar. Depends on the specified searchMode.
        SphinxDecoder(std::string decoderName, std::string pathToHMM = DEFAULT_HMM_PATH, std::string pathToDictionary = DEFAULT_DICT_PATH, std::string pathToLogFile = DEFAULT_LOG_PATH);
        ~SphinxDecoder();

        const bool isReady();
        bool processRawAudio(int16 adbuf[], int32 frameCount);

        // Dictionary manipulation
        const bool wordExists(std::string word);
        void addWord(std::string word, std::string phonemes);

        //Utterance
        bool isInUtterance();
        void startUtterance();
        void endUtterance();
        std::string getHypothesis();

        //Updating methods
        void updateAcousticModel(std::string pathToHMM, bool applyUpdate = false);
        void updateDictionary(std::string pathToDict, bool applyUpdate = false);
		void updateLM(std::string pathToLM, bool applyUpdate = false);
        void updateJSGFFile(std::string pathToJSGF, bool applyUpdate = false);
		void updateLoggingFile(std::string pathToLog, bool applyUpdate = false);
		void updateJSGFString(std::string jsgf, bool applyUpdate = false);
	
		void selectSearchMode(SphinxHelper::SearchMode mode, bool applyUpdate = false);

        //Getters
        std::string getDictionaryPath();
        char * getHMMPath();
        std::string getJSGFPath();
        std::string getJSGFString();
        std::string getLMPath();
        char * getLogPath();
        std::string getName();
        const SphinxHelper::DecoderState getState();

        cmd_ln_t * getConfig();

    protected:
        static void _updateAcousticModel(SphinxDecoder * d, std::string pathToHMM);
        static void _updateDictionary(SphinxDecoder * d, std::string pathToDict);        
		static void _updateLoggingFile(SphinxDecoder * d, std::string pathToLog);
		static void _updateLM(SphinxDecoder * d, std::string pathToLM);
        static void _updateJSGFFile(SphinxDecoder * d, std::string pathToJSGF);
		static void _updateJSGFString(SphinxDecoder * d, std::string jsgf);
		static void _selectSearchMode(SphinxDecoder * d, SphinxHelper::SearchMode mode);
		
        char hmmPath[256]; // path to the acoustic model
		char logPath[256]; // path to the logging file
        
        std::string dictionaryPath; // path to the dictionary file
        std::string lmPath; // path to the language model
        std::string jsgfPath; // path to the jsgf grammar
        std::string jsgfString;
		std::string name;

		SphinxHelper::SearchMode recognitionMode;
		bool ready;
		bool inUtterance;
		
		bool jsgfFileSearchSet;
		bool jsgfStringSearchSet;
		bool lmSearchSet;

		std::atomic<SphinxHelper::DecoderState> state;
		
		std::queue<std::function<void()>> updateQueue;
		std::mutex queueLock;
		void applyUpdateQueue();

        ps_decoder_t *ps;
        cmd_ln_t *config;
};

#endif // SPHINXDECODER_H
