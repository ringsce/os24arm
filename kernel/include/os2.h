#ifndef OS2_H
#define OS2_H

/* ─────────────────────────────────────────────────────────────────────────────
   Bare-metal AArch64 build: -nostdinc means stdint.h is unavailable.
   Define all fixed-width types manually.
   ───────────────────────────────────────────────────────────────────────────── */
typedef unsigned char          uint8_t;
typedef signed   char          int8_t;
typedef unsigned short         uint16_t;
typedef signed   short         int16_t;
typedef unsigned int           uint32_t;
typedef signed   int           int32_t;
typedef unsigned long          uint64_t;
//typedef signed   long          int64_t;
typedef unsigned long          size_t;
typedef signed   long          ssize_t;

#ifdef __aarch64__
#define APIENTRY
#define EXPENTRY
#endif

/* ========================================================= */
/* Basic OS/2 Type System (32-bit semantics preserved)       */
/* ========================================================= */

typedef uint32_t  ULONG;
typedef int32_t   LONG;
typedef uint16_t  USHORT;
typedef int16_t   SHORT;
typedef uint8_t   UCHAR;
typedef int32_t   BOOL;
typedef uint32_t  APIRET;

typedef char*     PSZ;
typedef char*     PCHAR;
typedef void*     PVOID;
typedef ULONG*    PULONG;
typedef APIRET*   PAPIRET;

typedef ULONG     HFILE;
typedef ULONG     PID;
typedef ULONG     TID;
typedef ULONG     HMODULE;
typedef ULONG     HEV;
typedef ULONG     HMTX;
typedef ULONG     HSEM;

#define TRUE     1
#define FALSE    0
#define NULL     ((void *)0)
#define NO_ERROR 0

/* ========================================================= */
/* FILE APIs                                                 */
/* ========================================================= */

APIRET APIENTRY DosOpen(
    PSZ     pszFileName,
    HFILE  *phFile,
    PULONG  pulAction,
    ULONG   cbFile,
    ULONG   ulAttribute,
    ULONG   fsOpenFlags,
    ULONG   fsOpenMode,
    PVOID   peaop2
);

APIRET APIENTRY DosRead(
    HFILE  hFile,
    PVOID  pBuffer,
    ULONG  cbRead,
    PULONG pcbActual
);

APIRET APIENTRY DosWrite(
    HFILE  hFile,
    PVOID  pBuffer,
    ULONG  cbWrite,
    PULONG pcbActual
);

APIRET APIENTRY DosClose(HFILE hFile);

APIRET APIENTRY DosDelete(PSZ pszFile);

APIRET APIENTRY DosMove(PSZ pszOld, PSZ pszNew);

APIRET APIENTRY DosQueryFileInfo(
    HFILE hFile,
    ULONG ulInfoLevel,
    PVOID pInfo,
    ULONG cbInfo
);

/* ========================================================= */
/* MEMORY APIs                                               */
/* ========================================================= */

APIRET APIENTRY DosAllocMem(
    PVOID *ppb,
    ULONG  cb,
    ULONG  flags
);

APIRET APIENTRY DosFreeMem(PVOID pb);

APIRET APIENTRY DosSetMem(
    PVOID pb,
    ULONG cb,
    ULONG flags
);

APIRET APIENTRY DosQueryMem(
    PVOID  pb,
    PULONG pcb,
    PULONG pFlags
);

/* ========================================================= */
/* PROCESS APIs                                              */
/* ========================================================= */

APIRET APIENTRY DosExecPgm(
    PCHAR  pObjName,
    LONG   cbObjName,
    ULONG  execFlag,
    PSZ    pArg,
    PSZ    pEnv,
    PVOID  pResult,
    PSZ    pName
);

APIRET APIENTRY DosExit(
    ULONG action,
    ULONG result
);

APIRET APIENTRY DosWaitChild(
    ULONG  action,
    ULONG  waitOption,
    PVOID  pResult,
    PULONG pPID,
    PID    pid
);

/* ========================================================= */
/* THREAD APIs                                               */
/* ========================================================= */

APIRET APIENTRY DosCreateThread(
    TID   *ptid,
    PVOID  pfnThread,
    ULONG  param,
    ULONG  flags,
    ULONG  stackSize
);

APIRET APIENTRY DosExitThread(ULONG result);

APIRET APIENTRY DosWaitThread(
    TID   *ptid,
    ULONG  option
);

APIRET APIENTRY DosSleep(ULONG msec);

/* ========================================================= */
/* SEMAPHORES – Event                                        */
/* ========================================================= */

APIRET APIENTRY DosCreateEventSem(
    PSZ   pszName,
    HEV  *phev,
    ULONG flAttr,
    BOOL  fState
);

APIRET APIENTRY DosPostEventSem(HEV hev);

APIRET APIENTRY DosWaitEventSem(
    HEV   hev,
    ULONG timeout
);

APIRET APIENTRY DosCloseEventSem(HEV hev);

/* ========================================================= */
/* SEMAPHORES – Mutex                                        */
/* ========================================================= */

APIRET APIENTRY DosCreateMutexSem(
    PSZ   pszName,
    HMTX *phmtx,
    ULONG flAttr,
    BOOL  fInitial
);

APIRET APIENTRY DosRequestMutexSem(
    HMTX  hmtx,
    ULONG timeout
);

APIRET APIENTRY DosReleaseMutexSem(HMTX hmtx);

APIRET APIENTRY DosCloseMutexSem(HMTX hmtx);

/* ========================================================= */
/* MODULE / DLL APIs                                         */
/* ========================================================= */

APIRET APIENTRY DosLoadModule(
    PSZ      pszFailName,
    ULONG    cbFailName,
    PSZ      pszModuleName,
    HMODULE *phmod
);

APIRET APIENTRY DosFreeModule(HMODULE hmod);

APIRET APIENTRY DosQueryProcAddr(
    HMODULE  hmod,
    ULONG    ordinal,
    PSZ      pszProcName,
    PVOID   *ppfn
);

/* ========================================================= */
/* TIME / SYSTEM INFO                                        */
/* ========================================================= */

APIRET APIENTRY DosQuerySysInfo(
    ULONG iStart,
    ULONG iLast,
    PVOID pBuf,
    ULONG cbBuf
);

APIRET APIENTRY DosGetDateTime(PVOID pdt);

APIRET APIENTRY DosSetDateTime(PVOID pdt);

#endif /* OS2_H */

