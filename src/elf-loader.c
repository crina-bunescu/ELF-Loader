// SPDX-License-Identifier: BSD-3-Clause

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <elf.h>
#include <string.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/auxv.h>

void *map_elf(const char *filename)
{
	// This part helps you store the content of the ELF file inside the buffer.
	struct stat st;
	void *file;
	int fd;

	fd = open(filename, O_RDONLY);
	if (fd < 0) {
		perror("open");
		exit(1);
	}

	fstat(fd, &st);

	file = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (file == MAP_FAILED) {
		perror("mmap");
		close(fd);
		exit(1);
	}

	return file;
}

// TO DO - task 4 //
void push_stack(void **stack, uint64_t val)
{
	*stack = (char *)*stack - sizeof(val);
	*(uint64_t *)*stack = val;
}

void load_and_run(const char *filename, int argc, char **argv, char **envp)
{
	// Contents of the ELF file are in the buffer: elf_contents[x] is the x-th byte of the ELF file.
	void *elf_contents = map_elf(filename);

	/**
	 * TODO: ELF Header Validation
	 * Validate ELF magic bytes - "Not a valid ELF file" + exit code 3 if invalid.
	 * Validate ELF class is 64-bit (ELFCLASS64) - "Not a 64-bit ELF" + exit code 4 if invalid.
	 */

	Elf64_Ehdr *ehdr = (Elf64_Ehdr *) elf_contents;

	if (ehdr->e_ident[0] != 0x7F || ehdr->e_ident[1] != 'E' || ehdr->e_ident[2] != 'L' || ehdr->e_ident[3] != 'F') {
		fprintf(stderr, "Not a valid ELF file\n");
		exit(3);
	}

	if (ehdr->e_ident[4] != ELFCLASS64) {
		fprintf(stderr, "Not a 64-bit ELF\n");
		exit(4); }

	/**
	 * TODO: Load PT_LOAD segments
	 * For minimal syscall-only binaries.
	 * For each PT_LOAD segment:
	 * - Map the segments in memory. Permissions can be RWX for now.
	 */

	uint64_t load_base = 0;

	Elf64_Phdr *phdr = (Elf64_Phdr *)((char *)elf_contents + ehdr->e_phoff);
	long page_size = sysconf(_SC_PAGESIZE);

	int is_pie = 0;

	if (ehdr->e_type == ET_DYN)
		is_pie = 1;

	int first_load = 1;

	for (int i = 0; i < ehdr->e_phnum; i++) {
		if (phdr[i].p_type == PT_LOAD) {

			Elf64_Addr vaddr = phdr[i].p_vaddr;
			Elf64_Off file_offset = phdr[i].p_offset;
			Elf64_Xword memsz = phdr[i].p_memsz;
			Elf64_Xword filesz = phdr[i].p_filesz;

			Elf64_Addr page_start = vaddr & ~(page_size - 1); // offsetul in cadrul primei pagini
			Elf64_Off page_offset = vaddr - page_start;
			Elf64_Xword map_size = (page_offset + memsz + page_size - 1) & ~(page_size - 1); // lungimea totala de mapat (rotunjita in sus)

			Elf64_Addr map_addr = load_base + page_start;

			int map_flags = MAP_PRIVATE | MAP_ANONYMOUS;

			if (is_pie) {
				if (first_load) { // vf daca e primul segment
					map_addr = 0;
					first_load = 0;
				} else
					map_flags |= MAP_FIXED;
			} else
				map_flags |= MAP_FIXED;

			void *addr = mmap((void *)map_addr, (size_t)map_size, PROT_READ | PROT_WRITE | PROT_EXEC, map_flags, -1, 0);

			if (addr == MAP_FAILED) {
				perror("mmap");
				exit(5);
			}

			if (is_pie && load_base == 0)
				load_base = (uint64_t)addr - page_start;

			Elf64_Addr new_vaddr = load_base + vaddr;

			if (filesz > 0)
				memcpy((void *)new_vaddr, (char *)elf_contents + file_offset, (size_t)filesz);

			if (memsz > filesz)
				memset((char *)new_vaddr + filesz, 0, (size_t)memsz - filesz);

			/**
			 * TODO: Load Memory Regions with Correct Permissions
			 * For each PT_LOAD segment:
			 * - Set memory permissions according to program header p_flags (PF_R, PF_W, PF_X).
			 * - Use mprotect() or map with the correct permissions directly using mmap().
			 */

			int prot = 0;

			if (phdr[i].p_flags & PF_R)
				prot |= PROT_READ;
			if (phdr[i].p_flags & PF_W)
				prot |= PROT_WRITE;
			if (phdr[i].p_flags & PF_X)
				prot |= PROT_EXEC;

			mprotect((void *)addr, (size_t)map_size, prot);
		}
	}

	/**
	 * TODO: Support Static Non-PIE Binaries with libc
	 * Must set up a valid process stack, including:
	 *	- argc, argv, envp
	 *	- auxv vector (with entries like AT_PHDR, AT_PHENT, AT_PHNUM, etc.)
	 * Note: Beware of the AT_RANDOM, AT_PHDR entries, the application will crash if you do not set them up properly.
	 */
	void *sp = NULL;

	struct rlimit rlim;
	size_t stack_size;

	if (getrlimit(RLIMIT_STACK, &rlim) != 0) {
		perror("getrlimit");
		stack_size = 8 * 1024 * 1024; // 8MB - valoare implicit mare
	} else {
		if (rlim.rlim_cur == RLIM_INFINITY)
			stack_size = 16 * 1024 * 1024; // limita nelimitata => 16MB
		else
			stack_size = rlim.rlim_cur;
	}

	void *stack_base = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

	if (stack_base == MAP_FAILED) {
		perror("mmap stack");
		exit(7);
	}

	sp = (char *)stack_base + stack_size;
	sp = (void *)((uintptr_t)sp & ~0xF);

	// argv
	uint64_t *stack_argv[argc + 1];	     // pt null

	for (int i = argc - 1; i >= 0; i--) {
		size_t len = strlen(argv[i]) + 1;

		sp = (char *)sp - len;
		memcpy(sp, argv[i], len);
		stack_argv[i] = (uint64_t *)sp;
	}

	stack_argv[argc] = NULL;

	int envc = 0;

	while (envp[envc] != NULL)
		envc++;

	// envp
	uint64_t *stack_envp[envc + 1];

	for (int i = envc - 1; i >= 0; i--) {
		size_t len = strlen(envp[i]) + 1;

		sp = (char *)sp - len;
		memcpy(sp, envp[i], len);
		stack_envp[i] = (uint64_t *)sp;
	}

	stack_envp[envc] = NULL;

	// auxv
	char random[16];

	for (int i = 0; i < 16; i++)
		random[i] = (char)i;

	sp = (char *)sp - 16;
	memcpy(sp, random, 16);
	uint64_t random_addr = (uint64_t)sp;

	push_stack(&sp, 0);
	push_stack(&sp, AT_NULL);

	push_stack(&sp, random_addr);
	push_stack(&sp, AT_RANDOM);

	uint64_t phdr_addr = load_base + ehdr->e_phoff + phdr[0].p_vaddr;

	push_stack(&sp, phdr_addr);
	push_stack(&sp, AT_PHDR);

	push_stack(&sp, load_base + (uint64_t)ehdr->e_entry);
	push_stack(&sp, AT_ENTRY);

	push_stack(&sp, (uint64_t)ehdr->e_phnum);
	push_stack(&sp, AT_PHNUM);

	push_stack(&sp, (uint64_t)ehdr->e_phentsize);
	push_stack(&sp, AT_PHENT);

	push_stack(&sp, (uint64_t)page_size);
	push_stack(&sp, AT_PAGESZ);

	for (int i = envc; i >= 0; i--)
		push_stack(&sp, (uint64_t)stack_envp[i]);

	for (int i = argc; i >= 0; i--)
		push_stack(&sp, (uint64_t)stack_argv[i]);

	push_stack(&sp, argc);

	/**
	 * TODO: Support Static PIE Executables
	 * Map PT_LOAD segments at a random load base.
	 * Adjust virtual addresses of segments and entry point by load_base.
	 * Stack setup (argc, argv, envp, auxv) same as above.
	 */

	// TODO: Set the entry point and the stack pointer
	void (*entry)() = (void (*)())(load_base + ehdr->e_entry);

	// Transfer control
	__asm__ __volatile__(
			"mov %0, %%rsp\n"
			"xor %%rbp, %%rbp\n"
			"jmp *%1\n"
			:
			: "r"(sp), "r"(entry)
			: "memory"
			);
}

int main(int argc, char **argv, char **envp)
{
	if (argc < 2) {
		fprintf(stderr, "Usage: %s <static-elf-binary>\n", argv[0]);
		exit(1);
	}

	load_and_run(argv[1], argc - 1, &argv[1], envp);
	return 0;
}
