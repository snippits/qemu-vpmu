/* FIXME Check the return value of malloc */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "efd.h"

/* ARM-specific */
static int parse_plt_for_arm(EFD *efd)
{
    uint16_t   ndx_relplt, ndx_plt;
    Elf32_Rel *reltab;
    uint32_t   plt_addr;
    int        i;
    size_t     size;

    /* Read relocation entries of "plt" section */
    ndx_relplt = efd_find_sec_by_name(efd, ".rel.plt");
    if (ndx_relplt == 0) return 0;
    reltab = (Elf32_Rel *)malloc(efd->sec_hdr[ndx_relplt].sh_size);

    fseek(efd->pfile, efd->sec_hdr[ndx_relplt].sh_offset, SEEK_SET);
    size = fread(reltab, efd->sec_hdr[ndx_relplt].sh_size, 1, efd->pfile);
    if (size == 0) return 0;

    efd->plt_num = efd->sec_hdr[ndx_relplt].sh_size / efd->sec_hdr[ndx_relplt].sh_entsize;
    efd->plt     = (PLTentry *)malloc(efd->plt_num * sizeof(PLTentry));

    /* Read "plt" section */
    ndx_plt  = efd_find_sec_by_name(efd, ".plt");
    plt_addr = efd->sec_hdr[ndx_plt].sh_addr;

    /* Remove first PLT entry */
    /* FIXME using more flexible way */
    plt_addr += 20;

    /* Read and parse PLT entries */
    /* FIXME No sanity check */
    for (i = 0; i < efd->plt_num; i++) {
        efd->plt[i].addr = plt_addr;
        efd->plt[i].name = efd_get_dynsym_name(efd, ELF32_R_SYM(reltab[i].r_info));
        plt_addr += 12;
    }

    free(reltab);

    /* Sanity check */
    if (efd->ndx_dynsym != efd->sec_hdr[ndx_relplt].sh_link) return -1;
    if (plt_addr != efd->sec_hdr[ndx_plt].sh_addr + efd->sec_hdr[ndx_plt].sh_size)
        return -1;

    return 0;
}

static void efd_error(EFD *efd, char *msg)
{
    fprintf(stderr, "EFD: %s\n", msg);
    efd_close(efd);
}

static EFD *efd_init()
{
    EFD *efd;
    efd = (EFD *)malloc(sizeof(EFD));
    memset(efd, 0, sizeof(EFD));

    return efd;
}

static bool elf_ident(unsigned char *ident)
{
    if (*(ident + EI_MAG0) != ELFMAG0) return false;
    if (*(ident + EI_MAG1) != ELFMAG1) return false;
    if (*(ident + EI_MAG2) != ELFMAG2) return false;
    if (*(ident + EI_MAG3) != ELFMAG3) return false;

    if (*(ident + EI_CLASS) != ELFCLASS32) return false;

    return true;
}

static bool elf_check_format(EFD *efd)
{
    Elf32_Ehdr *header = &efd->elf_hdr;

    if (elf_ident(header->e_ident) == false) {
        efd_error(efd, "Not a 32-bit ELF file!");
        return false;
    }

    if (header->e_type != ET_EXEC && header->e_type != ET_DYN) {
        efd_error(efd, "Not an executable or shared object!");
        return false;
    }

    return true;
}

