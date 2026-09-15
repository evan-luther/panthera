#ifndef	_notify_ipc_server_
#define	_notify_ipc_server_

/* Module notify_ipc */

#include <string.h>
#include <mach/ndr.h>
#include <mach/boolean.h>
#include <mach/kern_return.h>
#include <mach/notify.h>
#include <mach/mach_types.h>
#include <mach/message.h>
#include <mach/mig_errors.h>
#include <mach/port.h>
	
/* BEGIN VOUCHER CODE */

#ifndef KERNEL
#if defined(__has_include)
#if __has_include(<mach/mig_voucher_support.h>)
#ifndef USING_VOUCHERS
#define USING_VOUCHERS
#endif
#ifndef __VOUCHER_FORWARD_TYPE_DECLS__
#define __VOUCHER_FORWARD_TYPE_DECLS__
#ifdef __cplusplus
extern "C" {
#endif
#ifndef __VOUCHER_FOWARD_TYPE_DECLS_SINGLE_ATTR
#define __VOUCHER_FOWARD_TYPE_DECLS_SINGLE_ATTR __unsafe_indexable
#endif
	extern boolean_t voucher_mach_msg_set(mach_msg_header_t * msg) __attribute__((weak_import));
#ifdef __cplusplus
}
#endif
#endif // __VOUCHER_FORWARD_TYPE_DECLS__
#endif // __has_include(<mach/mach_voucher_types.h>)
#endif // __has_include
#endif // !KERNEL
	
/* END VOUCHER CODE */

	
/* BEGIN MIG_STRNCPY_ZEROFILL CODE */

#if defined(__has_include)
#if __has_include(<mach/mig_strncpy_zerofill_support.h>)
#ifndef USING_MIG_STRNCPY_ZEROFILL
#define USING_MIG_STRNCPY_ZEROFILL
#endif
#ifndef __MIG_STRNCPY_ZEROFILL_FORWARD_TYPE_DECLS__
#define __MIG_STRNCPY_ZEROFILL_FORWARD_TYPE_DECLS__
#ifdef __cplusplus
extern "C" {
#endif
#ifndef __MIG_STRNCPY_ZEROFILL_FORWARD_TYPE_DECLS_CSTRING_ATTR
#define __MIG_STRNCPY_ZEROFILL_FORWARD_TYPE_DECLS_CSTRING_COUNTEDBY_ATTR(C) __unsafe_indexable
#endif
	extern int mig_strncpy_zerofill(char * dest, const char * src, int len) __attribute__((weak_import));
#ifdef __cplusplus
}
#endif
#endif /* __MIG_STRNCPY_ZEROFILL_FORWARD_TYPE_DECLS__ */
#endif /* __has_include(<mach/mig_strncpy_zerofill_support.h>) */
#endif /* __has_include */
	
/* END MIG_STRNCPY_ZEROFILL CODE */


#ifdef AUTOTEST
#ifndef FUNCTION_PTR_T
#define FUNCTION_PTR_T
typedef void (*function_ptr_t)(mach_port_t, char *, mach_msg_type_number_t);
typedef struct {
        char            * name;
        function_ptr_t  function;
} function_table_entry;
typedef function_table_entry   *function_table_t;
#endif /* FUNCTION_PTR_T */
#endif /* AUTOTEST */

#ifndef	notify_ipc_MSG_COUNT
#define	notify_ipc_MSG_COUNT	33
#endif	/* notify_ipc_MSG_COUNT */

#include <Availability.h>
#include <mach/std_types.h>
#include <mach/mig.h>
#include <mach/mig.h>
#include <mach/mach_types.h>
#include <sys/types.h>

#ifdef __BeforeMigServerHeader
__BeforeMigServerHeader
#endif /* __BeforeMigServerHeader */

#ifndef MIG_SERVER_ROUTINE
#define MIG_SERVER_ROUTINE
#endif


