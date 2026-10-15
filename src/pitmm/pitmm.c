/*
 * PITMM.DLL - emulador de CD de audio para Pitfall: The Mayan Adventure
 * Copyright (c) 2026 Alessandro Ramos - MIT License
 *
 * O PITFALL.EXE importa WINMM.dll; no executavel corrigido o nome do import
 * foi trocado para PITMM.dll. Todas as funcoes sao repassadas ao winmm real,
 * exceto:
 *   - mciSendCommandA: o dispositivo "cdaudio" vira um CD virtual que toca
 *     MP3 (via MCI mpegvideo), com o mapa de faixas em PITMUSIC.INI;
 *   - aux*: um dispositivo auxiliar "CD Audio" virtual, para o controle de
 *     volume da musica do jogo funcionar.
 * O CD virtual responde a verificacao de disco do jogo (duracao das faixas
 * 7 e 8) e envia MM_MCINOTIFY ao fim de cada faixa, como um CD real.
 * Com [options] realcd=1 tudo e repassado ao winmm (usa o CD fisico).
 */
#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <string.h>

#define FAKE_CD_ID   0xC0DE
#define ALIAS        "pitmus"
#define TIMER_SILENT 1
#define NTRACKS      24

/* Duracao das faixas do CD original (redump, em setores de 1/75 s) */
static const DWORD g_track_sectors[NTRACKS + 1] = {
    0, 20488, 2192, 3365, 10997, 16730, 7071, 18558, 20412, 11439, 7399,
    17529, 13572, 14573, 17432, 18799, 19373, 13618, 7786, 10057, 8666,
    8541, 1970, 10220, 3284
};

typedef MCIERROR (WINAPI *PFN_mciSendCommandA)(MCIDEVICEID, UINT, DWORD_PTR, DWORD_PTR);
typedef MCIERROR (WINAPI *PFN_mciSendStringA)(LPCSTR, LPSTR, UINT, HWND);

static HMODULE              g_winmm;
static PFN_mciSendCommandA  real_mciSendCommandA;
static PFN_mciSendStringA   real_mciSendStringA;
static HINSTANCE            g_self;

static int   g_open;
static DWORD g_timefmt = MCI_FORMAT_MSF;
static HWND  g_notify_hwnd;     /* janela do jogo que pediu notify */
static int   g_pending_notify;  /* ha um play com MCI_NOTIFY pendente */
static int   g_playing_track;
static int   g_mp3_open;
static HWND  g_msgwnd;
static DWORD g_volume = 0xFFFFFFFF; /* aux volume (L|R<<16), 0..0xFFFF */
static char  g_inipath[MAX_PATH];
static char  g_exedir[MAX_PATH];
static int   g_log;
static int   g_realcd;   /* [options] realcd=1: usa o drive de CD fisico */

typedef UINT (WINAPI *PFN_auxGetNumDevs)(void);
typedef MMRESULT (WINAPI *PFN_auxGetDevCapsA)(UINT_PTR, LPAUXCAPSA, UINT);
typedef MMRESULT (WINAPI *PFN_auxGetVolume)(UINT, LPDWORD);
typedef MMRESULT (WINAPI *PFN_auxSetVolume)(UINT, DWORD);
static PFN_auxGetNumDevs  real_auxGetNumDevs;
static PFN_auxGetDevCapsA real_auxGetDevCapsA;
static PFN_auxGetVolume   real_auxGetVolume;
static PFN_auxSetVolume   real_auxSetVolume;

static void plog(const char *fmt, ...)
{
    char path[MAX_PATH]; FILE *f; va_list ap;
    if (!g_log) return;
    lstrcpyA(path, g_exedir); lstrcatA(path, "PITMM.LOG");
    f = fopen(path, "a");
    if (!f) return;
    va_start(ap, fmt); vfprintf(f, fmt, ap); va_end(ap);
    fputc('\n', f); fclose(f);
}