EFD *efd_open_elf(char *name)
{
    EFD *  efd;
    size_t size;
    efd = efd_init();

    /* Open ELF file */
    efd->pfile = fopen(name, "rb");
    if (efd->pfile == NULL) {
        efd_error(efd, "Cannot open ELF file!");
        fprintf(stderr, "%s\n", name);
        return NULL;
    }

    /* Read ELF header */
    size = fread(&efd->elf_hdr, sizeof(Elf32_Ehdr), 1, efd->pfile);
    if (size == 0) return NULL;
    if (feof(efd->pfile)) {
        efd_error(efd, "Cannot read ELF header!");
        return NULL;
    }

    if (!elf_check_format(efd)) {
        fprintf(stderr, "file name %s\n", name);
        return NULL;
    }

    /* Read section header */
    efd->sec_num = efd->elf_hdr.e_shnum;
    efd->sec_hdr = (Elf32_Shdr *)malloc(sizeof(Elf32_Shdr) * efd->sec_num);

    fseek(efd->pfile, efd->elf_hdr.e_shoff, SEEK_SET);
    size = fread(efd->sec_hdr, sizeof(Elf32_Shdr), efd->sec_num, efd->pfile);
    if (size == 0) return NULL;

    if (feof(efd->pfile)) {
        efd_error(efd, "ELF file broken!");
        return NULL;
    }

    /* Read string table of section names */
    efd->ndx_shstr = efd->elf_hdr.e_shstrndx;
    efd->shstrtab  = (char *)malloc(efd->sec_hdr[efd->ndx_shstr].sh_size);

    fseek(efd->pfile, efd->sec_hdr[efd->ndx_shstr].sh_offset, SEEK_SET);
    size = fread(efd->shstrtab, efd->sec_hdr[efd->ndx_shstr].sh_size, 1, efd->pfile);
    if (size == 0) return NULL;

    /* Read symbol table */
    efd->ndx_sym = efd_find_sec_by_type(efd, SHT_SYMTAB);
    if (efd->ndx_sym == 0) {
        /* Stripped shared object, so we use "dynamic" symtab as symtab instead */
        efd->ndx_sym = efd_find_sec_by_type(efd, SHT_DYNSYM);
        if (efd->ndx_sym == 0) {
            efd_error(efd, "ELF file doesn't contain any symbol!");
            return NULL;
        }
    }

    efd->sym_num =
      efd->sec_hdr[efd->ndx_sym].sh_size / efd->sec_hdr[efd->ndx_sym].sh_entsize;
    efd->symtab = (Elf32_Sym *)malloc(efd->sec_hdr[efd->ndx_sym].sh_size);

    fseek(efd->pfile, efd->sec_hdr[efd->ndx_sym].sh_offset, SEEK_SET);
    size = fread(efd->symtab, efd->sec_hdr[efd->ndx_sym].sh_size, 1, efd->pfile);
    if (size == 0) return NULL;

    if (feof(efd->pfile)) {
        efd_error(efd, "ELF file broken!");
        return NULL;
    }

    /* Read string table of symbols */
    efd->ndx_str = efd->sec_hdr[efd->ndx_sym].sh_link;
    efd->strtab  = (char *)malloc(efd->sec_hdr[efd->ndx_str].sh_size);

    fseek(efd->pfile, efd->sec_hdr[efd->ndx_str].sh_offset, SEEK_SET);
    size = fread(efd->strtab, efd->sec_hdr[efd->ndx_str].sh_size, 1, efd->pfile);
    if (size == 0) return NULL;

    if (feof(efd->pfile)) {
        efd_error(efd, "ELF file broken!");
        return NULL;
    }

    /* Determine this elf contain dynamic symbol table or not */
    efd->ndx_dynsym = efd_find_sec_by_type(efd, SHT_DYNSYM);
    if (efd->ndx_dynsym == 0) {
        return efd; /* "dynsymtab" can be left empty */
    } else {
        /* Read dynamic symbol table */
        efd->dynsym_num = efd->sec_hdr[efd->ndx_dynsym].sh_size
                          / efd->sec_hdr[efd->ndx_dynsym].sh_entsize;
        efd->dynsymtab = (Elf32_Sym *)malloc(efd->sec_hdr[efd->ndx_dynsym].sh_size);

        fseek(efd->pfile, efd->sec_hdr[efd->ndx_dynsym].sh_offset, SEEK_SET);
        size =
          fread(efd->dynsymtab, efd->sec_hdr[efd->ndx_dynsym].sh_size, 1, efd->pfile);
        if (size == 0) return NULL;

        /* Read string table of dynamic symbols */
        efd->ndx_dynstr = efd->sec_hdr[efd->ndx_dynsym].sh_link;
        efd->dynstrtab  = (char *)malloc(efd->sec_hdr[efd->ndx_dynstr].sh_size);

        fseek(efd->pfile, efd->sec_hdr[efd->ndx_dynstr].sh_offset, SEEK_SET);
        size =
          fread(efd->dynstrtab, efd->sec_hdr[efd->ndx_dynstr].sh_size, 1, efd->pfile);
        if (size == 0) return NULL;

        /* Architecture specific. It is for ARM now */
        if (parse_plt_for_arm(efd) != 0) {
            efd_error(efd, "Parse PLT error!");
            return NULL;
        }

        /* Dynamic section */
        efd->ndx_dyn = efd_find_sec_by_type(efd, SHT_DYNAMIC);
        efd->dyntab  = (Elf32_Dyn *)malloc(efd->sec_hdr[efd->ndx_dyn].sh_size);

        fseek(efd->pfile, efd->sec_hdr[efd->ndx_dyn].sh_offset, SEEK_SET);
        size = fread(efd->dyntab, efd->sec_hdr[efd->ndx_dyn].sh_size, 1, efd->pfile);
        if (size == 0) return NULL;

        while (efd->dyntab[efd->lib_num].d_tag == DT_NEEDED) efd->lib_num++;
    }

    return efd;
}

