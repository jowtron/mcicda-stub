/*
 * MCI CD Audio Driver with Direct Playback
 * Intercepts CD audio commands and plays WAV files directly via MCI waveaudio.
 * No external controller needed - audio plays through Wine's audio subsystem.
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>

/* Internal MCI driver message IDs (not in standard headers) */
#ifndef MCI_OPEN_DRIVER
#define MCI_OPEN_DRIVER 0x0801
#endif
#ifndef MCI_CLOSE_DRIVER
#define MCI_CLOSE_DRIVER 0x0802
#endif

/* Music directory - where track files are stored */
#define MUSIC_DIR "C:\\music\\"

/* Debug log file */
#define DEBUG_LOG "C:\\mcicda_debug.log"

static void DebugLog(const char* fmt, ...)
{
    FILE* f = fopen(DEBUG_LOG, "a");
    if (f) {
        va_list args;
        va_start(args, fmt);
        vfprintf(f, fmt, args);
        va_end(args);
        fprintf(f, "\n");
        fflush(f);
        fclose(f);
    }
}

/* Device state */
static BOOL g_bOpen = FALSE;
static DWORD g_dwCurrentTrack = 2;
static DWORD g_dwNumTracks = 18;
static DWORD g_dwTimeFormat = MCI_FORMAT_TMSF;
static BOOL g_bPlaying = FALSE;
static BOOL g_bPaused = FALSE;

/* MCI device ID for waveaudio playback */
static MCIDEVICEID g_waveDeviceId = 0;

/* Build path to track file */
static void GetTrackPath(DWORD track, char* path, size_t pathSize)
{
    _snprintf(path, pathSize, "%strack%02d.wav", MUSIC_DIR, track);
}

/* Check if a track file exists */
static BOOL TrackExists(DWORD track)
{
    char path[MAX_PATH];
    GetTrackPath(track, path, MAX_PATH);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* Stop current playback */
static void StopPlayback(void)
{
    if (g_waveDeviceId) {
        mciSendCommand(g_waveDeviceId, MCI_STOP, 0, 0);
        mciSendCommand(g_waveDeviceId, MCI_CLOSE, 0, 0);
        g_waveDeviceId = 0;
    }
    g_bPlaying = FALSE;
    g_bPaused = FALSE;
}

/* Play a track using MCI waveaudio */
static BOOL PlayTrack(DWORD track)
{
    char path[MAX_PATH];
    MCI_OPEN_PARMSA openParms;
    MCI_PLAY_PARMS playParms;
    MCIERROR err;
    char errBuf[256];

    DebugLog("PlayTrack called for track %d", track);

    /* Stop any current playback */
    StopPlayback();

    /* Build track path */
    GetTrackPath(track, path, MAX_PATH);
    DebugLog("Track path: %s", path);

    /* Check if file exists */
    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        DebugLog("ERROR: File not found: %s (error %d)", path, GetLastError());
        return FALSE;
    }
    DebugLog("File exists");

    /* Open the wave file */
    ZeroMemory(&openParms, sizeof(openParms));
    openParms.lpstrDeviceType = "waveaudio";
    openParms.lpstrElementName = path;

    err = mciSendCommandA(0, MCI_OPEN,
        MCI_OPEN_TYPE | MCI_OPEN_ELEMENT,
        (DWORD_PTR)&openParms);

    if (err != 0) {
        mciGetErrorStringA(err, errBuf, sizeof(errBuf));
        DebugLog("ERROR: MCI_OPEN failed: %s (code %d)", errBuf, err);
        return FALSE;
    }

    g_waveDeviceId = openParms.wDeviceID;
    DebugLog("MCI_OPEN success, deviceId=%d", g_waveDeviceId);

    /* Start playback */
    ZeroMemory(&playParms, sizeof(playParms));
    err = mciSendCommand(g_waveDeviceId, MCI_PLAY, 0, (DWORD_PTR)&playParms);

    if (err != 0) {
        mciGetErrorStringA(err, errBuf, sizeof(errBuf));
        DebugLog("ERROR: MCI_PLAY failed: %s (code %d)", errBuf, err);
        mciSendCommand(g_waveDeviceId, MCI_CLOSE, 0, 0);
        g_waveDeviceId = 0;
        return FALSE;
    }

    DebugLog("MCI_PLAY success - audio should be playing");

    g_dwCurrentTrack = track;
    g_bPlaying = TRUE;
    g_bPaused = FALSE;

    return TRUE;
}