/* Routine _notify_server_check */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_check
(
	mach_port_t server,
	int token,
	int *check,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_get_state */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_get_state
(
	mach_port_t server,
	int token,
	uint64_t *state,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_suspend */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_suspend
(
	mach_port_t server,
	int token,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_resume */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_resume
(
	mach_port_t server,
	int token,
	int *status,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_suspend_pid */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_suspend_pid
(
	mach_port_t server,
	int pid,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_resume_pid */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_resume_pid
(
	mach_port_t server,
	int pid,
	audit_token_t audit
);

/* Routine _notify_server_post_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_post_2
(
	mach_port_t server,
	caddr_t name,
	uint64_t *name_id,
	int *status,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_post_3 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_post_3
(
	mach_port_t server,
	uint64_t name_id,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_post_4 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_post_4
(
	mach_port_t server,
	caddr_t name,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_register_plain_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_plain_2
(
	mach_port_t server,
	caddr_t name,
	int token,
	audit_token_t audit
);

/* Routine _notify_server_register_check_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_check_2
(
	mach_port_t server,
	caddr_t name,
	int token,
	int *size,
	int *slot,
	uint64_t *name_id,
	int *status,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_register_signal_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_signal_2
(
	mach_port_t server,
	caddr_t name,
	int token,
	int sig,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_register_file_descriptor_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_file_descriptor_2
(
	mach_port_t server,
	caddr_t name,
	int token,
	mach_port_t fileport,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_register_mach_port_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_mach_port_2
(
	mach_port_t server,
	caddr_t name,
	int token,
	mach_port_t port,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_cancel_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_cancel_2
(
	mach_port_t server,
	int token,
	audit_token_t audit
);

/* Routine _notify_server_get_state_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_get_state_2
(
	mach_port_t server,
	uint64_t name_id,
	uint64_t *state,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_get_state_3 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_get_state_3
(
	mach_port_t server,
	int token,
	uint64_t *state,
	uint64_t *nid,
	int *status,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_set_state_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_set_state_2
(
	mach_port_t server,
	uint64_t name_id,
	uint64_t state,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* Routine _notify_server_set_state_3 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_set_state_3
(
	mach_port_t server,
	int token,
	uint64_t state,
	uint64_t *nid,
	int *status,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_monitor_file_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_monitor_file_2
(
	mach_port_t server,
	int token,
	caddr_t path,
	mach_msg_type_number_t pathCnt,
	int flags,
	audit_token_t audit
);

/* Routine _notify_server_regenerate */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_regenerate
(
	mach_port_t server,
	caddr_t name,
	int token,
	uint32_t reg_type,
	mach_port_t port,
	int sig,
	int prev_slot,
	uint64_t prev_state,
	uint64_t prev_time,
	caddr_t path,
	mach_msg_type_number_t pathCnt,
	int path_flags,
	int *new_slot,
	uint64_t *new_name_id,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_checkin */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_checkin
(
	mach_port_t server,
	uint32_t *version,
	uint32_t *server_pid,
	int *status,
	audit_token_t audit
);

/* Routine _notify_server_dump */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_dump
(
	mach_port_t server,
	mach_port_t fileport,
	audit_token_t audit
);

/* Routine _notify_generate_common_port */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_generate_common_port
(
	mach_port_t server,
	uint32_t *status,
	mach_port_t *port,
	audit_token_t audit
);

/* SimpleRoutine _notify_server_register_common_port */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_common_port
(
	mach_port_t server,
	caddr_t name,
	int token,
	audit_token_t audit
);

/* Routine _notify_server_register_mach_port_3 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __notify_server_register_mach_port_3
(
	mach_port_t server,
	caddr_t name,
	int token,
	uint32_t *status,
	mach_port_t *port,
	audit_token_t audit
);

/* Routine _filtered_notify_server_checkin */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __filtered_notify_server_checkin
(
	mach_port_t server,
	uint32_t *version,
	uint32_t *server_pid,
	int *status,
	audit_token_t audit
);

/* SimpleRoutine _filtered_notify_server_post */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __filtered_notify_server_post
(
	mach_port_t server,
	caddr_t name,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* Routine _filtered_notify_server_regenerate */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __filtered_notify_server_regenerate
(
	mach_port_t server,
	caddr_t name,
	int token,
	uint32_t reg_type,
	mach_port_t port,
	int sig,
	int prev_slot,
	uint64_t prev_state,
	uint64_t prev_time,
	caddr_t path,
	mach_msg_type_number_t pathCnt,
	int path_flags,
	int *new_slot,
	uint64_t *new_name_id,
	int *status,
	audit_token_t audit
);

/* SimpleRoutine _filtered_notify_server_set_state_2 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __filtered_notify_server_set_state_2
(
	mach_port_t server,
	uint64_t name_id,
	uint64_t state,
	boolean_t claim_root_access,
	audit_token_t audit
);

/* Routine _filtered_notify_server_set_state_3 */
#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
MIG_SERVER_ROUTINE
kern_return_t __filtered_notify_server_set_state_3
(
	mach_port_t server,
	int token,
	uint64_t state,
	uint64_t *nid,
	int *status,
	boolean_t claim_root_access,
	audit_token_t audit
);

#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
boolean_t notify_ipc_server(
		mach_msg_header_t *InHeadP,
		mach_msg_header_t *OutHeadP);

#ifdef	mig_external
mig_external
#else
extern
#endif	/* mig_external */
mig_routine_t notify_ipc_server_routine(
		mach_msg_header_t *InHeadP);


/* Description of this subsystem, for use in direct RPC */
extern const struct _notify_ipc_subsystem {
	mig_server_routine_t	server;	/* Server routine */
	mach_msg_id_t	start;	/* Min routine number */
	mach_msg_id_t	end;	/* Max routine number + 1 */
	unsigned int	maxsize;	/* Max msg size */
	vm_address_t	reserved;	/* Reserved */
	struct routine_descriptor	/* Array of routine descriptors */
		routine[33];
} _notify_ipc_subsystem;

/* typedefs for all requests */

#ifndef __Request__notify_ipc_subsystem__defined
#define __Request__notify_ipc_subsystem__defined

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_check_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_get_state_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_suspend_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_resume_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int pid;
	} __Request___notify_server_suspend_pid_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int pid;
	} __Request___notify_server_resume_pid_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		boolean_t claim_root_access;
	} __Request___notify_server_post_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		uint64_t name_id;
		boolean_t claim_root_access;
	} __Request___notify_server_post_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		boolean_t claim_root_access;
	} __Request___notify_server_post_4_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_plain_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_check_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
		int sig;
	} __Request___notify_server_register_signal_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t fileport;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_file_descriptor_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t port;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_mach_port_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_cancel_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		uint64_t name_id;
	} __Request___notify_server_get_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
	} __Request___notify_server_get_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		uint64_t name_id;
		uint64_t state;
		boolean_t claim_root_access;
	} __Request___notify_server_set_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
		uint64_t state;
		boolean_t claim_root_access;
	} __Request___notify_server_set_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_ool_descriptor_t path;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		int token;
		mach_msg_type_number_t pathCnt;
		int flags;
	} __Request___notify_server_monitor_file_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t port;
		mach_msg_ool_descriptor_t path;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
		uint32_t reg_type;
		int sig;
		int prev_slot;
		uint64_t prev_state;
		uint64_t prev_time;
		mach_msg_type_number_t pathCnt;
		int path_flags;
	} __Request___notify_server_regenerate_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
	} __Request___notify_server_checkin_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t fileport;
		/* end of the kernel processed data */
	} __Request___notify_server_dump_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
	} __Request___notify_generate_common_port_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_common_port_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
	} __Request___notify_server_register_mach_port_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
	} __Request___filtered_notify_server_checkin_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		boolean_t claim_root_access;
	} __Request___filtered_notify_server_post_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t port;
		mach_msg_ool_descriptor_t path;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		mach_msg_type_number_t nameOffset; /* MiG doesn't use it */
		mach_msg_type_number_t nameCnt;
		char name[512];
		int token;
		uint32_t reg_type;
		int sig;
		int prev_slot;
		uint64_t prev_state;
		uint64_t prev_time;
		mach_msg_type_number_t pathCnt;
		int path_flags;
	} __Request___filtered_notify_server_regenerate_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		uint64_t name_id;
		uint64_t state;
		boolean_t claim_root_access;
	} __Request___filtered_notify_server_set_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		int token;
		uint64_t state;
		boolean_t claim_root_access;
	} __Request___filtered_notify_server_set_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif
#endif /* !__Request__notify_ipc_subsystem__defined */


/* union of all requests */

#ifndef __RequestUnion___notify_ipc_subsystem__defined
#define __RequestUnion___notify_ipc_subsystem__defined
union __RequestUnion___notify_ipc_subsystem {
	__Request___notify_server_check_t Request__notify_server_check;
	__Request___notify_server_get_state_t Request__notify_server_get_state;
	__Request___notify_server_suspend_t Request__notify_server_suspend;
	__Request___notify_server_resume_t Request__notify_server_resume;
	__Request___notify_server_suspend_pid_t Request__notify_server_suspend_pid;
	__Request___notify_server_resume_pid_t Request__notify_server_resume_pid;
	__Request___notify_server_post_2_t Request__notify_server_post_2;
	__Request___notify_server_post_3_t Request__notify_server_post_3;
	__Request___notify_server_post_4_t Request__notify_server_post_4;
	__Request___notify_server_register_plain_2_t Request__notify_server_register_plain_2;
	__Request___notify_server_register_check_2_t Request__notify_server_register_check_2;
	__Request___notify_server_register_signal_2_t Request__notify_server_register_signal_2;
	__Request___notify_server_register_file_descriptor_2_t Request__notify_server_register_file_descriptor_2;
	__Request___notify_server_register_mach_port_2_t Request__notify_server_register_mach_port_2;
	__Request___notify_server_cancel_2_t Request__notify_server_cancel_2;
	__Request___notify_server_get_state_2_t Request__notify_server_get_state_2;
	__Request___notify_server_get_state_3_t Request__notify_server_get_state_3;
	__Request___notify_server_set_state_2_t Request__notify_server_set_state_2;
	__Request___notify_server_set_state_3_t Request__notify_server_set_state_3;
	__Request___notify_server_monitor_file_2_t Request__notify_server_monitor_file_2;
	__Request___notify_server_regenerate_t Request__notify_server_regenerate;
	__Request___notify_server_checkin_t Request__notify_server_checkin;
	__Request___notify_server_dump_t Request__notify_server_dump;
	__Request___notify_generate_common_port_t Request__notify_generate_common_port;
	__Request___notify_server_register_common_port_t Request__notify_server_register_common_port;
	__Request___notify_server_register_mach_port_3_t Request__notify_server_register_mach_port_3;
	__Request___filtered_notify_server_checkin_t Request__filtered_notify_server_checkin;
	__Request___filtered_notify_server_post_t Request__filtered_notify_server_post;
	__Request___filtered_notify_server_regenerate_t Request__filtered_notify_server_regenerate;
	__Request___filtered_notify_server_set_state_2_t Request__filtered_notify_server_set_state_2;
	__Request___filtered_notify_server_set_state_3_t Request__filtered_notify_server_set_state_3;
};
#endif /* __RequestUnion___notify_ipc_subsystem__defined */
/* typedefs for all replies */

