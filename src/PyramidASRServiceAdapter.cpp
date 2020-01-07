#include "PyramidASRServiceAdapter.h"

    PyramidASRServiceAdapter::PyramidASRServiceAdapter(PyramidASRService * adaptee, std::string path) : Buckey::ASRServiceAdapter(adaptee, path) {
    DBus::MethodBase::pointer temp_method;
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setGrammar",sigc::mem_fun(adaptee, &PyramidASRService::setGrammar));
    temp_method->set_arg_name(0, "jsgf");
    
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setLanguageModel",sigc::mem_fun(adaptee, &PyramidASRService::setLanguageModel));
    temp_method->set_arg_name(0, "path");
    
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setRecognitionMode",sigc::mem_fun(adaptee, &PyramidASRService::setRecognitionMode));
    temp_method->set_arg_name(0, "mode");
    
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setListeningMode",sigc::mem_fun(adaptee, &PyramidASRService::setListeningMode));
    temp_method->set_arg_name(0, "mode");
    
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setAcousticModel",sigc::mem_fun(adaptee, &PyramidASRService::updateAcousticModel));
    temp_method->set_arg_name(0, "path");
    
    temp_method = this->create_method<void,std::string>("ca.l5.expandingdev.PyramidASR", "setDictionary",sigc::mem_fun(adaptee, &PyramidASRService::updateDictionary));
    temp_method->set_arg_name(0, "path");
    
    temp_method = this->create_method<bool,std::string,std::string>("ca.l5.expandingdev.PyramidASR", "addWord",sigc::mem_fun(adaptee, &PyramidASRService::addWord));
    temp_method->set_arg_name(0, "success");
    temp_method->set_arg_name(1, "word");
    temp_method->set_arg_name(2, "phonemes");
    
    temp_method = this->create_method<bool,std::string>("ca.l5.expandingdev.PyramidASR", "wordExists",sigc::mem_fun(adaptee, &PyramidASRService::wordExists));
    temp_method->set_arg_name(0, "exists");
    temp_method->set_arg_name(1, "word");
    
    temp_method = this->create_method<bool>("ca.l5.expandingdev.PyramidASR", "isListening",sigc::mem_fun(adaptee, &PyramidASRService::isListening));
    temp_method->set_arg_name(0, "listening");
    
}

std::shared_ptr<PyramidASRServiceAdapter> PyramidASRServiceAdapter::create(PyramidASRService * adaptee, std::string path){
    return std::shared_ptr<PyramidASRServiceAdapter>(new PyramidASRServiceAdapter(adaptee, path));
}
