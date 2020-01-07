#ifndef PYRAMIDASRSERVICEADAPTER_H
#define PYRAMIDASRSERVICEADAPTER_H

#include <dbus-cxx.h>
#include <memory>
#include <stdint.h>
#include <string>
#include "ASRServiceAdapter.h"
#include "PyramidASRService.h"

class PyramidASRServiceAdapter : public Buckey::ASRServiceAdapter {
    protected:
        PyramidASRServiceAdapter(PyramidASRService * adaptee, std::string path);
    public:
        static std::shared_ptr<PyramidASRServiceAdapter> create(PyramidASRService * adaptee, std::string path);
};
#endif /* PYRAMIDASRSERVICEADAPTER_H */
