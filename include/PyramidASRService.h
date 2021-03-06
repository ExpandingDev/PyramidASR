#include <mutex>
#include <string>
#include <atomic>
#include <thread>
#include <vector>

#include <glib.h>

#include "ASRService.h"
#include "SphinxDecoder.h"

#define AUDIO_FRAME_SIZE 2048

enum class ListeningMode {
    CONTINUOUS, PUSH_TO_SPEAK
};

class PyramidASRService : public Buckey::ASRService {
	public:
        //DBus
        void setListeningMode(std::string mode);
        void setRecognitionMode(std::string mode);
        
        void setGrammar(std::string jsgf);
        void setLanguageModel(std::string lmpath);
        void setKeyword(std::string keyword);
        
        void startListening();
        void stopListening();
        
        PyramidASRService();	
        virtual ~PyramidASRService();
        
        //Controlling the decoders
           
        //Dictionary
        bool wordExists(std::string word);
        bool addWord(std::string word, std::string phones);
        
        void updateLMPath(std::string path);
        void updateAcousticModel(std::string path);
        void updateLogPath(std::string path);
        void updateJSGFPath(std::string path);
        void updateDictionary(std::string path);
        void applyUpdates();
        
        bool isListening();
           
        std::atomic<bool> running;
	        
	protected:	
	    ///Callback for when the utterance ends and the hypothesis needs extracted
        static void endAndGetHypothesis(PyramidASRService * sr, SphinxDecoder * sd);
        ///Management function for press to speak mode
        static void pushToSpeakRecognition(PyramidASRService * sr);
        ///Management function for continuous speech mode
        static void continuousSpeechRecognition(PyramidASRService * sr);
        
        std::atomic<unsigned short> currentDecoderIndex;
        std::vector<SphinxDecoder *> decoders;
        std::mutex decoderIndexLock;
        std::mutex updateLock;
        
        std::thread recognizerLoop; // The "management thread" that handles the sphinx decoders
        std::vector<std::thread> miscThreads; //Holds threads used to retreive the hypothesis        
        
        std::atomic<bool> inUtterance;
        std::atomic<bool> endLoop; // Setting to true requests the running management thread to exit
        std::atomic<bool> voiceDetected;
        
        std::atomic<bool> listening; // Set to true while the management thread is running
        std::atomic<bool> paused;
        
        SphinxHelper::SearchMode searchMode;
        ListeningMode listeningMode;
           
        GKeyFile * configFile;
        const char * CONFIG_FILENAME = "pyramid.conf";
        
        //Values read from the config file
        unsigned short maxDecoders;
        std::string hmmPath;
        std::string lmPath;
        std::string dictPath;
        std::string device;
	   
};