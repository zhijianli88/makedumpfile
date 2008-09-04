/*
 * x86_64.c
 *
 * Copyright (C) 2006, 2007  NEC Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */
#ifdef __x86_64__

#include "makedumpfile.h"

int
is_vmalloc_addr(ulong vaddr)
{
	/*
	 *  vmalloc, virtual memmap, and module space as VMALLOC space.
	 */
	return ((vaddr >= VMALLOC_START && vaddr <= VMALLOC_END)
	    || (vaddr >= VMEMMAP_START && vaddr <= VMEMMAP_END)
	    || (vaddr >= MODULES_VADDR && vaddr <= MODULES_END));
}

int
get_phys_base_x86_64(void)
{
	int i;
	struct pt_load_segment *pls;

	/*
	 * Get the relocatable offset
	 */
	info->phys_base = 0; /* default/traditional */

	for (i = 0; i < info->num_load_memory; i++) {
		pls = &info->pt_load_segments[i];
		if ((pls->virt_start >= __START_KERNEL_map) &&
		    !(is_vmalloc_addr(pls->virt_start))) {

			info->phys_base = pls->phys_start -
			    (pls->virt_start & ~(__START_KERNEL_map));

			break;
		}
	}

	return TRUE;
}

int
get_max_physmem_size_x86_64(void)
{
	info->section_size_bits = _SECTION_SIZE_BITS;

	/*
	 * On linux-2.6.26, MAX_PHYSMEM_BITS is changed to 44 from 40.
	 */
	if (info->kernel_version < VERSION_LINUX_2_6_26)
		info->max_physmem_bits  = _MAX_PHYSMEM_BITS_ORIG;
	else
		info->max_physmem_bits  = _MAX_PHYSMEM_BITS_2_6_26;

	return TRUE;
}

/*
 * Translate a virtual address to a physical address by using 4 levels paging.
 */
unsigned long long
vtop4_x86_64(unsigned long vaddr)
{
	unsigned long page_dir, pml4, pgd_paddr, pgd_pte, pmd_paddr, pmd_pte;
	unsigned long pte_paddr, pte;

	if (SYMBOL(init_level4_pgt) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of init_level4_pgt.\n");
		return NOT_PADDR;
	}

	/*
	 * Get PGD.
	 */
	page_dir  = SYMBOL(init_level4_pgt);
	page_dir += pml4_index(vaddr) * sizeof(unsigned long);
	if (!readmem(VADDR, page_dir, &pml4, sizeof pml4)) {
		ERRMSG("Can't get pml4 (page_dir:%lx).\n", page_dir);
		return NOT_PADDR;
	}
	if (!(pml4 & _PAGE_PRESENT)) {
		ERRMSG("Can't get a valid pml4.\n");
		return NOT_PADDR;
	}

	/*
	 * Get PUD.
	 */
	pgd_paddr  = pml4 & PHYSICAL_PAGE_MASK;
	pgd_paddr += pgd_index(vaddr) * sizeof(unsigned long);
	if (!readmem(PADDR, pgd_paddr, &pgd_pte, sizeof pgd_pte)) {
		ERRMSG("Can't get pgd_pte (pgd_paddr:%lx).\n", pgd_paddr);
		return NOT_PADDR;
	}
	if (!(pgd_pte & _PAGE_PRESENT)) {
		ERRMSG("Can't get a valid pgd_pte.\n");
		return NOT_PADDR;
	}

	/*
	 * Get PMD.
	 */
	pmd_paddr  = pgd_pte & PHYSICAL_PAGE_MASK;
	pmd_paddr += pmd_index(vaddr) * sizeof(unsigned long);
	if (!readmem(PADDR, pmd_paddr, &pmd_pte, sizeof pmd_pte)) {
		ERRMSG("Can't get pmd_pte (pmd_paddr:%lx).\n", pmd_paddr);
		return NOT_PADDR;
	}
	if (!(pmd_pte & _PAGE_PRESENT)) {
		ERRMSG("Can't get a valid pmd_pte.\n");
		return NOT_PADDR;
	}
	if (pmd_pte & _PAGE_PSE)
		return (PAGEBASE(pmd_pte) & PHYSICAL_PAGE_MASK)
			+ (vaddr & ~_2MB_PAGE_MASK);

	/*
	 * Get PTE.
	 */
	pte_paddr  = pmd_pte & PHYSICAL_PAGE_MASK;
	pte_paddr += pte_index(vaddr) * sizeof(unsigned long);
	if (!readmem(PADDR, pte_paddr, &pte, sizeof pte)) {
		ERRMSG("Can't get pte (pte_paddr:%lx).\n", pte_paddr);
		return NOT_PADDR;
	}
	if (!(pte & _PAGE_PRESENT)) {
		ERRMSG("Can't get a valid pte.\n");
		return NOT_PADDR;
	}
	return (PAGEBASE(pte) & PHYSICAL_PAGE_MASK) + PAGEOFFSET(vaddr);
}

