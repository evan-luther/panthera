#ifndef PANTHERA_SECURITY_SECURITY_H
#define PANTHERA_SECURITY_SECURITY_H

#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#include <sys/types.h>

typedef int32_t OSStatus;
typedef uint32_t SecProtocolType;
typedef uint32_t SecAuthenticationType;
typedef uint32_t SecItemClass;
typedef uint32_t SecKeychainAttrType;
typedef struct __Authorization *AuthorizationRef;
typedef struct {
    uint8_t bytes[32];
} AuthorizationExternalForm;
typedef struct __SecAccess *SecAccessRef;
typedef struct __SecKeychain *SecKeychainRef;
typedef struct __SecKeychainItem *SecKeychainItemRef;
typedef struct __SecTrustedApplication *SecTrustedApplicationRef;

typedef struct {
    SecKeychainAttrType tag;
    UInt32 length;
    void *data;
} SecKeychainAttribute;

typedef struct {
    UInt32 count;
    SecKeychainAttribute *attr;
} SecKeychainAttributeList;

#ifndef noErr
#define noErr 0
#endif

enum {
    errAuthorizationSuccess = 0,
    errSecItemNotFound = -25300,
    kSecUseOnlyUID = 0x00000001
};

enum {
    kSecPreferencesDomainSystem = 2,
    kSecGenericPasswordItemClass = 0x67656e70,
    kSecServiceItemAttr = 0x73766365,
    kSecLabelItemAttr = 0x6c61626c,
    kSecDescriptionItemAttr = 0x64657363,
    kSecAccountItemAttr = 0x61636374
};

#define kSecMatchSearchList       CFSTR("kSecMatchSearchList")
#define kSecClass                 CFSTR("kSecClass")
#define kSecClassGenericPassword  CFSTR("kSecClassGenericPassword")
#define kSecAttrService           CFSTR("kSecAttrService")
#define kSecReturnRef             CFSTR("kSecReturnRef")

OSStatus AuthorizationCreateFromExternalForm(const AuthorizationExternalForm *extForm, AuthorizationRef *authorization);
OSStatus AuthorizationMakeExternalForm(AuthorizationRef authorization, AuthorizationExternalForm *extForm);
OSStatus SecAccessCreate(CFStringRef descriptor, CFArrayRef trustedlist, SecAccessRef *accessRef);
SecAccessRef SecAccessCreateWithOwnerAndACL(uid_t userId, gid_t groupId, uint32_t mode, CFArrayRef accessList, CFErrorRef *error);
OSStatus SecTrustedApplicationCreateFromPath(const char *path, SecTrustedApplicationRef *app);
OSStatus SecKeychainCopyDomainDefault(uint32_t domain, SecKeychainRef *keychain);
OSStatus SecKeychainItemCopyContent(SecKeychainItemRef itemRef, uint32_t *itemClass, void *attrList, UInt32 *length, void **outData);
OSStatus SecKeychainItemCreateFromContent(uint32_t itemClass,
                                          const void *attrList,
                                          UInt32 length,
                                          const void *data,
                                          SecKeychainRef keychain,
                                          SecAccessRef initialAccess,
                                          SecKeychainItemRef *itemRef);
OSStatus SecKeychainItemDelete(SecKeychainItemRef itemRef);
OSStatus SecKeychainItemFreeContent(void *attrList, void *data);
OSStatus SecKeychainItemModifyContent(SecKeychainItemRef itemRef, const void *attrList, UInt32 length, const void *data);
OSStatus SecItemCopyMatching(CFDictionaryRef query, CFTypeRef *result);

#endif /* PANTHERA_SECURITY_SECURITY_H */
