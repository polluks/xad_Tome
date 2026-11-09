/**
 * xadClient.h - XAD Client API definitions for tome_xad
 *
 * Based on the XADMaster library API (AmigaOS).
 * This header provides the types and prototypes needed to write
 * an external XAD client library.
 *
 * An XAD client is a shared library (.library) installed in LIBS:xad/.
 * It does NOT link against xadmaster at build time. Instead, xadmaster
 * opens the client library and calls its exported functions. The client
 * opens xadmaster.library itself during its init phase and stores the
 * library base in a global for use by all client functions.
 *
 * This implementation is written in ANSI C (C89) and is therefore
 * self-contained: it does not depend on any AmigaOS system headers.
 */

#ifndef XADCLIENT_H
#define XADCLIENT_H

#include <stddef.h>

/* ---------- Basic XAD Types ---------- */

typedef signed long xadINT32;
typedef unsigned long xadUINT32;
typedef unsigned short xadUINT16;
typedef unsigned char xadUINT8;
typedef unsigned long xadSize;
typedef char *xadSTRPTR;
typedef void *xadPTR;
typedef int xadBOOL;
typedef int xadERROR;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define XADTRUE 1
#define XADFALSE 0

/* ---------- Tag System ---------- */

#define TAG_DONE 0L
#define TAG_IGNORE 1L
#define TAG_MORE 2L
#define TAG_SKIP 3L
#define TAG_USER ((unsigned long)1 << 31)

#define XAD_DATEUNIX (TAG_USER + 1)
#define XAD_GETDATEXADDATE (TAG_USER + 5)
#define XAD_MAKELOCALDATE (TAG_USER + 6)
#define XAD_SETINPOS (TAG_USER + 20)

/* ---------- XAD Error Codes ---------- */

#define XADERR_OK 0
#define XADERR_UNKNOWN 1
#define XADERR_INPUT 2
#define XADERR_OUTPUT 3
#define XADERR_BADPARAMS 4
#define XADERR_NOMEMORY 5
#define XADERR_ILLEGALDATA 6
#define XADERR_NOTSUPPORTED 7
#define XADERR_RESOURCE 8
#define XADERR_DECRUNCH 9
#define XADERR_FILETYPE 10
#define XADERR_OPENFILE 11
#define XADERR_SKIP 12
#define XADERR_BREAK 13
#define XADERR_FILEEXISTS 14
#define XADERR_PASSWORD 15
#define XADERR_MAKEDIR 16
#define XADERR_CHECKSUM 17
#define XADERR_VERIFY 18
#define XADERR_GEOMETRY 19
#define XADERR_DATAFORMAT 20
#define XADERR_EMPTY 21
#define XADERR_FILESYSTEM 22
#define XADERR_FILEDIR 23
#define XADERR_SHORTBUFFER 24
#define XADERR_ENCODING 25

/* ---------- XAD Access Commands ---------- */

#define XADAC_READ 1
#define XADAC_WRITE 2
#define XADAC_COPY 3
#define XADAC_INPUTSEEK 4
#define XADAC_OUTPUTSEEK 5

/* ---------- XAD Object Types ---------- */

#define XADOBJ_ARCHIVEINFO 0x0001
#define XADOBJ_FILEINFO 0x0002
#define XADOBJ_DISKINFO 0x0003

/* ---------- XAD Allocation Tags ---------- */

#define XAD_OBJNAMESIZE (TAG_USER + 10)
#define XAD_OBJCOMMENTSIZE (TAG_USER + 11)
#define XAD_OBJPRIVINFOSIZE (TAG_USER + 12)

/* ---------- XAD FileInfo Flags ---------- */

#define XADFIF_DIRECTORY (1 << 1)
#define XADFIF_LINK (1 << 2)
#define XADFIF_CRYPTED (1 << 3)
#define XADFIF_EXTRACTONBUILD (1 << 4)
#define XADFIF_NODATE (1 << 5)
#define XADFIF_INFOTEXT (1 << 6)
#define XADFIF_NOFILENAME (1 << 7)
#define XADFIF_SEEKDATAPOS (1 << 8)
#define XADFIF_ENTRYMAYCHANGE (1 << 9)
#define XADFIF_UNIXPROTECTION (1 << 12)
#define XADFIF_DOSPROTECTION (1 << 13)

/* ---------- XAD Client Flags ---------- */

#define XADCF_FILEARCHIVER (1 << 0)
#define XADCF_DISKARCHIVER (1 << 1)
#define XADCF_FILEREADFRIENDLY (1 << 2)
#define XADCF_FREESPECIALINFO (1 << 3)
#define XADCF_FREEFILEINFO (1 << 4)
#define XADCF_FREEDISKINFO (1 << 5)
#define XADCF_ANYZIP (1 << 6)
#define XADCF_NOCHECKSIZE (1 << 7)

/* ---------- XAD Date ---------- */

