#ifndef PANTHERA_MOBILEGESTALT_H
#define PANTHERA_MOBILEGESTALT_H

#include <CoreFoundation/CoreFoundation.h>

#ifndef kMGQProductType
#define kMGQProductType CFSTR("ProductType")
#endif

static inline CFTypeRef
MGCopyAnswer(CFStringRef question, CFDictionaryRef options)
{
	(void)question;
	(void)options;
	return CFRetain(CFSTR("Panthera"));
}

#endif /* PANTHERA_MOBILEGESTALT_H */
