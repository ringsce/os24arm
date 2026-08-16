typedef struct _STARTDATA
{
USHORT Length; /* The length of the data structure, in bytes,
including Length itself. */
USHORT Related; /* An indicator which specifies whether the
session created is related to the calling session. */
USHORT FgBg; /* An indicator which specifies whether the new
session should be started in the foreground or background. */
USHORT TraceOpt; /* An indicator which specifies whether the
program started in the new session should be executed under conditions
for tracing. */
PSZ PgmTitle; /* Address of an ASCIIZ string that contains the
program title. */
PSZ PgmName; /* The address of an ASCIIZ string that contains
the file specification of the program to be loaded. */
PBYTE PgmInputs;/* Either 0 or the address of an ASCIIZ string
that contains the input arguments to be passed to the program. */
PBYTE TermQ; /* Either 0 or the address of an ASCIIZ string
that contains the file specification of a system queue. */
PBYTE Environment;/* The address of an environment string to be
passed to the program started in the new session. */
USHORT InheritOpt; /* Specifies whether the program started in the
new session should inherit the calling program's environment and open
file handles. */
USHORT SessionType;/* The type of session that should be created
for this program. */
PSZ IconFile; /* Either 0 or the address of an ASCIIZ string
that contains the file specification of an icon definition. */
ULONG PgmHandle; /* Either 0 or the program handle. */
USHORT PgmControl;/* An indicator which specifies the initial
state for a windowed application. */
USHORT InitXPos; /* The initial x-coordinate, in pels, for the
initial session window. */
USHORT InitYPos; /* The initial y-coordinate, in pels, for the
initial session window. */
USHORT InitXSize; /* The initial x extent, in pels, for the
initial session window. */
USHORT InitYSize; /* The initial y extent, in pels, for the
initial session window. */
USHORT Reserved; /* Reserved; must be zero. */
PSZ ObjectBuffer; /* Buffer in which the name of the object
that contributed to the failure of DosExecPgm is returned. */
ULONG ObjectBuffLen;/* The length, in bytes, of the buffer
pointed to by ObjectBuffer. */
} STARTDATA;
typedef STARTDATA *PSTARTDATA;
