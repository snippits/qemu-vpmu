#ifndef __LINUX_MM_H_
#define __LINUX_MM_H_

/*
 * vm_flags in vm_area_struct, see mm_types.h.
 */
#define VM_NONE         0x00000000

#define VM_READ         0x00000001 /* currently active flags */
#define VM_WRITE        0x00000002
#define VM_EXEC         0x00000004
#define VM_SHARED       0x00000008
/* mprotect() hardcodes VM_MAYREAD >> 4 == VM_READ, and so for r/w/x bits. */
#define VM_MAYREAD      0x00000010 /* limits for mprotect() etc */
#define VM_MAYWRITE     0x00000020
#define VM_MAYEXEC      0x00000040
#define VM_MAYSHARE     0x00000080

#define VM_GROWSDOWN    0x00000100 /* general info on the segment */
#define VM_UFFD_MISSING 0x00000200 /* missing pages tracking */
#define VM_PFNMAP       0x00000400 /* Page-ranges managed without "struct page", just pure PFN */
#define VM_DENYWRITE    0x00000800 /* ETXTBSY on write attempts.. */
#define VM_UFFD_WP      0x00001000 /* wrprotect pages tracking */

#define VM_LOCKED       0x00002000
#define VM_IO           0x00004000 /* Memory mapped I/O or similar */

/* Used by sys_madvise() */
#define VM_SEQ_READ     0x00008000 /* App will access data sequentially */
#define VM_RAND_READ    0x00010000 /* App will not benefit from clustered reads */

#define VM_DONTCOPY     0x00020000 /* Do not copy this vma on fork */
#define VM_DONTEXPAND   0x00040000 /* Cannot expand with mremap() */
#define VM_LOCKONFAULT  0x00080000 /* Lock the pages covered when they are faulted in */
#define VM_ACCOUNT      0x00100000 /* Is a VM accounted object */
#define VM_NORESERVE    0x00200000 /* should the VM suppress accounting */
#define VM_HUGETLB      0x00400000 /* Huge TLB Page VM */
#define VM_ARCH_1       0x01000000 /* Architecture-specific flag */
#define VM_ARCH_2       0x02000000
#define VM_DONTDUMP     0x04000000 /* Do not include in the core dump */

#endif
