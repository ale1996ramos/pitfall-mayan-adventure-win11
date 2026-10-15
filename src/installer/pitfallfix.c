/*
 * PitfallFix.exe - installer for the Pitfall: The Mayan Adventure Windows 10/11 fix
 * Copyright (c) 2026 Alessandro Ramos - MIT License
 *
 * Run it inside the game folder (or pass the folder as argument).
 *   PitfallFix.exe              install / update
 *   PitfallFix.exe --uninstall  restore the original files
 *
 * It verifies that PITFALL.EXE is the known 1995 Windows 95 release, backs up
 * the originals (*.orig), applies the byte patches, writes WAIL32.DLL and
 * PITMM.DLL (embedded in this installer) and creates PITMUSIC.INI if missing.
 */
#include <windows.h>
#include <stdio.h>
#include <string.h>

#include "embedded.h"   /* generated: wail32_dll[], pitmm_dll[], pitmusic_ini[] */

#ifndef PITFALLFIX_VERSION
#define PITFALLFIX_VERSION "dev"
#endif

#define EXE_SIZE 710144u
#define ORIGINAL_WAIL_SIZE 116736u

typedef struct PATCH {
    DWORD offset;
    int   len;
    const char *orig;
    const char *patched;
    const char *what;
} PATCH;

/* File offsets in PITFALL.EXE (710,144 bytes). See docs/TECHNICAL.md. */
static const PATCH g_patches[] = {
    { 0x3b6d2, 2, "\x76\x10", "\x90\x90", "Win95 flag: do not clear it on Windows NT (WAIL32/joystick)" },
    { 0x3b6e2, 1, "\x73",     "\xeb",     "Win95 flag: ignore minor version check" },
    { 0x3c01a, 1, "\x74",     "\xeb",     "Sound effects: use the AIL path instead of single-voice sndPlaySound" },
    { 0x3f527, 1, "\x02",     "\x01",     "Settings: HKEY_LOCAL_MACHINE -> HKEY_CURRENT_USER (read)" },
    { 0x3f860, 1, "\x74",     "\xeb",     "Settings: do not force Option_SoundEnabled=0 on NT" },
    { 0x3f8b1, 1, "\x02",     "\x01",     "Settings: HKEY_LOCAL_MACHINE -> HKEY_CURRENT_USER (write)" },
    { 0x40e95, 1, "\x74",     "\xeb",     "Properties dialog: keep the sound control enabled on NT" },
    { 0x6c7f4, 1, "\x57",     "\x50",     "Import WINMM.dll -> PITMM.dll (W->P)" },
    { 0x6c7f6, 1, "\x4e",     "\x54",     "Import WINMM.dll -> PITMM.dll (N->T)" },
};
#define NPATCHES (sizeof(g_patches) / sizeof(g_patches[0]))

static char g_dir[MAX_PATH];

static void path_of(char *out, const char *name)
{
    lstrcpyA(out, g_dir);
    lstrcatA(out, name);
}

static int exists(const char *name)
{
    char p[MAX_PATH];
    path_of(p, name);
    return GetFileAttributesA(p) != INVALID_FILE_ATTRIBUTES;
}

static BYTE *read_file(const char *name, DWORD *size)
{
    char p[MAX_PATH]; HANDLE h; DWORD rd; BYTE *buf;
    path_of(p, name);
    h = CreateFileA(p, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) return NULL;
    *size = GetFileSize(h, NULL);
    buf = (BYTE *)HeapAlloc(GetProcessHeap(), 0, *size ? *size : 1);
    if (!ReadFile(h, buf, *size, &rd, NULL) || rd != *size) { CloseHandle(h); return NULL; }
    CloseHandle(h);
    return buf;
}

static int write_file(const char *name, const void *data, DWORD size)
{
    char p[MAX_PATH]; HANDLE h; DWORD wr; BOOL ok;
    path_of(p, name);
    SetFileAttributesA(p, FILE_ATTRIBUTE_NORMAL);
    h = CreateFileA(p, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        printf("  ERROR: cannot write %s (error %lu)\n", name, GetLastError());
        return 0;
    }
    ok = WriteFile(h, data, size, &wr, NULL) && wr == size;
    CloseHandle(h);
    if (!ok) printf("  ERROR: write failed for %s\n", name);
    return ok;
}

static int copy_if_missing(const char *from, const char *to)
{
    char a[MAX_PATH], b[MAX_PATH];
    if (exists(to)) return 1;
    path_of(a, from); path_of(b, to);
    if (!CopyFileA(a, b, TRUE)) {
        printf("  ERROR: cannot back up %s (error %lu)\n", from, GetLastError());
        return 0;
    }
    printf("  backup: %s -> %s\n", from, to);
    return 1;
}

/* 0 = original, 1 = already patched, -1 = unknown version */
static int exe_state(const BYTE *exe, DWORD size)
{
    unsigned i; int orig = 1, patched = 1;
    if (size != EXE_SIZE) return -1;
    for (i = 0; i < NPATCHES; i++) {
        const PATCH *p = &g_patches[i];
        if (memcmp(exe + p->offset, p->orig, p->len)) orig = 0;
        if (memcmp(exe + p->offset, p->patched, p->len)) patched = 0;
    }
    if (orig) return 0;
    if (patched) return 1;
    return -1;
}

