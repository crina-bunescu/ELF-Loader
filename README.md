# ELF Loader

A minimal ELF64 loader implemented from scratch in C, capable of loading and executing statically linked Linux binaries — including syscall-only programs, non-PIE C binaries compiled with libc, and PIE executables — without relying on the OS dynamic linker.

> **Assignment context:** Operating Systems course, UNSTPB. Full requirements are in [`requirements.md`](requirements.md).

---

## What It Does

The loader manually replicates what the Linux kernel does when executing an ELF binary:

1. **Validates** the ELF magic and class (exits with specific error codes for invalid files).
2. **Maps `PT_LOAD` segments** into memory using `mmap()`, respecting each segment's `p_flags` permissions (`PF_R`, `PF_W`, `PF_X`) via `mprotect()`.
3. **Constructs a valid process stack** for binaries that use libc, including:
   - `argc` and `argv`
   - `envp` (copied from the loader's own environment)
   - Auxiliary vector (`auxv`) with entries like `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ`, `AT_RANDOM`, etc.
4. **Handles PIE executables** (`ET_DYN`) by mapping segments at a random base address and adjusting entry point and `auxv` entries accordingly.
5. **Transfers control** to the loaded binary's entry point via inline assembly (`jmp`), zeroing `rbp` as required by the ABI.

**Key detail:** `p_filesz` vs `p_memsz` — the BSS region (`p_memsz > p_filesz`) is zero-initialized separately after the file-backed mapping, as the kernel does.

---

## Implementation

All implementation is in a single file:

| File | Description |
|---|---|
| `src/elf-loader.c` | Full loader: ELF validation, segment mapping, stack construction, control transfer |

---

## Build & Run

```bash
cd src/
make
```

```bash
# Load and run a static binary
./elf-loader <path-to-elf> [arg1 arg2 ...]

# Examples
./elf-loader ../tests/snippets/no_pie
./elf-loader ../tests/snippets/pie hello world
./elf-loader ../tests/snippets/nolibc
```

---

## Testing

```bash
cd tests/
make check
```

Expected output when all tests pass:

```
error-bad-magic ......... passed
error-not-64 ............ passed
no_pie_hello ............ passed
no_pie_argc ............. passed
no_pie_argv ............. passed
no_pie_envp ............. passed
no_pie_auxv ............. passed
nolibc .................. passed
nolibc_no_rwx_rodata .... passed
nolibc_no_rwx_text ...... passed
pie ..................... passed
```

Or run via the local checker (replicates the GitLab CI environment):

```bash
./local.sh checker
```

### Debugging Tips

```bash
# Inspect segment layout of a binary
readelf -l -h tests/snippets/no_pie

# Run under GDB and load symbols for the target ELF
gdb ./src/elf-loader
(gdb) run ./tests/snippets/no_pie
(gdb) add-symbol-file tests/snippets/no_pie 0x<text_section_address>

# Check live memory mapping of the loader process
pmap $(pidof elf-loader)
```

> The easiest way to reproduce the grading environment is via the provided Docker setup — see [`README.checker.md`](README.checker.md).