static void load_real(void)
{
    char sys[MAX_PATH];
    if (g_winmm) return;
    GetSystemDirectoryA(sys, MAX_PATH);
    lstrcatA(sys, "\\winmm.dll");
    g_winmm = LoadLibraryA(sys);
    real_mciSendCommandA = (PFN_mciSendCommandA)GetProcAddress(g_winmm, "mciSendCommandA");
    real_mciSendStringA  = (PFN_mciSendStringA)GetProcAddress(g_winmm, "mciSendStringA");
    real_auxGetNumDevs   = (PFN_auxGetNumDevs)GetProcAddress(g_winmm, "auxGetNumDevs");
    real_auxGetDevCapsA  = (PFN_auxGetDevCapsA)GetProcAddress(g_winmm, "auxGetDevCapsA");
    real_auxGetVolume    = (PFN_auxGetVolume)GetProcAddress(g_winmm, "auxGetVolume");
    real_auxSetVolume    = (PFN_auxSetVolume)GetProcAddress(g_winmm, "auxSetVolume");
}

static void mcistr(const char *cmd, HWND cb)
{
    MCIERROR e = real_mciSendStringA(cmd, NULL, 0, cb);
    plog("  mciSendString(%s) = %lu", cmd, (unsigned long)e);
}

static void apply_volume(void)
{
    char cmd[96];
    DWORD l = g_volume & 0xFFFF, r = g_volume >> 16;
    DWORD v = ((l > r ? l : r) * 1000) / 0xFFFF;
    if (!g_mp3_open) return;
    wsprintfA(cmd, "setaudio " ALIAS " volume to %lu", (unsigned long)v);
    mcistr(cmd, NULL);
}

static void close_mp3(void)
{
    if (g_msgwnd) KillTimer(g_msgwnd, TIMER_SILENT);
    if (g_mp3_open) { mcistr("close " ALIAS, NULL); g_mp3_open = 0; }
    g_playing_track = 0;
}

/* Fim de faixa: avisa o jogo como um CD de verdade */
static void track_finished(void)
{
    plog("track %d finished", g_playing_track);
    close_mp3();
    if (g_pending_notify && g_notify_hwnd) {
        g_pending_notify = 0;
        PostMessageA(g_notify_hwnd, MM_MCINOTIFY, MCI_NOTIFY_SUCCESSFUL, FAKE_CD_ID);
    }
}

static void cancel_notify(WPARAM why)
{
    if (g_pending_notify && g_notify_hwnd)
        PostMessageA(g_notify_hwnd, MM_MCINOTIFY, why, FAKE_CD_ID);
    g_pending_notify = 0;
}

static LRESULT CALLBACK msgproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == MM_MCINOTIFY) {
        if (w == MCI_NOTIFY_SUCCESSFUL && g_mp3_open) track_finished();
        return 0;
    }
    if (m == WM_TIMER && w == TIMER_SILENT) {
        KillTimer(h, TIMER_SILENT);
        track_finished();
        return 0;
    }
    return DefWindowProcA(h, m, w, l);
}

static void ensure_window(void)
{
    WNDCLASSA wc;
    if (g_msgwnd) return;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = msgproc;
    wc.hInstance = g_self;
    wc.lpszClassName = "PitMMNotify";
    RegisterClassA(&wc);
    g_msgwnd = CreateWindowExA(0, "PitMMNotify", "", 0, 0, 0, 0, 0,
                               HWND_MESSAGE, NULL, g_self, NULL);
}