static int install(void)
{
    DWORD size, wsize; BYTE *exe, *wail; unsigned i; int st;

    exe = read_file("PITFALL.EXE", &size);
    if (!exe) {
        printf("PITFALL.EXE not found in:\n  %s\nRun PitfallFix.exe inside the game folder.\n"
               "PITFALL.EXE nao encontrado. Rode o PitfallFix.exe dentro da pasta do jogo.\n", g_dir);
        return 0;
    }
    st = exe_state(exe, size);
    if (st < 0) {
        printf("Unknown PITFALL.EXE version (size %lu). Nothing was changed.\n"
               "Versao desconhecida do PITFALL.EXE. Nada foi alterado.\n"
               "Please open an issue with your EXE size and SHA-256.\n", size);
        return 0;
    }

    printf("[1/4] Backups\n");
    if (st == 0 && !copy_if_missing("PITFALL.EXE", "PITFALL.EXE.orig")) return 0;
    wail = read_file("WAIL32.DLL", &wsize);
    if (wail && wsize == ORIGINAL_WAIL_SIZE && !copy_if_missing("WAIL32.DLL", "WAIL32.DLL.orig")) return 0;

    printf("[2/4] Patching PITFALL.EXE\n");
    if (st == 1) printf("  already patched\n");
    else {
        for (i = 0; i < NPATCHES; i++) {
            memcpy(exe + g_patches[i].offset, g_patches[i].patched, g_patches[i].len);
            printf("  %#07lx  %s\n", g_patches[i].offset, g_patches[i].what);
        }
        if (!write_file("PITFALL.EXE", exe, size)) return 0;
    }

    printf("[3/4] Writing WAIL32.DLL (DirectSound) and PITMM.DLL (virtual CD)\n");
    if (!write_file("WAIL32.DLL", wail32_dll, sizeof(wail32_dll))) return 0;
    if (!write_file("PITMM.DLL", pitmm_dll, sizeof(pitmm_dll))) return 0;

    printf("[4/4] Music configuration\n");
    if (exists("PITMUSIC.INI")) printf("  PITMUSIC.INI already exists, kept as is\n");
    else if (!write_file("PITMUSIC.INI", pitmusic_ini, sizeof(pitmusic_ini) - 1)) return 0;
    else printf("  PITMUSIC.INI created (put CD tracks in the 'music' folder)\n");

    printf("\nDone! / Pronto! Run PITFALL.EXE normally (no admin, no compatibility mode).\n"
           "In the game Properties enable Sound Effects and CD Music.\n");
    return 1;
}

static int restore(const char *backup, const char *target)
{
    char a[MAX_PATH], b[MAX_PATH];
    if (!exists(backup)) return 1;
    path_of(a, backup); path_of(b, target);
    SetFileAttributesA(b, FILE_ATTRIBUTE_NORMAL);
    if (!CopyFileA(a, b, FALSE)) {
        printf("  ERROR: cannot restore %s (error %lu)\n", target, GetLastError());
        return 0;
    }
    DeleteFileA(a);
    printf("  restored %s\n", target);
    return 1;
}

static int uninstall(void)
{
    char p[MAX_PATH];
    printf("Restoring original files / Restaurando arquivos originais\n");
    if (!restore("PITFALL.EXE.orig", "PITFALL.EXE")) return 0;
    if (!restore("WAIL32.DLL.orig", "WAIL32.DLL")) return 0;
    path_of(p, "PITMM.DLL");
    if (DeleteFileA(p)) printf("  removed PITMM.DLL\n");
    printf("Done. PITMUSIC.INI and the music folder were kept.\n");
    return 1;
}

int main(int argc, char **argv)
{
    int i, un = 0, ok, quiet = 0;
    char *slash;

    printf("Pitfall: The Mayan Adventure - Windows 10/11 fix " PITFALLFIX_VERSION "\n\n");

    GetModuleFileNameA(NULL, g_dir, MAX_PATH);
    slash = strrchr(g_dir, '\\');
    if (slash) slash[1] = 0;

    for (i = 1; i < argc; i++) {
        if (!lstrcmpiA(argv[i], "--uninstall") || !lstrcmpiA(argv[i], "/u")) un = 1;
        else if (!lstrcmpiA(argv[i], "--quiet") || !lstrcmpiA(argv[i], "/q")) quiet = 1;
        else {
            lstrcpynA(g_dir, argv[i], MAX_PATH - 2);
            if (g_dir[lstrlenA(g_dir) - 1] != '\\') lstrcatA(g_dir, "\\");
        }
    }

    ok = un ? uninstall() : install();
    if (!quiet) {
        printf("\nPress Enter to close / Pressione Enter para fechar...");
        getchar();
    }
    return ok ? 0 : 1;
}
