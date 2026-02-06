/*
 * MCI CD Audio Driver with Direct waveOut Playback
 * Intercepts CD audio commands and plays WAV files using waveOut API.
 * No external controller needed.
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>
#include <stdarg.h>

#pragma comment(lib, "winmm.lib")

/* Internal MCI driver message IDs */
#ifndef MCI_OPEN_DRIVER
#define MCI_OPEN_DRIVER 0x0801
#endif
#ifndef MCI_CLOSE_DRIVER
#define MCI_CLOSE_DRIVER 0x0802
#endif

/* Music directory */
#define MUSIC_DIR "C:\\music\\"
#define DEBUG_LOG "C:\\mcicda_debug.log"

/* Device state */
static BOOL g_bOpen = FALSE;
static DWORD g_dwCurrentTrack = 2;
static DWORD g_dwNumTracks = 18;
static DWORD g_dwTimeFormat = MCI_FORMAT_TMSF;
static BOOL g_bPlaying = FALSE;
static BOOL g_bPaused = FALSE;

/* Audio playback state */
static HWAVEOUT g_hWaveOut = NULL;
static WAVEHDR g_waveHdr = {0};
static BYTE* g_pAudioData = NULL;
static HANDLE g_hPlayThread = NULL;
static volatile BOOL g_bStopRequested = FALSE;

/* Debug logging */
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

/* Build path to track file */
static void GetTrackPath(DWORD track, char* path, size_t pathSize)
{
    _snprintf(path, pathSize, "%strack%02d.wav", MUSIC_DIR, track);
}

/* Check if track exists */
static BOOL TrackExists(DWORD track)
{
    char path[MAX_PATH];
    GetTrackPath(track, path, MAX_PATH);
    return GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
}

/* Stop current playback */
static void StopPlayback(void)
{
    g_bStopRequested = TRUE;

    if (g_hPlayThread) {
        WaitForSingleObject(g_hPlayThread, 2000);
        CloseHandle(g_hPlayThread);
        g_hPlayThread = NULL;
    }

    if (g_hWaveOut) {
        waveOutReset(g_hWaveOut);
        if (g_waveHdr.dwFlags & WHDR_PREPARED) {
            waveOutUnprepareHeader(g_hWaveOut, &g_waveHdr, sizeof(WAVEHDR));
        }
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
    }

    if (g_pAudioData) {
        free(g_pAudioData);
        g_pAudioData = NULL;
    }

    ZeroMemory(&g_waveHdr, sizeof(g_waveHdr));
    g_bPlaying = FALSE;
    g_bPaused = FALSE;
    g_bStopRequested = FALSE;
}

/* Playback thread */
static DWORD WINAPI PlaybackThread(LPVOID param)
{
    char* path = (char*)param;
    HANDLE hFile;
    DWORD dwRead;
    BYTE header[44];
    WAVEFORMATEX wfx;
    DWORD dataSize;
    MMRESULT result;

    DebugLog("PlaybackThread started for: %s", path);

    /* Open WAV file */
    hFile = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE) {
        DebugLog("ERROR: Cannot open file (error %d)", GetLastError());
        free(path);
        return 1;
    }

    /* Read WAV header */
    if (!ReadFile(hFile, header, 44, &dwRead, NULL) || dwRead < 44) {
        DebugLog("ERROR: Cannot read WAV header");
        CloseHandle(hFile);
        free(path);
        return 1;
    }

    /* Verify RIFF/WAVE */
    if (memcmp(header, "RIFF", 4) != 0 || memcmp(header + 8, "WAVE", 4) != 0) {
        DebugLog("ERROR: Not a valid WAV file");
        CloseHandle(hFile);
        free(path);
        return 1;
    }

    /* Parse format (assuming PCM at offset 20) */
    wfx.wFormatTag = *(WORD*)(header + 20);
    wfx.nChannels = *(WORD*)(header + 22);
    wfx.nSamplesPerSec = *(DWORD*)(header + 24);
    wfx.nAvgBytesPerSec = *(DWORD*)(header + 28);
    wfx.nBlockAlign = *(WORD*)(header + 32);
    wfx.wBitsPerSample = *(WORD*)(header + 34);
    wfx.cbSize = 0;

    dataSize = *(DWORD*)(header + 40);

    DebugLog("WAV format: %d ch, %d Hz, %d bit, %d bytes",
             wfx.nChannels, wfx.nSamplesPerSec, wfx.wBitsPerSample, dataSize);

    /* Allocate buffer for audio data */
    g_pAudioData = (BYTE*)malloc(dataSize);
    if (!g_pAudioData) {
        DebugLog("ERROR: Cannot allocate %d bytes", dataSize);
        CloseHandle(hFile);
        free(path);
        return 1;
    }

    /* Read audio data */
    if (!ReadFile(hFile, g_pAudioData, dataSize, &dwRead, NULL)) {
        DebugLog("ERROR: Cannot read audio data");
        CloseHandle(hFile);
        free(path);
        free(g_pAudioData);
        g_pAudioData = NULL;
        return 1;
    }
    CloseHandle(hFile);

    DebugLog("Read %d bytes of audio data", dwRead);

    /* Open waveOut device */
    result = waveOutOpen(&g_hWaveOut, WAVE_MAPPER, &wfx, 0, 0, CALLBACK_NULL);
    if (result != MMSYSERR_NOERROR) {
        DebugLog("ERROR: waveOutOpen failed (error %d)", result);
        free(path);
        free(g_pAudioData);
        g_pAudioData = NULL;
        return 1;
    }

    DebugLog("waveOutOpen success");

    /* Prepare header */
    g_waveHdr.lpData = (LPSTR)g_pAudioData;
    g_waveHdr.dwBufferLength = dwRead;
    g_waveHdr.dwFlags = 0;

    result = waveOutPrepareHeader(g_hWaveOut, &g_waveHdr, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        DebugLog("ERROR: waveOutPrepareHeader failed (error %d)", result);
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
        free(path);
        free(g_pAudioData);
        g_pAudioData = NULL;
        return 1;
    }

    /* Write data to start playback */
    result = waveOutWrite(g_hWaveOut, &g_waveHdr, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR) {
        DebugLog("ERROR: waveOutWrite failed (error %d)", result);
        waveOutUnprepareHeader(g_hWaveOut, &g_waveHdr, sizeof(WAVEHDR));
        waveOutClose(g_hWaveOut);
        g_hWaveOut = NULL;
        free(path);
        free(g_pAudioData);
        g_pAudioData = NULL;
        return 1;
    }

    DebugLog("waveOutWrite success - playback started");
    g_bPlaying = TRUE;

    /* Wait for playback to complete or stop request */
    while (!g_bStopRequested) {
        if (g_waveHdr.dwFlags & WHDR_DONE) {
            DebugLog("Playback completed");
            break;
        }
        Sleep(100);
    }

    free(path);
    DebugLog("PlaybackThread exiting");
    return 0;
}