/* Pause playback */
static void PausePlayback(void)
{
    if (g_waveDeviceId && g_bPlaying && !g_bPaused) {
        mciSendCommand(g_waveDeviceId, MCI_PAUSE, 0, 0);
        g_bPaused = TRUE;
    }
}

/* Resume playback */
static void ResumePlayback(void)
{
    if (g_waveDeviceId && g_bPaused) {
        mciSendCommand(g_waveDeviceId, MCI_RESUME, 0, 0);
        g_bPaused = FALSE;
    }
}

/* Count available tracks */
static DWORD CountTracks(void)
{
    DWORD count = 0;
    DWORD i;
    for (i = 2; i <= 99; i++) {
        if (TrackExists(i)) {
            count++;
        } else if (count > 0) {
            /* Stop at first gap after finding tracks */
            break;
        }
    }
    return count > 0 ? count + 1 : 18; /* +1 for data track, default to 18 */
}

/* MCI driver procedure */
LRESULT CALLBACK DriverProc(DWORD_PTR dwDriverId, HDRVR hDriver, UINT msg,
                            LPARAM lParam1, LPARAM lParam2)
{
    switch (msg) {
    case DRV_LOAD:
    case DRV_ENABLE:
        return 1;

    case DRV_OPEN:
    case DRV_CLOSE:
    case DRV_DISABLE:
    case DRV_FREE:
        return 1;

    case DRV_QUERYCONFIGURE:
        return 0;

    case DRV_INSTALL:
    case DRV_REMOVE:
        return DRV_OK;
    }

    /* MCI commands */
    if (msg == MCI_OPEN_DRIVER) {
        g_bOpen = TRUE;
        g_dwNumTracks = CountTracks();
        DebugLog("MCI_OPEN_DRIVER: device opened, %d tracks found", g_dwNumTracks);
        return 0;
    }

    if (msg == MCI_CLOSE_DRIVER) {
        StopPlayback();
        g_bOpen = FALSE;
        return 0;
    }

    if (!g_bOpen)
        return MCIERR_DEVICE_NOT_READY;

    switch (msg) {
    case MCI_OPEN:
        g_dwNumTracks = CountTracks();
        return 0;

    case MCI_CLOSE:
        StopPlayback();
        return 0;

    case MCI_PLAY:
        {
            DWORD dwFrom = g_dwCurrentTrack;

            DebugLog("MCI_PLAY received, flags=0x%08X", lParam1);

            if (lParam1 & MCI_FROM) {
                MCI_PLAY_PARMS* parms = (MCI_PLAY_PARMS*)lParam2;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwFrom = MCI_TMSF_TRACK(parms->dwFrom);
                else
                    dwFrom = parms->dwFrom;
                DebugLog("MCI_FROM specified: track %d", dwFrom);
            }

            PlayTrack(dwFrom);
        }
        return 0;

    case MCI_STOP:
        StopPlayback();
        return 0;

    case MCI_PAUSE:
        PausePlayback();
        return 0;

    case MCI_RESUME:
        ResumePlayback();
        return 0;

    case MCI_SEEK:
        {
            if (lParam1 & MCI_TO) {
                MCI_SEEK_PARMS* parms = (MCI_SEEK_PARMS*)lParam2;
                DWORD dwTrack;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwTrack = MCI_TMSF_TRACK(parms->dwTo);
                else
                    dwTrack = parms->dwTo;
                g_dwCurrentTrack = dwTrack;
            }
        }
        return 0;

    case MCI_STATUS:
        {
            MCI_STATUS_PARMS* parms = (MCI_STATUS_PARMS*)lParam2;
            if (!parms) return MCIERR_NULL_PARAMETER_BLOCK;

            if (lParam1 & MCI_STATUS_ITEM) {
                switch (parms->dwItem) {
                case MCI_STATUS_NUMBER_OF_TRACKS:
                    parms->dwReturn = g_dwNumTracks;
                    break;
                case MCI_STATUS_CURRENT_TRACK:
                    parms->dwReturn = g_dwCurrentTrack;
                    break;
                case MCI_STATUS_LENGTH:
                    /* Return ~3 minutes per track in ms */
                    parms->dwReturn = 180000;
                    break;
                case MCI_STATUS_MODE:
                    if (g_bPlaying)
                        parms->dwReturn = g_bPaused ? MCI_MODE_PAUSE : MCI_MODE_PLAY;
                    else
                        parms->dwReturn = MCI_MODE_STOP;
                    break;
                case MCI_STATUS_MEDIA_PRESENT:
                    parms->dwReturn = TRUE;
                    break;
                case MCI_STATUS_READY:
                    parms->dwReturn = TRUE;
                    break;
                case MCI_STATUS_POSITION:
                    parms->dwReturn = MCI_MAKE_TMSF(g_dwCurrentTrack, 0, 0, 0);
                    break;
                case MCI_STATUS_TIME_FORMAT:
                    parms->dwReturn = g_dwTimeFormat;
                    break;
                case MCI_CDA_STATUS_TYPE_TRACK:
                    parms->dwReturn = MCI_CDA_TRACK_AUDIO;
                    break;
                default:
                    parms->dwReturn = 0;
                }
            }
        }
        return 0;

    case MCI_SET:
        {
            MCI_SET_PARMS* parms = (MCI_SET_PARMS*)lParam2;
            if (parms && (lParam1 & MCI_SET_TIME_FORMAT)) {
                g_dwTimeFormat = parms->dwTimeFormat;
            }
        }
        return 0;

    case MCI_GETDEVCAPS:
        {
            MCI_GETDEVCAPS_PARMS* parms = (MCI_GETDEVCAPS_PARMS*)lParam2;
            if (!parms) return MCIERR_NULL_PARAMETER_BLOCK;

            if (lParam1 & MCI_GETDEVCAPS_ITEM) {
                switch (parms->dwItem) {
                case MCI_GETDEVCAPS_CAN_PLAY:
                case MCI_GETDEVCAPS_HAS_AUDIO:
                    parms->dwReturn = TRUE;
                    break;
                case MCI_GETDEVCAPS_CAN_RECORD:
                case MCI_GETDEVCAPS_HAS_VIDEO:
                case MCI_GETDEVCAPS_CAN_EJECT:
                case MCI_GETDEVCAPS_CAN_SAVE:
                case MCI_GETDEVCAPS_USES_FILES:
                case MCI_GETDEVCAPS_COMPOUND_DEVICE:
                    parms->dwReturn = FALSE;
                    break;
                case MCI_GETDEVCAPS_DEVICE_TYPE:
                    parms->dwReturn = MCI_DEVTYPE_CD_AUDIO;
                    break;
                default:
                    parms->dwReturn = 0;
                }
            }
        }
        return 0;

    case MCI_INFO:
        /* Return empty string for info requests */
        if (lParam2) {
            MCI_INFO_PARMS* parms = (MCI_INFO_PARMS*)lParam2;
            if (parms->lpstrReturn && parms->dwRetSize > 0)
                parms->lpstrReturn[0] = '\0';
        }
        return 0;

    default:
        return MCIERR_UNRECOGNIZED_COMMAND;
    }

    return 0;
}

/* DLL entry point */
BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
    switch (fdwReason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hinstDLL);
        /* Clear debug log */
        {
            FILE* f = fopen(DEBUG_LOG, "w");
            if (f) {
                fprintf(f, "mcicda.dll loaded - direct audio playback version\n");
                fclose(f);
            }
        }
        break;
    case DLL_PROCESS_DETACH:
        DebugLog("DLL unloading");
        StopPlayback();
        break;
    }
    return TRUE;
}
