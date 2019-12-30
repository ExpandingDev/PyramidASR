#include "ASRService.h"
#include <mutex>
#include <string>
#include <atomic>

class PyramidASRService : public Buckey::ASRService {
	public:
	    void setListeningBehavior(uint8_t types);
            void setGrammar(std::string jsgf);
            void setLanguageModel(std::string lmpath);
            void setKeyword(std::string keyword);
            void setRecognitionMode(uint8_t mode);
            void startListening();
            void stopListening();
            
            PyramidASRService(std::string version, std::string name);	
	    virtual ~PyramidASRService();	

}