#ifndef __Reply__notify_ipc_subsystem__defined
#define __Reply__notify_ipc_subsystem__defined

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int check;
		int status;
	} __Reply___notify_server_check_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t state;
		int status;
	} __Reply___notify_server_get_state_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int status;
	} __Reply___notify_server_suspend_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int status;
	} __Reply___notify_server_resume_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_suspend_pid_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_resume_pid_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t name_id;
		int status;
	} __Reply___notify_server_post_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_post_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_post_4_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_register_plain_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int size;
		int slot;
		uint64_t name_id;
		int status;
	} __Reply___notify_server_register_check_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_register_signal_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_register_file_descriptor_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_register_mach_port_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_cancel_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t state;
		int status;
	} __Reply___notify_server_get_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t state;
		uint64_t nid;
		int status;
	} __Reply___notify_server_get_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_set_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t nid;
		int status;
	} __Reply___notify_server_set_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_monitor_file_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int new_slot;
		uint64_t new_name_id;
		int status;
	} __Reply___notify_server_regenerate_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint32_t version;
		uint32_t server_pid;
		int status;
	} __Reply___notify_server_checkin_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_dump_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t port;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		uint32_t status;
	} __Reply___notify_generate_common_port_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___notify_server_register_common_port_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		/* start of the kernel processed data */
		mach_msg_body_t msgh_body;
		mach_msg_port_descriptor_t port;
		/* end of the kernel processed data */
		NDR_record_t NDR;
		uint32_t status;
	} __Reply___notify_server_register_mach_port_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint32_t version;
		uint32_t server_pid;
		int status;
	} __Reply___filtered_notify_server_checkin_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___filtered_notify_server_post_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		int new_slot;
		uint64_t new_name_id;
		int status;
	} __Reply___filtered_notify_server_regenerate_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
	} __Reply___filtered_notify_server_set_state_2_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif

#ifdef  __MigPackStructs
#pragma pack(push, 4)
#endif
	typedef struct {
		mach_msg_header_t Head;
		NDR_record_t NDR;
		kern_return_t RetCode;
		uint64_t nid;
		int status;
	} __Reply___filtered_notify_server_set_state_3_t __attribute__((unused));
#ifdef  __MigPackStructs
#pragma pack(pop)
#endif
#endif /* !__Reply__notify_ipc_subsystem__defined */


/* union of all replies */