struct xadDate {
    xadUINT32 xd_Day;
    xadUINT32 xd_Month;
    xadUINT32 xd_Year;
    xadUINT32 xd_Hour;
    xadUINT32 xd_Minute;
    xadUINT32 xd_Second;
};

/* ---------- XAD FileInfo ---------- */

struct xadFileInfo {
    struct xadFileInfo *xfi_Next;
    xadUINT32 xfi_EntryNumber;
    xadSTRPTR xfi_EntryInfo;
    xadPTR xfi_PrivateInfo;
    xadUINT32 xfi_Flags;
    xadSTRPTR xfi_FileName;
    xadSTRPTR xfi_Comment;
    xadUINT32 xfi_Protection;
    xadUINT32 xfi_OwnerUID;
    xadUINT32 xfi_OwnerGID;
    xadSTRPTR xfi_UserName;
    xadSTRPTR xfi_GroupName;
    xadSize xfi_Size;
    xadSize xfi_GroupCrSize;
    xadSize xfi_CrunchSize;
    xadSTRPTR xfi_LinkName;
    struct xadDate xfi_Date;
    xadUINT16 xfi_Generation;
    xadSize xfi_DataPos;
    struct xadFileInfo *xfi_MacFork;
    xadUINT16 xfi_UnixProtect;
    xadUINT8 xfi_DosProtect;
    xadUINT8 xfi_FileType;
};

/* ---------- XAD ArchiveInfo ---------- */

#define XADAIF_FILECORRUPT (1 << 0)

struct xadArchiveInfo {
    struct xadClient *xai_Client;
    xadPTR xai_PrivateClient;
    xadSTRPTR xai_Password;
    xadUINT32 xai_Flags;
    xadUINT32 xai_LowCyl;
    xadUINT32 xai_HighCyl;
    xadSize xai_InPos;
    xadSize xai_InSize;
    xadSize xai_OutPos;
    xadSize xai_OutSize;
    struct xadFileInfo *xai_FileInfo;
    struct xadFileInfo *xai_CurFile;
    xadERROR xai_LastError;
    xadSTRPTR xai_InName;
};

/* ---------- XAD Client ---------- */

#define XADCLIENT_VERSION 1
#define XADNEXTCLIENT ((struct xadClient *)0)

struct xadClient {
    struct xadClient *xc_Next;
    xadUINT16 xc_Version;
    xadUINT16 xc_MasterVersion;
    xadUINT16 xc_ClientVersion;
    xadUINT16 xc_ClientRevision;
    xadUINT32 xc_RecogSize;
    xadUINT32 xc_Flags;
    xadUINT32 xc_Identifier;
    const char *xc_ArchiverName;
    xadBOOL (*xc_RecogData)(xadUINT32, const xadUINT8 *, struct xadArchiveInfo *);
    xadERROR (*xc_GetInfo)(struct xadArchiveInfo *);
    xadERROR (*xc_UnArchive)(struct xadArchiveInfo *);
    void (*xc_Free)(struct xadArchiveInfo *);
};

/* ---------- Client Declaration Macros ---------- */

#define XADRECOGDATA(name) \
    static xadBOOL Recog_##name(xadUINT32 size, const xadUINT8 *data, \
                                struct xadArchiveInfo *ai)

#define XADGETINFO(name) \
    static xadERROR GetInfo_##name(struct xadArchiveInfo *ai)

#define XADUNARCHIVE(name) \
    static xadERROR UnArchive_##name(struct xadArchiveInfo *ai)

#define XADFREE(name) \
    static void Free_##name(struct xadArchiveInfo *ai)

#define XADRECOGDATAP(name)     Recog_##name
#define XADGETINFOP(name)       GetInfo_##name
#define XADUNARCHIVEP(name)     UnArchive_##name
#define XADFREEP(name)          Free_##name

#define XADFIRSTCLIENT(name) \
    static const struct xadClient FirstClient_##name = \
    { \
        XADNEXTCLIENT, \
        XADCLIENT_VERSION, \
        13, \
        TOME_CLIENT_VERSION, \
        TOME_CLIENT_REVISION, \
        TOME_RECOG_SIZE, \
        XADCF_FILEARCHIVER | XADCF_FILEREADFRIENDLY, \
        0, \
        TOME_ARCHIVERNAME, \
        XADRECOGDATAP(name), \
        XADGETINFOP(name), \
        XADUNARCHIVEP(name), \
        XADFREEP(name) \
    };

#define XADCLIENTVERSTR(str) \
    static const char _VersTag[] = "$VER: " str;

/* ---------- Client Implementation Entry Points ---------- */

/*
 * The format specific client library implements the XAD client
 * protocol (see the XADFIRSTCLIENT macro above) and provides the
 * convenience entry points used by XAD compatible tools that want to
 * install/update directly from an in-memory archive image.
 */

int xad_tome_client_install(const unsigned char *data, unsigned int size, const char *output_dir);
int xad_tome_client_recognize(const unsigned char *data, unsigned int size);
const char *xad_tome_client_name(void);

#endif /* XADCLIENT_H */
