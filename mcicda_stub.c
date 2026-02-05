/*
 * Stub MCI CD Audio Driver
 * Returns success for all operations and logs commands to a file
 * for external playback control.
 */

#include <windows.h>
#include <mmsystem.h>
#include <stdio.h>

/* Log file path - in the same directory as the DLL */
#define LOG_FILE "C:\\mcicda_commands.log"

/* Device state */
static BOOL g_bOpen = FALSE;
static DWORD g_dwCurrentTrack = 2;
static DWORD g_dwNumTracks = 18;
static DWORD g_dwTimeFormat = MCI_FORMAT_TMSF;
static BOOL g_bPlaying = FALSE;
static BOOL g_bPaused = FALSE;

/* Write a command to the log file */
static void LogCommand(const char* cmd)
{
    FILE* f = fopen(LOG_FILE, "a");
    if (f) {
        fprintf(f, "%s\n", cmd);
        fflush(f);
        fclose(f);
    }
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
        LogCommand("OPEN");
        return 0;
    }

    if (msg == MCI_CLOSE_DRIVER) {
        g_bOpen = FALSE;
        g_bPlaying = FALSE;
        LogCommand("CLOSE");
        return 0;
    }

    if (!g_bOpen)
        return MCIERR_DEVICE_NOT_READY;

    switch (msg) {
    case MCI_OPEN:
        LogCommand("OPEN");
        return 0;

    case MCI_CLOSE:
        g_bPlaying = FALSE;
        LogCommand("CLOSE");
        return 0;

    case MCI_PLAY:
        {
            char buf[64];
            DWORD dwFrom = g_dwCurrentTrack;
            DWORD dwTo = g_dwNumTracks;

            if (lParam1 & MCI_FROM) {
                MCI_PLAY_PARMS* parms = (MCI_PLAY_PARMS*)lParam2;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwFrom = MCI_TMSF_TRACK(parms->dwFrom);
                else
                    dwFrom = parms->dwFrom;
            }
            if (lParam1 & MCI_TO) {
                MCI_PLAY_PARMS* parms = (MCI_PLAY_PARMS*)lParam2;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwTo = MCI_TMSF_TRACK(parms->dwTo);
                else
                    dwTo = parms->dwTo;
            }

            g_dwCurrentTrack = dwFrom;
            g_bPlaying = TRUE;
            g_bPaused = FALSE;

            sprintf(buf, "PLAY %d %d", dwFrom, dwTo);
            LogCommand(buf);
        }
        return 0;

    case MCI_STOP:
        g_bPlaying = FALSE;
        g_bPaused = FALSE;
        LogCommand("STOP");
        return 0;

    case MCI_PAUSE:
        if (g_bPlaying) {
            g_bPaused = TRUE;
            LogCommand("PAUSE");
        }
        return 0;

    case MCI_RESUME:
        if (g_bPaused) {
            g_bPaused = FALSE;
            LogCommand("RESUME");
        }
        return 0;

    case MCI_SEEK:
        {
            char buf[64];
            if (lParam1 & MCI_TO) {
                MCI_SEEK_PARMS* parms = (MCI_SEEK_PARMS*)lParam2;
                DWORD dwTrack;
                if (g_dwTimeFormat == MCI_FORMAT_TMSF)
                    dwTrack = MCI_TMSF_TRACK(parms->dwTo);
                else
                    dwTrack = parms->dwTo;
                g_dwCurrentTrack = dwTrack;
                sprintf(buf, "SEEK %d", dwTrack);
                LogCommand(buf);
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
        /* Clear the log file on load */
        {
            FILE* f = fopen(LOG_FILE, "w");
            if (f) fclose(f);
        }
        break;
    }
    return TRUE;
}