#ifndef __ReplyUnion___notify_ipc_subsystem__defined
#define __ReplyUnion___notify_ipc_subsystem__defined
union __ReplyUnion___notify_ipc_subsystem {
	__Reply___notify_server_check_t Reply__notify_server_check;
	__Reply___notify_server_get_state_t Reply__notify_server_get_state;
	__Reply___notify_server_suspend_t Reply__notify_server_suspend;
	__Reply___notify_server_resume_t Reply__notify_server_resume;
	__Reply___notify_server_suspend_pid_t Reply__notify_server_suspend_pid;
	__Reply___notify_server_resume_pid_t Reply__notify_server_resume_pid;
	__Reply___notify_server_post_2_t Reply__notify_server_post_2;
	__Reply___notify_server_post_3_t Reply__notify_server_post_3;
	__Reply___notify_server_post_4_t Reply__notify_server_post_4;
	__Reply___notify_server_register_plain_2_t Reply__notify_server_register_plain_2;
	__Reply___notify_server_register_check_2_t Reply__notify_server_register_check_2;
	__Reply___notify_server_register_signal_2_t Reply__notify_server_register_signal_2;
	__Reply___notify_server_register_file_descriptor_2_t Reply__notify_server_register_file_descriptor_2;
	__Reply___notify_server_register_mach_port_2_t Reply__notify_server_register_mach_port_2;
	__Reply___notify_server_cancel_2_t Reply__notify_server_cancel_2;
	__Reply___notify_server_get_state_2_t Reply__notify_server_get_state_2;
	__Reply___notify_server_get_state_3_t Reply__notify_server_get_state_3;
	__Reply___notify_server_set_state_2_t Reply__notify_server_set_state_2;
	__Reply___notify_server_set_state_3_t Reply__notify_server_set_state_3;
	__Reply___notify_server_monitor_file_2_t Reply__notify_server_monitor_file_2;
	__Reply___notify_server_regenerate_t Reply__notify_server_regenerate;
	__Reply___notify_server_checkin_t Reply__notify_server_checkin;
	__Reply___notify_server_dump_t Reply__notify_server_dump;
	__Reply___notify_generate_common_port_t Reply__notify_generate_common_port;
	__Reply___notify_server_register_common_port_t Reply__notify_server_register_common_port;
	__Reply___notify_server_register_mach_port_3_t Reply__notify_server_register_mach_port_3;
	__Reply___filtered_notify_server_checkin_t Reply__filtered_notify_server_checkin;
	__Reply___filtered_notify_server_post_t Reply__filtered_notify_server_post;
	__Reply___filtered_notify_server_regenerate_t Reply__filtered_notify_server_regenerate;
	__Reply___filtered_notify_server_set_state_2_t Reply__filtered_notify_server_set_state_2;
	__Reply___filtered_notify_server_set_state_3_t Reply__filtered_notify_server_set_state_3;
};
#endif /* __ReplyUnion___notify_ipc_subsystem__defined */

#ifndef subsystem_to_name_map_notify_ipc
#define subsystem_to_name_map_notify_ipc \
    { "_notify_server_check", 1002 },\
    { "_notify_server_get_state", 1003 },\
    { "_notify_server_suspend", 1004 },\
    { "_notify_server_resume", 1005 },\
    { "_notify_server_suspend_pid", 1006 },\
    { "_notify_server_resume_pid", 1007 },\
    { "_notify_server_post_2", 1008 },\
    { "_notify_server_post_3", 1009 },\
    { "_notify_server_post_4", 1010 },\
    { "_notify_server_register_plain_2", 1011 },\
    { "_notify_server_register_check_2", 1012 },\
    { "_notify_server_register_signal_2", 1013 },\
    { "_notify_server_register_file_descriptor_2", 1014 },\
    { "_notify_server_register_mach_port_2", 1015 },\
    { "_notify_server_cancel_2", 1016 },\
    { "_notify_server_get_state_2", 1017 },\
    { "_notify_server_get_state_3", 1018 },\
    { "_notify_server_set_state_2", 1019 },\
    { "_notify_server_set_state_3", 1020 },\
    { "_notify_server_monitor_file_2", 1021 },\
    { "_notify_server_regenerate", 1022 },\
    { "_notify_server_checkin", 1023 },\
    { "_notify_server_dump", 1024 },\
    { "_notify_generate_common_port", 1025 },\
    { "_notify_server_register_common_port", 1026 },\
    { "_notify_server_register_mach_port_3", 1027 },\
    { "_filtered_notify_server_checkin", 1028 },\
    { "_filtered_notify_server_post", 1029 },\
    { "_filtered_notify_server_regenerate", 1030 },\
    { "_filtered_notify_server_set_state_2", 1031 },\
    { "_filtered_notify_server_set_state_3", 1032 }
#endif

#ifdef __AfterMigServerHeader
__AfterMigServerHeader
#endif /* __AfterMigServerHeader */

#endif	 /* _notify_ipc_server_ */
