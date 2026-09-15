#ifndef _PANTHERA_APPLEUSBEHCIDIAGNOSTICS_H
#define _PANTHERA_APPLEUSBEHCIDIAGNOSTICS_H

#include <libkern/c++/OSObject.h>

class AppleUSBEHCI;

class AppleUSBEHCIDiagnostics
{
public:
    static OSObject *createDiagnostics(AppleUSBEHCI *)
    {
        return NULL;
    }
};

#endif
