#ifndef _PANTHERA_APPLEKEYSTORE_FSSERVICES_H_
#define _PANTHERA_APPLEKEYSTORE_FSSERVICES_H_

#include <sys/cprotect.h>
#include <sys/types.h>
#include <uuid/uuid.h>

#define kAKSFileSystemKeyServices "AKSFileSystemKeyServices"
#define AKS_RAW_KEY_WRAPPEDKEY 0x1U

struct aks_cred_s {
	uint64_t inode;
	int32_t pid;
	uid_t uid;
	uuid_t volume_uuid;
	cp_key_revision_t key_revision;
};
typedef struct aks_cred_s *aks_cred_t;

struct aks_wrapped_key_s {
	void *key;
	uint32_t key_len;
	cp_key_class_t dp_class;
};
typedef struct aks_wrapped_key_s *aks_wrapped_key_t;

struct aks_raw_key_s {
	void *key;
	uint32_t key_len;
	void *iv_key;
	uint32_t iv_key_len;
	uint32_t flags;
};
typedef struct aks_raw_key_s *aks_raw_key_t;

typedef struct aks_file_system_key_services {
	int (*unwrap_key)(aks_cred_t access,
	    const aks_wrapped_key_t wrapped_key_in,
	    aks_raw_key_t key_out);
	int (*rewrap_key)(aks_cred_t access,
	    cp_key_class_t dp_class,
	    const aks_wrapped_key_t wrapped_key_in,
	    aks_wrapped_key_t wrapped_key_out);
	int (*new_key)(aks_cred_t access,
	    cp_key_class_t dp_class,
	    aks_raw_key_t key_out,
	    aks_wrapped_key_t wrapped_key_out);
	int (*backup_key)(aks_cred_t access,
	    const aks_wrapped_key_t wrapped_key_in,
	    aks_wrapped_key_t wrapped_key_out);
} aks_file_system_key_services_t;

#endif