static void play_track(int track)
{
    char key[16], file[MAX_PATH], full[MAX_PATH], cmd[MAX_PATH + 64], *fp;
    ensure_window();
    close_mp3();
    g_playing_track = track;
    wsprintfA(key, "%d", track);
    GetPrivateProfileStringA("tracks", key, "", file, MAX_PATH, g_inipath);
    plog("play track %d -> '%s'", track, file);
    if (file[0]) {
        /* caminho relativo a pasta do PITFALL.EXE */
        char rel[MAX_PATH];
        if (file[1] == ':' || file[0] == '\\') lstrcpyA(rel, file);
        else { lstrcpyA(rel, g_exedir); lstrcatA(rel, file); }
        GetFullPathNameA(rel, MAX_PATH, full, &fp);
        wsprintfA(cmd, "open \"%s\" type mpegvideo alias " ALIAS, full);
        if (real_mciSendStringA(cmd, NULL, 0, NULL) == 0) {
            g_mp3_open = 1;
            plog("  opened %s", full);
            apply_volume();
            mcistr("play " ALIAS " from 0 notify", g_msgwnd);
            return;
        }
        plog("  open failed: %s", full);
    }
    /* Faixa sem MP3 (ambientes): "toca" silencio com a duracao original */
    if (track >= 1 && track <= NTRACKS)
        SetTimer(g_msgwnd, TIMER_SILENT, g_track_sectors[track] * 1000 / 75, NULL);
}

static DWORD pack_msf(DWORD sectors)
{
    DWORD m = sectors / (75 * 60), s = (sectors / 75) % 60, f = sectors % 75;
    return m | (s << 8) | (f << 16);
}

static MCIERROR fake_cd(UINT msg, DWORD_PTR flags, DWORD_PTR parm)
{
    plog("cd msg=%#x flags=%#lx", msg, (unsigned long)flags);
    switch (msg) {
    case MCI_SET: {
        MCI_SET_PARMS *p = (MCI_SET_PARMS *)parm;
        if (p && (flags & MCI_SET_TIME_FORMAT)) g_timefmt = p->dwTimeFormat;
        return 0;
    }
    case MCI_STATUS: {
        MCI_STATUS_PARMS *p = (MCI_STATUS_PARMS *)parm;
        if (!p || !(flags & MCI_STATUS_ITEM)) return 0;
        switch (p->dwItem) {
        case MCI_STATUS_LENGTH:
            if ((flags & MCI_TRACK) && p->dwTrack >= 1 && p->dwTrack <= NTRACKS) {
                /* o jogo confere faixa 7 = 4:07.33 e faixa 8 = 4:32.12 */
                if (p->dwTrack == 7) p->dwReturn = 4 | (7 << 8) | (33 << 16);
                else if (p->dwTrack == 8) p->dwReturn = 4 | (32 << 8) | (12 << 16);
                else p->dwReturn = pack_msf(g_track_sectors[p->dwTrack]);
            } else p->dwReturn = pack_msf(270000);
            return 0;
        case MCI_STATUS_NUMBER_OF_TRACKS: p->dwReturn = NTRACKS; return 0;
        case MCI_STATUS_MODE:
            p->dwReturn = g_playing_track ? MCI_MODE_PLAY : MCI_MODE_STOP; return 0;
        case MCI_STATUS_MEDIA_PRESENT: p->dwReturn = TRUE; return 0;
        case MCI_STATUS_READY: p->dwReturn = TRUE; return 0;
        case MCI_STATUS_CURRENT_TRACK: p->dwReturn = g_playing_track; return 0;
        case MCI_CDA_STATUS_TYPE_TRACK:
            p->dwReturn = (p->dwTrack == 1) ? MCI_CDA_TRACK_OTHER : MCI_CDA_TRACK_AUDIO;
            return 0;
        default: p->dwReturn = 0; return 0;
        }
    }
    case MCI_PLAY: {
        MCI_PLAY_PARMS *p = (MCI_PLAY_PARMS *)parm;
        int track = 2;
        if (p && (flags & MCI_FROM)) {
            if (g_timefmt == MCI_FORMAT_TMSF) track = (int)(p->dwFrom & 0xFF);
            else track = 2;
        }
        cancel_notify(MCI_NOTIFY_SUPERSEDED);
        if (p && (flags & MCI_NOTIFY)) {
            g_notify_hwnd = (HWND)p->dwCallback;
            g_pending_notify = 1;
        }
        play_track(track);
        return 0;
    }
    case MCI_STOP:
    case MCI_PAUSE:
        cancel_notify(MCI_NOTIFY_ABORTED);
        close_mp3();
        return 0;
    case MCI_CLOSE:
        cancel_notify(MCI_NOTIFY_ABORTED);
        close_mp3();
        g_open = 0;
        return 0;
    default:
        return 0;
    }
}

