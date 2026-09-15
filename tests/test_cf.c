#include <CoreFoundation/CoreFoundation.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>

extern void __CFInitialize(void);
static void out(const char *s) { write(1, s, strlen(s)); }

int main(void) {
    __CFInitialize();
    char buf[256];

    CFStringRef str = CFSTR("Hello Panthera");
    CFStringGetCString(str, buf, sizeof(buf), kCFStringEncodingUTF8);
    snprintf(buf+128, 128, "CFString: %s\n", buf);
    out(buf+128);

    CFMutableDictionaryRef dict = CFDictionaryCreateMutable(NULL, 0,
        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFDictionarySetValue(dict, CFSTR("key"), CFSTR("value"));
    snprintf(buf, sizeof(buf), "CFDictionary count: %ld\n", CFDictionaryGetCount(dict));
    out(buf);

    CFMutableArrayRef arr = CFArrayCreateMutable(NULL, 0, &kCFTypeArrayCallBacks);
    CFArrayAppendValue(arr, CFSTR("item1"));
    CFArrayAppendValue(arr, CFSTR("item2"));
    snprintf(buf, sizeof(buf), "CFArray count: %ld\n", CFArrayGetCount(arr));
    out(buf);

    CFDataRef plistData = CFPropertyListCreateData(NULL, dict,
        kCFPropertyListXMLFormat_v1_0, 0, NULL);
    snprintf(buf, sizeof(buf), "Plist XML size: %ld bytes\n",
        plistData ? CFDataGetLength(plistData) : -1);
    out(buf);

    CFRunLoopRef rl = CFRunLoopGetCurrent();
    snprintf(buf, sizeof(buf), "CFRunLoop: %p\n", (void *)rl);
    out(buf);

    CFLocaleRef locale = CFLocaleCopyCurrent();
    CFStringRef locId = CFLocaleGetIdentifier(locale);
    CFStringGetCString(locId, buf, sizeof(buf), kCFStringEncodingUTF8);
    snprintf(buf+128, 128, "Locale: %s\n", buf);
    out(buf+128);

    if (plistData) CFRelease(plistData);
    CFRelease(arr);
    CFRelease(dict);
    CFRelease(locale);

    out("CoreFoundation: ALL PASS\n");
    return 0;
}