void efd_close(EFD *efd)
{
    if (efd == NULL) return;

    if (efd->pfile != NULL) fclose(efd->pfile);

    if (efd->sec_hdr != NULL) free(efd->sec_hdr);

    if (efd->shstrtab != NULL) free(efd->shstrtab);

    if (efd->symtab != NULL) free(efd->symtab);

    if (efd->strtab != NULL) free(efd->strtab);

    if (efd->dynsymtab != NULL) free(efd->dynsymtab);

    if (efd->dynstrtab != NULL) free(efd->dynstrtab);

    if (efd->dyntab != NULL) free(efd->dyntab);

    if (efd->plt != NULL) free(efd->plt);

    free(efd);
}

uint16_t efd_find_sec_by_type(EFD *efd, uint32_t type)
{
    uint16_t i;
    for (i = 1; i < efd->sec_num; i++) {
        if (efd->sec_hdr[i].sh_type == type) return i;
    }

    return 0;
}

uint16_t efd_find_sec_by_name(EFD *efd, char *name)
{
    uint16_t i;
    for (i = 1; i < efd->sec_num; i++) {
        if (strcmp(name, efd_get_sec_name(efd, i)) == 0) return i;
    }

    return 0;
}

uint32_t efd_find_sym_by_name(EFD *efd, char *name)
{
    uint32_t i;
    for (i = 0; i < efd->sym_num; i++) {
        if (strcmp(name, efd_get_sym_name(efd, i)) == 0) return efd_get_sym_value(efd, i);
    }

    return 0;
}

uint32_t efd_find_dynsym_by_name(EFD *efd, char *name)
{
    uint32_t i;
    for (i = 0; i < efd->dynsym_num; i++) {
        if (strcmp(name, efd_get_dynsym_name(efd, i)) == 0)
            return efd_get_dynsym_value(efd, i);
    }

    return 0;
}

void dump_symbol_table(EFD *efd)
{
    /* Push special functions into hash table */
    for (int i = 0; i < efd_get_sym_num(efd); i++) {
        if ((efd_get_sym_type(efd, i) == STT_FUNC)
            && (efd_get_sym_vis(efd, i) == STV_DEFAULT)
            && (efd_get_sym_shndx(efd, i) != SHN_UNDEF)) {
            uint32_t vaddr    = efd_get_sym_value(efd, i) & 0xfffffffe;
            char *   funcName = efd_get_sym_name(efd, i);
            fprintf(stderr, "%s\n", funcName);
        }
    }
}
