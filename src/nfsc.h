#define HAVE_NFSSUPPORT
#define HAVE_TERMEMU

#define VERSION_NUM    54		/* must be integer */
#define VERSION_STRING "5.4"

#define PREFIX			0x80		/* additional for the new prot*/

#define XY_XMODEM		1
#define XY_XMODEMCRC		2
#define XY_XMODEMCRC1K		3
#define XY_YMODEM		4
#define XY_YMODEM1K		5
#define XY_YMODEMG		6
#define XY_YMODEMG1K		7

#define ON 			1
#define OFF			0
#define NR_OF_DEVICES		5

#define MAXCOLS 80		/* Longest line possible */

#define P_WRITE(_h, _p, _n) serial_write (_p, _n)
#define P_READ(_h, _p, _n)  { p_read  (_h, _p, (_n)); stats_read  += (_n); }

struct parameters {
	char *name;
	char type;	/* 0 string 
	                   1 numeric (unsigned character)
			   2 function
			   3 offon
			   4 choicelist
	                   5 numeric (unsigned word)
			   */
	unsigned char *value;
	char **arg;
};

struct fkey	/* Function key */
{
	UWORD key;
	UBYTE modifier;
	struct fkey *next;
};

struct sendexpect {
	char *txt;
	struct sendexpect *next;
};

typedef struct
{
  E_MESSAGE m; /* Defined in EPOC.H */
  UWORD arg1; /* First argument */
  UWORD arg2; /* Second argument */
} MESS;

#define QueueTimer(when) p_ioa4(timH, P_FRELATIVE, &timstat, &when)


extern UBYTE  charwidth,charheight, ascent, rows, cols;   
extern WORD   t_w, t_h;
extern UINT   main_win;
extern INT    hgc;
extern BYTE   debug;
extern VOID   *serial;
extern char   cur_appl;		/* Keypad mode or not / vt100 */
extern unsigned char *sb_buf, *sb_nl;			
extern int sb_beg, sb_end;
extern WORD   mstat;
extern HANDLE sendtopid;
extern char   *sendtoaddr;
extern char   mfb[];


extern void   serial_write(char *buf, int len);
extern void   AddToHistory (char ch);
extern void   DoXYmodem (char ch);
extern int    Check (INT val,TEXT *msg);
extern void   Sw_Serial (char);
GLREF_C VOID  TtyEmu(unsigned char *buf, INT len);
GLREF_C VOID  Reset(void);
extern unsigned char iso2cp(unsigned char ch);

extern void   AddFnKey(int, int, char *);
extern void   SendFnKey(struct fkey *);
extern void   InitParams(void);
extern int    LoadParams(char *fname, int warn);
extern int    SaveParams(char *fname, int warn);
extern void   DoExit(int);
extern void   DoPaste(void);
extern void   DoBring(void);
extern void   Conv2Ser(void);
extern void   SetScrollback(int);
extern int    DoCopy(int key, int modifier);
extern void   DoRegister(int state);
extern void   DoSrvBring(void);
extern int    getsbll(int row);
extern void   ParseKey(UWORD key, UBYTE modifier);
extern void   SendModem(void);

extern unsigned char pm_baud, pm_databits, pm_stopbits, pm_parity, pm_xonxoff,
	pm_nortscts, pm_dodsr, pm_dodcd, pm_dodtr, pm_ignpar, pm_statusline,
	pm_clockwin, pm_protocol, pm_jumpscroll, pm_debugterm, pm_font,
	pm_fontsize, pm_isotoibm, pm_maxlines, pm_updfreq, pm_xymodem,
	pm_autodial, pm_port;
extern	WORD	pm_scrollback;
extern char pm_xyname[129];
extern char pm_dialpgm[129];
extern char pm_devices[NR_OF_DEVICES];