MCIERROR WINAPI pit_mciSendCommandA(MCIDEVICEID id, UINT msg, DWORD_PTR flags, DWORD_PTR parm)
{
    load_real();
    if (g_realcd) return real_mciSendCommandA(id, msg, flags, parm);
    if (msg == MCI_OPEN && parm && (flags & MCI_OPEN_TYPE) && !(flags & MCI_OPEN_TYPE_ID)) {
        MCI_OPEN_PARMSA *p = (MCI_OPEN_PARMSA *)parm;
        if (p->lpstrDeviceType && !lstrcmpiA(p->lpstrDeviceType, "cdaudio")) {
            plog("open cdaudio -> virtual CD");
            g_open = 1;
            g_timefmt = MCI_FORMAT_MSF;
            p->wDeviceID = FAKE_CD_ID;
            return 0;
        }
    }
    if (id == FAKE_CD_ID) return fake_cd(msg, flags, parm);
    return real_mciSendCommandA(id, msg, flags, parm);
}

/* ---- dispositivo aux "CD Audio" virtual (volume da musica) ---- */
UINT WINAPI pit_auxGetNumDevs(void)
{
    if (g_realcd) return real_auxGetNumDevs();
    return 1;
}

MMRESULT WINAPI pit_auxGetDevCapsA(UINT_PTR id, LPAUXCAPSA caps, UINT cb)
{
    AUXCAPSA c;
    if (g_realcd) return real_auxGetDevCapsA(id, caps, cb);
    if (id != 0) return MMSYSERR_BADDEVICEID;
    memset(&c, 0, sizeof(c));
    c.wMid = 1; c.wPid = 1; c.vDriverVersion = 0x100;
    lstrcpyA(c.szPname, "Pitfall Virtual CD Audio");
    c.wTechnology = AUXCAPS_CDAUDIO;
    c.dwSupport = AUXCAPS_VOLUME | AUXCAPS_LRVOLUME;
    memcpy(caps, &c, cb < sizeof(c) ? cb : sizeof(c));
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI pit_auxGetVolume(UINT id, LPDWORD vol)
{
    if (g_realcd) return real_auxGetVolume(id, vol);
    if (id != 0) return MMSYSERR_BADDEVICEID;
    if (vol) *vol = g_volume;
    return MMSYSERR_NOERROR;
}

MMRESULT WINAPI pit_auxSetVolume(UINT id, DWORD vol)
{
    if (g_realcd) return real_auxSetVolume(id, vol);
    if (id != 0) return MMSYSERR_BADDEVICEID;
    g_volume = vol;
    apply_volume();
    return MMSYSERR_NOERROR;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID r)
{
    (void)r;
    if (reason == DLL_PROCESS_ATTACH) {
        char *p;
        g_self = h;
        GetModuleFileNameA(NULL, g_exedir, MAX_PATH);
        p = strrchr(g_exedir, '\\');
        if (p) p[1] = 0;
        lstrcpyA(g_inipath, g_exedir);
        lstrcatA(g_inipath, "PITMUSIC.INI");
        g_log = GetPrivateProfileIntA("options", "log", 0, g_inipath);
        g_realcd = GetPrivateProfileIntA("options", "realcd", 0, g_inipath);
        load_real();
    } else if (reason == DLL_PROCESS_DETACH) {
        if (g_mp3_open && real_mciSendStringA) real_mciSendStringA("close " ALIAS, NULL, 0, NULL);
    }
    return TRUE;
}