/* Play a track */
static BOOL PlayTrack(DWORD track)
{
    char path[MAX_PATH];
    char* pathCopy;

    DebugLog("PlayTrack called for track %d", track);

    StopPlayback();

    GetTrackPath(track, path, MAX_PATH);
    DebugLog("Track path: %s", path);

    if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) {
        DebugLog("ERROR: File not found");
        return FALSE;
    }

    /* Copy path for thread */
    pathCopy = _strdup(path);
    if (!pathCopy) {
        DebugLog("ERROR: Cannot allocate path");
        return FALSE;
    }

    g_dwCurrentTrack = track;
    g_bStopRequested = FALSE;

    /* Start playback thread */
    g_hPlayThread = CreateThread(NULL, 0, PlaybackThread, pathCopy, 0, NULL);
    if (!g_hPlayThread) {
        DebugLog("ERROR: Cannot create playback thread");
        free(pathCopy);
        return FALSE;
    }

    DebugLog("Playback thread created");
    return TRUE;
}

/* Pause playback */
static void PausePlayback(void)
{
    if (g_hWaveOut && g_bPlaying && !g_bPaused) {
        waveOutPause(g_hWaveOut);
        g_bPaused = TRUE;
        DebugLog("Paused");
    }
}

/* Resume playback */
static void ResumePlayback(void)
{
    if (g_hWaveOut && g_bPaused) {
        waveOutRestart(g_hWaveOut);
        g_bPaused = FALSE;
        DebugLog("Resumed");
    }
}

/* Count available tracks */
static DWORD CountTracks(void)
{
    DWORD count = 0;
    DWORD i;
    for (i = 2; i <= 99; i++) {
        if (TrackExists(i)) count++;
        else if (count > 0) break;
    }
    return count > 0 ? count + 1 : 18;
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

    if (msg == MCI_OPEN_DRIVER) {
        g_bOpen = TRUE;
        g_dwNumTracks = CountTracks();
        DebugLog("MCI_OPEN_DRIVER: %d tracks", g_dwNumTracks);
        return 0;
    }

    if (msg == MCI_CLOSE_DRIVER) {
        DebugLog("MCI_CLOSE_DRIVER");
        StopPlayback();
        g_bOpen = FALSE;
        return 0;
    }

    if (!g_bOpen)
        return MCIERR_DEVICE_NOT_READY;

    switch (msg) {
    case MCI_OPEN:
        DebugLog("MCI_OPEN");
        return 0;

    case MCI_CLOSE:
        DebugLog("MCI_CLOSE");
        StopPlayback();
        return 0;

    case MCI_PLAY:
        {
            DWORD dwFrom = g_dwCurrentTrack;
            DebugLog("MCI_PLAY received");

            if (lParam1 & MCI_FROM) {
                MCI_PLAY_PARMS* parms = (MCI_PLAY_PARMS*)lParam2;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwFrom = MCI_TMSF_TRACK(parms->dwFrom);
                else
                    dwFrom = parms->dwFrom;
            }

            DebugLog("Playing track %d", dwFrom);
            PlayTrack(dwFrom);
        }
        return 0;

    case MCI_STOP:
        DebugLog("MCI_STOP");
        StopPlayback();
        return 0;

    case MCI_PAUSE:
        DebugLog("MCI_PAUSE");
        PausePlayback();
        return 0;

    case MCI_RESUME:
        DebugLog("MCI_RESUME");
        ResumePlayback();
        return 0;

    case MCI_SEEK:
        if (lParam1 & MCI_TO) {
            MCI_SEEK_PARMS* parms = (MCI_SEEK_PARMS*)lParam2;
            DWORD dwTrack;
            if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                dwTrack = MCI_TMSF_TRACK(parms->dwTo);
            else
                dwTrack = parms->dwTo;
            g_dwCurrentTrack = dwTrack;
            DebugLog("MCI_SEEK to track %d", dwTrack);
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
        {
            FILE* f = fopen(DEBUG_LOG, "w");
            if (f) {
                fprintf(f, "mcicda.dll loaded - waveOut direct playback\n");
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