unsigned long long
vaddr_to_paddr_x86_64(unsigned long vaddr)
{
	unsigned long phys_base;
	unsigned long long paddr;

	/*
	 * Check the relocatable kernel.
	 */
	if (SYMBOL(phys_base) != NOT_FOUND_SYMBOL)
		phys_base = info->phys_base;
	else
		phys_base = 0;

	if (is_vmalloc_addr(vaddr)) {
		if ((paddr = vtop4_x86_64(vaddr)) == NOT_PADDR) {
			ERRMSG("Can't convert a virtual address(%lx) to " \
			    "physical address.\n", vaddr);
			return NOT_PADDR;
		}
	}
	else if (vaddr >= __START_KERNEL_map)
		paddr = vaddr - __START_KERNEL_map + phys_base;
	else
		paddr = vaddr - PAGE_OFFSET;

	return paddr;
}

/*
 * for Xen extraction
 */
unsigned long long
kvtop_xen_x86_64(unsigned long kvaddr)
{
	unsigned long long dirp, entry;

	if (!is_xen_vaddr(kvaddr))
		return NOT_PADDR;

	if (is_xen_text(kvaddr))
		return (unsigned long)kvaddr - XEN_VIRT_START + info->xen_phys_start;

	if (is_direct(kvaddr))
		return (unsigned long)kvaddr - DIRECTMAP_VIRT_START;

	if ((dirp = kvtop_xen_x86_64(SYMBOL(pgd_l4))) == NOT_PADDR)
		return NOT_PADDR;
	dirp += pml4_index(kvaddr) * sizeof(unsigned long long);
	if (!readmem(PADDR, dirp, &entry, sizeof(entry)))
		return NOT_PADDR;

	if (!(entry & _PAGE_PRESENT))
		return NOT_PADDR;

	dirp = entry & ENTRY_MASK;
	dirp += pgd_index(kvaddr) * sizeof(unsigned long long);
	if (!readmem(PADDR, dirp, &entry, sizeof(entry)))
		return NOT_PADDR;

	if (!(entry & _PAGE_PRESENT))
		return NOT_PADDR;

	dirp = entry & ENTRY_MASK;
	dirp += pmd_index(kvaddr) * sizeof(unsigned long long);
	if (!readmem(PADDR, dirp, &entry, sizeof(entry)))
		return NOT_PADDR;

	if (!(entry & _PAGE_PRESENT))
		return NOT_PADDR;

	if (entry & _PAGE_PSE) {
		entry = (entry & ENTRY_MASK) + (kvaddr & ((1UL << PMD_SHIFT) - 1));
		return entry;
	}
	dirp = entry & ENTRY_MASK;
	dirp += pte_index(kvaddr) * sizeof(unsigned long long);
	if (!readmem(PADDR, dirp, &entry, sizeof(entry)))
		return NOT_PADDR;

	if (!(entry & _PAGE_PRESENT)) {
		return NOT_PADDR;
	}

	entry = (entry & ENTRY_MASK) + (kvaddr & ((1UL << PTE_SHIFT) - 1));

	return entry;
}

int get_xen_info_x86_64(void)
{
	unsigned long frame_table_vaddr;
	unsigned long xen_end;
	int i;

	if (SYMBOL(pgd_l4) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get pml4.\n");
		return FALSE;
	}

	if (SYMBOL(frame_table) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of frame_table.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, SYMBOL(frame_table), &frame_table_vaddr,
	    sizeof(frame_table_vaddr))) {
		ERRMSG("Can't get the value of frame_table.\n");
		return FALSE;
	}
	info->frame_table_vaddr = frame_table_vaddr;

	if (SYMBOL(xenheap_phys_end) == NOT_FOUND_SYMBOL) {
		ERRMSG("Can't get the symbol of xenheap_phys_end.\n");
		return FALSE;
	}
	if (!readmem(VADDR_XEN, SYMBOL(xenheap_phys_end), &xen_end,
	    sizeof(xen_end))) {
		ERRMSG("Can't get the value of xenheap_phys_end.\n");
		return FALSE;
	}
	info->xen_heap_end = (xen_end >> PAGESHIFT());
	info->xen_heap_start = 0;

	/*
	 * pickled_id == domain addr for x86_64
	 */
	for (i = 0; i < info->num_domain; i++) {
		info->domain_list[i].pickled_id =
			info->domain_list[i].domain_addr;
	}

	return TRUE;
}

#endif /* x86_64 */

