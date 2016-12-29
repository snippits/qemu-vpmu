#ifndef EFD_H
#define EFD_H

#include <stdio.h>  // FILE
#include <stdint.h> // uint8_t, uint32_t, etc.
#include <elf.h>    // Elf32_Ehdr

typedef struct PLTentry {
    uint32_t    addr;
    char        *name;
} PLTentry;

typedef struct EFD {
    FILE        *pfile;
    Elf32_Ehdr  elf_hdr;

    /* Section header */
    uint16_t    sec_num;
    Elf32_Shdr  *sec_hdr;
    uint16_t    ndx_shstr;
    char        *shstrtab;

    /* Symbol table */
    uint16_t    ndx_sym;
    uint32_t    sym_num;
    Elf32_Sym   *symtab;
    uint16_t    ndx_str;
    char        *strtab;

    /* Dynamic symbol table */
    uint16_t    ndx_dynsym;
    uint32_t    dynsym_num;
    Elf32_Sym   *dynsymtab;
    uint16_t    ndx_dynstr;
    char        *dynstrtab;

    /* Needed libraries in dynamic section */
    uint16_t    ndx_dyn;
    uint32_t    lib_num;
    Elf32_Dyn   *dyntab;

    /* Procedure linkage table */
    uint32_t    plt_num;    /* number of PLT entries */
    PLTentry    *plt;
} EFD;

EFD *efd_open_elf(const char *name);
void efd_close(EFD *efd);

#define efd_get_entry_point(efd)    ((efd)->elf_hdr.e_entry)
#define efd_get_sec_num(efd)        ((efd)->sec_num)
#define efd_get_sec_name(efd, i)    (((efd)->shstrtab) + ((efd)->sec_hdr[i].sh_name))
#define efd_get_sym_num(efd)        ((efd)->sym_num)
#define efd_get_sym_name(efd, i)    (((efd)->strtab) + ((efd)->symtab[i].st_name))
#define efd_get_sym_value(efd, i)   ((efd)->symtab[i].st_value)
#define efd_get_sym_size(efd, i)    ((efd)->symtab[i].st_size)
#define efd_get_sym_bind(efd, i)    ELF32_ST_BIND((efd)->symtab[i].st_info)
#define efd_get_sym_type(efd, i)    ELF32_ST_TYPE((efd)->symtab[i].st_info)
#define efd_get_sym_vis(efd, i)     (((efd)->symtab[i].st_other) & 0x3)
#define efd_get_sym_shndx(efd, i)   ((efd)->symtab[i].st_shndx)
#define efd_get_dynsym_num(efd)         ((efd)->dynsym_num)
#define efd_get_dynsym_name(efd, i)     (((efd)->dynstrtab) + ((efd)->dynsymtab[i].st_name))
#define efd_get_dynsym_value(efd, i)    ((efd)->dynsymtab[i].st_value)
#define efd_get_dynsym_size(efd, i)     ((efd)->dynsymtab[i].st_size)
#define efd_get_dynsym_bind(efd, i)     ELF32_ST_BIND((efd)->dynsymtab[i].st_info)
#define efd_get_dynsym_type(efd, i)     ELF32_ST_TYPE((efd)->dynsymtab[i].st_info)
#define efd_get_dynsym_vis(efd, i)      (((efd)->dynsymtab[i].st_other) & 0x3)
#define efd_get_dynsym_shndx(efd, i)    ((efd)->dynsymtab[i].st_shndx)
#define efd_get_needed_lib_num(efd)     ((efd)->lib_num)
#define efd_get_needed_lib_name(efd, i) ((efd)->dynstrtab + (efd)->dyntab[i].d_un.d_val)

uint16_t efd_find_sec_by_type(EFD *efd, uint32_t type);
uint16_t efd_find_sec_by_name(EFD *efd, char *name);
uint32_t efd_find_sym_by_name(EFD *efd, char *name);
uint32_t efd_find_dynsym_by_name(EFD *efd, char *name);

/* Architecture specific (PLT) */
#define efd_get_plt_num(efd)        ((efd)->plt_num)
#define efd_get_plt_name(efd, i)    ((efd)->plt[i].name)
#define efd_get_plt_addr(efd, i)    ((efd)->plt[i].addr)

// QEMU remove underscore after 2.6..... f***
#if defined(QEMU_ELF_H) || defined(_QEMU_ELF_H)
    #define STV_DEFAULT 0
	#define STV_HIDDEN	2
#endif

#endif
