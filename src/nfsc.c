/*
 * Copyright 1993-1996 Rudolf Koenig, Michael Schroeder, Juergen Weigert
 * see the GNU Public License V2 for details
 *
 * In this file lines end with CR/LF, as our psion C-compiler lives in the 
 * DOS-WORLD
 */

#include <plib.h>                                 /* NFS Client V3.10 N.N */
#include <p_serial.h>
#include <wlib.h>
#include <hwif.h>
#include "nfsc.rsg"			/* The help file definitions */
#include "nfsc.h"

#define TXT  H_DTEXT_ALIGN_CENTRE

/*========================================================== menu's used ===*/
static H_MENU_DATA mdata[]= {
    "File",		5,
    "Serial",		5,
#ifdef HAVE_TERMEMU
    "Emu-",		6,
    "lator",		5,
    "Xmit",		3,
#endif
#ifdef HAVE_NFSSUPPORT
    "UNIX",		2,
#endif
    NULL
};

static TEXT * cmds[]= {
/* nfsc */
	"lLoad config...",		/* 0 */
	"sSave config as...",
        "aAbout...",
        "XExit without saving",
        "xExit",
/* Serial Line */
        "pParameters...",		/* 5 */
        "eHandshake for experts...",
        "jStatus line...",
        "hHangup",
	"+Send a break",
#ifdef HAVE_TERMEMU
/* Emulator */
	"fFont...",			/* 10 */
	"zZoom: bigger",
	"ZZoom: smaller",
	"cCopy mode...",
	"iPaste",
	"bBring",			/* 15 */
/* Emulator*/
    	"rReset emulator",
	"kFn Keys...",
	"dDial support...",
	"qSettings...",
	"TToggle echo",
/* XYmodem */
	"uConfigure...",		/* 20 */
	"wTransmit file...",
	"yReceive file...",
#endif
#ifdef HAVE_NFSSUPPORT
/* Unix */
        "gExport...",
	"tProtocol...",
#endif
        NULL
};

#define NCMDS (sizeof(cmds) / sizeof(*cmds))

TEXT **        _cmds=cmds;
H_MENU_DATA *  _mdata=mdata;


/*===================================================== global variables ===*/
extern UWORD _UseFullScreen;    /* use full s3a or only s3 screen */

ULONG  stats_read, stats_write;	/* Read / write statistics */

P_RECT hisrect;			/* Rectangle for nfscmd history scrolling */
INT    stat_y,modem_x,io_x;	/* Y pos for stat printing (font descent),etc*/
UBYTE  charwidth,charheight,	/* Character data */
       ascent, rows, cols;
WORD   screenx,screeny,		/* Character width */
       t_w, t_h;		/* terminal width & height in pixels */

INT    hgc;			/* handle to graphics context */
UINT   main_win;		/* main window ID */
VOID   *serial = 0;		/* handle serial port */
VOID   *timH;			/* handle timer */
ULONG  timint;			/* poll interval in 100msec units */
WORD   timstat;
WORD   keystat;
WMSG_KEY   key;

char   zero=0;
WORD   one=1;
UBYTE  pctrl = 0xff;		/* store last pctrl */
UBYTE  sch;			/* Serial character */
WORD   ttystat;			/* We need it global */
P_SRCHAR pm_tty;
UBYTE	 pm_pctrl[3];
UBYTE  is_rtscts;

WORD   mstat;			/* Bring messages */

char copying = 0;
static char parmfile[129];
extern TEXT *DatUsedPathNamePtr;
extern char *dev_name[];

#ifdef HAVE_TERMEMU
struct fkey *fk = 0;
char   cur_appl;		/* cursor keys in application mode */
char   echoon = 1;
BYTE   fnkey = 0;
void  *logfd = 0;
#include "cnv.h"		/* charset conversion */
#endif

BYTE   isnfsd = 0;		/* used for sending a resize event *gulp* */
char   buf[256], mfb[256];	/* serial read buffers, not on stack  */
VOID   *rfile = 0;		/* last read filehandle */

/* Common strings to save some place */
char cant_open_the_file[] = "Can't open the file";
static char c_ignore[]      = "Ignore";
static char c_load[]        = "Load";
static char c_save[]        = "Save";
static char c_cfgserial[]   = "configure serial";
static char c_dtr[]         = "change DTR";
static char c_query[]       = "query modem";
static char c_on[]          = "On";
static char c_off[]         = "Off";
static char c_yes[]         = "Yes";
static char c_no[]          = "No";

/* Lets convert the pm_* parameters to the pm_tty struct */
void
Conv2Ser()
{
  pm_tty.tbaud = pm_tty.rbaud = pm_baud + 1;
  pm_tty.frame = pm_databits;
  if(pm_parity) pm_tty.frame |= P_PARITY;
  if(pm_stopbits == 2) pm_tty.frame |= P_TWOSTOP;
  pm_tty.parity = pm_parity;
  pm_tty.hand   = 0;
  if(pm_xonxoff) pm_tty.hand |= (P_SEND_XOFF|P_OBEY_XOFF);
  if(pm_nortscts) pm_tty.hand |= P_IGN_CTS;
  if(pm_dodsr) pm_tty.hand |= P_OBEY_DSR;
  if(pm_dodcd) pm_tty.hand |= P_OBEY_DCD;
  pm_tty.xon    = 19;
  pm_tty.xoff   = 17;
  pm_tty.flags  = (pm_ignpar ? P_IGNORE_PARITY : 0);
  pm_pctrl[0] = pm_pctrl[2] = 0;
  pm_pctrl[2] |= (pm_dodtr ? P_SRDTR_ON : P_SRDTR_OFF);
  pm_pctrl[1] = pm_pctrl[2];
}

/*======================================== Cancel / Reenable serial input ==*/
void Sw_Serial(char on)
{
  if(!serial)
    return;
  if(on)
    {
      one=1;
      p_ioa (serial, P_FREAD, &ttystat, &sch, &one);
    }
  else
    p_iow (serial, P_FCANCEL);
}

void
SetSer()
{
  if(!serial)
    return;
  Sw_Serial(OFF);		/* otherwise panic with P_FSET! */
  p_waitstat (&ttystat);
  Check(p_iow(serial,P_FSET,&pm_tty), c_cfgserial);
  Check(p_iow(serial,P_FCTRL,&pm_pctrl), c_dtr);
  pm_pctrl[1] = 0x00;		/* or ModemUpdate will do it again */
  Sw_Serial(ON);
}

/*========================================================= AddToHistory ===*/
void AddToHistory (char ch) {
  static char idle;
  P_POINT o;
  int i = hisrect.tl.x;

  if(! pm_statusline)
    return;

  if (!ch)		/* a heart beat indicator */
    {
      idle++;
      if (idle != 2)	/* Will wrap after 256 ticks. But that is o.k. */
        return;
      /* seen two heartbeats in a row. Print a '.' as idle indicator */
      ch = '.';		
    }
  else 
    idle = 0;

  gPrintText(hisrect.br.x-charwidth, stat_y, &ch, 1);
  o.y = 0;
  o.x = -charwidth;			
  hisrect.tl.x += charwidth;
  wScrollRect(main_win, &hisrect, &o);	/* scroll one character leftward */
  hisrect.tl.x = hisrect.br.x - charwidth;
  gClrRect(&hisrect, G_TRMODE_CLR);	/* clear space for next character */
  hisrect.tl.x = i;
}

int 
Check (INT val,TEXT *msg)
{
  if (!val) return 0;
  p_atos(mfb, "Failed to %s", msg);
  if (!p_notifyerr(val, mfb, "Abort", c_ignore, 0))
    p_exit(0);
  return 1;
}

#if 0
/* This won't work for my 3a, as it returns 7FFF, meaning that
   a maximum of 9600 baud is supported */
void
setbaudrates(void)
{
  UWORD pm[3], l, b, *p;
  extern char *baudlist[];

  p_iow(serial, P_FINQ, pm);
  p = pm;
  for(b = l = 0; l < 18; l++, b++)
    {
      if(b == 16)
        p++, b = 0;
      if((*p & (1 << b)) == 0)
        baudlist[l] = 0;
    }
}
#endif

static void
OpenPort(int new, int old)
{
  extern char *portlist[];
  static char srx[] = "TTY:SRX";

  if(serial)
    {
      Sw_Serial(OFF);
      p_close(serial);
      if(!p_scmpi(portlist[old], srx))
        p_devdel(portlist[old], E_PDD);
      serial = 0;
    }
  if(new)
    {
      if(!p_scmpi(portlist[new], srx)) /* 3Fax, experimental */
        {
	  if(Check(p_loadpdd("loc::c:\\img\\sys$a550.pdd"),"load 3Fax driver"))
	    {
	      pm_port = 0;
	      ttystat = E_FILE_PENDING;
	      return;
	    }
	}

      if(Check(p_open(&serial, portlist[new],-1), portlist[new]))
	serial = 0;
    }
  if(serial)
    {
      pm_port = new;
      Check(p_iow(serial,P_FSET,&pm_tty), c_cfgserial);
      Sw_Serial(ON);
      /* setbaudrates(); */
    }
  else
    {
      pm_port = 0;
      ttystat = E_FILE_PENDING;
    }
}

void SendModem()
{
  p_pcpyto(sendtopid, (VOID*)sendtoaddr, (VOID*)&zero, 1);
  p_pcpyto(sendtopid, (VOID*)(sendtoaddr+1), (VOID*)&pctrl, 1);
  if(p_msendreceivew(sendtopid, 0, &sendtopid) != 0)
    sendtopid = 0;
}

void UpdateModem()
{
  if(!serial || !pm_statusline )
    return;

  Check(p_iow(serial, P_FCTRL, pm_pctrl), c_query);
  if (pm_pctrl[0] == pctrl)
    return;
  
  pctrl = pm_pctrl[0];
  
  if(sendtopid)
    SendModem();
    
  p_atos(mfb, "%s %s %s", 
         (pctrl & P_SRCTRL_CTS) ? "CTS" : "cts",
         (pctrl & P_SRCTRL_DSR) ? "DSR" : "dsr",
         (pctrl & P_SRCTRL_DCD) ? "DCD" : "dcd");
  pm_pctrl[1] = 0x00;	 /* in case we did set it here */
  gPrintText(modem_x, stat_y, mfb, p_slen(mfb));
}


/*===============  Check, if write would block, i.e. flow control is on === */
void
serial_write(char *p, int n)
{
  int ret;

  if(!serial)
    {
      TtyEmu((unsigned char *)p, n);
      return;
    }
  if(!pm_nortscts)
    {
      Check(p_iow(serial, P_FCTRL, pm_pctrl), c_query);
      while(!(pm_pctrl[0] & P_SRCTRL_CTS))
	{
	  H_DI_TEXT  line1={buf+  0,TXT}, 
		     line2={buf+ 50,TXT};

	  uZTStoBCS (line1.str, "Hardware flowcontrol specified,");
	  uZTStoBCS (line2.str, "but modem line CTS is not set");
	  if (   uOpenDialog ("WARNING!")
	      || uAddDialogItem (H_DIALOG_TEXT, NULL, &line1)
	      || uAddDialogItem (H_DIALOG_TEXT, NULL, &line2)
	      || (uAddButtonList ("Exit",   'x',
				  c_ignore, 'i',
				  "Retry",  W_KEY_RETURN, NULL))
	      || (ret = uRunDialog()) <= 0)
		  continue;
	  if(ret == 'x')
	    p_exit(0);
	  if(ret == 'i')
	    {
	      pm_nortscts = 1;
	      Conv2Ser();
	      SetSer();
	      break;
	    }
	  Check(p_iow(serial, P_FCTRL, pm_pctrl), c_query);
	}
    }
  p_write (serial, p, n);
  stats_write += n;
}

/*============================================================ ShowStats ===*/
void ShowStats (void) {

  if(! pm_statusline )
    return;

  p_atos (mfb, "I/O:%07Lu/%07Lu", stats_read, stats_write);
  gPrintText(io_x, stat_y, mfb, p_slen(mfb));
}

/*=============================================================== Resize ===*/
/* Compute every position & redraw everything */
void Resize (void)
{
    W_WINDATA	wd;
    WORD	havestat;
    P_RECT	pr;
    G_FONT_INFO fi;
    G_GC	gc;
    int		ret, l1, l2;

    /*----------------------------------- draw screen with status window ---*/

    if(pm_clockwin)
      {
	if (_UseFullScreen) /* S3a */
	  {
	    if(pm_clockwin == 2)
	      havestat = W_STATUS_WINDOW_BIG;
	    else
	      havestat = W_STATUS_WINDOW_SMALL;
	    wInquireStatusWindow (havestat, &wd.extent);
	    wd.extent.tl.x = wd.extent.tl.y = 0;
	    wd.extent.width = screenx - wd.extent.width;
	    wd.extent.height = screeny;
	  }
	else
	  wsScreenExt (&wd.extent);   /* Much easier */
      }
    else
      {
	wd.extent.tl.x = wd.extent.tl.y = 0;
	wd.extent.width = screenx;
	wd.extent.height = screeny;
      }

    wSetWindow (main_win, W_WIN_EXTENT, &wd);

    if ((ret=wCheckPoint()) != 0) p_exit (ret);

    pr.tl.x = pr.tl.y = 0;
    pr.br.x = wd.extent.width;
    pr.br.y = wd.extent.height;
    gClrRect(&pr, G_TRMODE_CLR);

    if(pm_clockwin)
      {
	if(_UseFullScreen)
          wStatusWindow(havestat);
	else
          wsEnable();
      }
    else
      wsDisable();

    /*-------------------------------------------------------- Change GC ---*/
    gc.style = G_STY_MONO;
    gc.font  = WS_FONT_BASE + pm_fontsize;
    gSetGC(hgc, G_GC_MASK_STYLE|G_GC_MASK_FONT, &gc);

    /*----------------- compute layout of the graphics items: ---*/
    gFontInfo(gc.font, G_STY_MONO, &fi);

    charwidth  = fi.max_width;
    charheight = fi.height;
    ascent     = fi.ascent;
    t_w	       = wd.extent.width;
    t_h        = screeny;

    if (pm_statusline)
      {
        t_h -= charheight;

	l1  = gTextWidth(gc.font, G_STY_MONO, "I/O:9999999/9999999", 19);
	l2  = gTextWidth(gc.font, G_STY_MONO, "CTS DSR DCD", 11);

	stat_y = screeny - fi.descent;
	io_x = 0;

	hisrect.tl.x = l1+1;
	hisrect.tl.y = screeny - charheight;
	hisrect.br.x = t_w - l2;
	hisrect.br.y = screeny;

	modem_x = hisrect.br.x;

	/*
	gDrawLine(0, t_y, t_w, t_y);
	*/

	ShowStats();
	pctrl = 0xff;
	UpdateModem();
      }

    rows = t_h / charheight;
    if(rows > pm_maxlines)
      rows = pm_maxlines;
    t_h = charheight * rows;
    cols = t_w / charwidth;  t_w = charwidth * cols;
    if(isnfsd)
      {
	/* Resize event ... */
        mfb[0] = 0; mfb[1] = rows; mfb[2] = cols;
	P_WRITE(serial, mfb, 3);
      }

#ifdef HAVE_TERMEMU
    Reset();
    p_atos (mfb, "Size: %d x %d", cols, rows);
    wInfoMsgCorner(mfb, W_CORNER_BOTTOM_RIGHT);
#endif
}

/*=========================================== screen/menu initialization ===*/
void ScreenInitialize (void) {

    G_GC	gc;
    int		new;
    extern WSERV_SPEC *wserv_channel;

    /*-------------------------------------------------- initialize HWIF ---*/
    _UseFullScreen = TRUE;	/* Set this to False for testing S3 */
    new = hCrackCommandLine();
    uCommonInit();
    uEscape (FALSE);

    /*----------------------------------- read set from environment ---*/
    if(new == 0)
      p_scpy(parmfile+1, "\\opd\\nfsc.nfs");
    else
      p_scpy(parmfile+1, DatUsedPathNamePtr);
    *parmfile = p_slen(parmfile+1);

    if(new == 'C') /* Check for extension */
      {
        int l = *parmfile-3;

	if(l < 1) l = 1;
        if(p_sloc(parmfile+l, '.') < 0)
	  {
	    p_scpy(parmfile+1+*parmfile, ".nfs");
	    *parmfile += 4;
	  }
      }
    if(new != 'O' || LoadParams(parmfile, 1))
      InitParams();

    if(!_UseFullScreen)
      pm_fontsize = 0; /* S3 font for the first time */

#ifndef HAVE_NFSSUPPORT
    pm_protocol = 0;
#endif

    /*--------------------------------------------- set process priority ---*/
#if 0
    p_setpri (p_getpid(), E_MAX_PRIORITY);
    if (_UseFullScreen) wSetPriorityControl (FALSE);            /* S3a only */
#endif

    /*----------------------------------------------------- first resize ---*/
    main_win = uFindMainWid();
    screenx = wserv_channel->conn.info.pixels.x;
    screeny = wserv_channel->conn.info.pixels.y;


    gc.textmode = G_TRMODE_REPL; /* Default ist _SET */
    hgc = gCreateGC(main_win, G_GC_MASK_TEXTMODE, &gc);

    Resize();
}

/*=============================================================== DoExit ===*/
void
DoExit(save)
  int save;
{
  extern char cansave;
  if(save && cansave)
    SaveParams(parmfile, 0);
  p_exit(0);
}

#ifdef HAVE_TERMEMU
/*=============================================================== iso2cp ===*/
unsigned char
iso2cp(unsigned char ch)			/* Called only with ch > 127 */
{
  if(pm_isotoibm)
    return tbl_iso2cp[ch-128];
  else
    return ch;
}
#endif

/* 
   Exec support:
     Easy opo support
       The string is terminated in ".opo" and there are no spaces in it.
     Basic opa and .img support:
       The first word (till the first space) is taken as "program file name"
       In the second word (after the first space) each ~ is replaced by
       zeroes and it is taken as "program information". For more see the
       psionics file syscalls.1, Fn $87 Sub $01 (FilExecute).
 */
static int
doexec(char *str)
{
  HANDLE pid;
  int l;
  char *prg, *arg;

  l = p_bloc(str+1, *str, ' ');
  if(l < 0 && *str > 4 && !p_scmpi(str+*str-3, ".opo"))
    {
      p_scpy(mfb, "RunOpl");
      mfb[6] = mfb[7] = 0;
      p_bcpy(mfb+8, str+1, *str);
      l = 8 + *str;
      mfb[l++] = 0;
      prg = "ROM::SYS$PRGO";
      arg = mfb;
    }
  else
    {
      str[*str+1] = 0;
      if(l > 0)
        {
	  prg = str+1+l;
	  *prg++ = 0;
	  arg = prg;
	  for(l = 0; *prg; prg++, l++)
	    if(*prg == '~')
	      *prg = 0;
	}
      else
        {
	  arg = str+1;
	  l = *str;
	}
      prg = str+1;
    }

  pid = p_execc(prg, arg, l);
  if(pid > 0)
    {
      p_presume(pid);
      return 0;
    }
  else
    {
      p_errs(mfb, pid);
      wInfoMsgCorner(mfb, W_CORNER_BOTTOM_RIGHT);
      return 1;
    }
}


/*=================================================== ExecuteMenuCommand ===*/
void ParseKey (UWORD key, UBYTE modifier) {

    INT    ret;

    /*--------------------------------------------------- system event ---*/
    if (key == 0x404)
      {
        wGetCommand((UBYTE *)buf);
	if(*buf == 'X')
	  DoExit(1);
	if(*buf == 'O' || *buf == 'C')
	  {
	    SaveParams(parmfile, 1);
	    p_scpy(parmfile, buf);
	    hSetUpStatusNames(parmfile+1);
	    wsUpdate(WS_UPDATE_NAME);
	  }
	if(*buf == 'O')
	  LoadParams(parmfile, 1);
	InitParams();
	Resize();
	return;
      }
    if (key & 0x400)	/* Some other system  event */
      return;

#ifdef HAVE_TERMEMU
    if(copying)
      {
        if(DoCopy(key, modifier))
	  copying = 0;
	return;
      }
    /*------------------------------------------- Function keys -----------*/
    {	
      struct fkey **fp, *f;

      for(fp = &fk; *fp; fp = &(*fp)->next)
	if((*fp)->key == key && (*fp)->modifier == modifier)
	  break;
      f = *fp;

      if(fnkey)	/* Define or delete it */
	{
	  if(fnkey == 'd')
	    {
	      H_DI_TEXT  l2={buf+ 50, TXT},
			 l3={buf+100, TXT},
			 l4={buf+150, TXT};
	      H_DI_SEDIT l5={buf+152, 100, 35}; 

	    uZTStoBCS(l2.str,"Use \\<nnn> for nonprintable (e.g. \\027=ESC)");
	    uZTStoBCS(l3.str,"and \\s<nn> for delay (e.g. \\s99=9.9 seconds)");
	    uZTStoBCS(l4.str, "");
	    uZTStoBCS(l5.str, f ? (char *)f + sizeof(struct fkey) : "");

	      if (   uOpenDialog ("Characters to send")
		  || uAddDialogItem (H_DIALOG_TEXT, NULL, &l2)
		  || uAddDialogItem (H_DIALOG_TEXT, NULL, &l3)
		  || uAddDialogItem (H_DIALOG_TEXT, NULL, &l4)
		  || uAddDialogItem (H_DIALOG_SEDIT,"Send", &l5))
		      return;
	      if(uRunDialog() > 0)
		AddFnKey(key, modifier, l5.str);
	    }

	  if(fnkey == 'r' && f)
	    {
	      *fp = f->next;
	      p_free(f);
	      wInfoMsgCorner("FnKey deleted", W_CORNER_BOTTOM_RIGHT);
	    }
	  fnkey = 0;
	  return;
	}
	
      if(f)	/* Was the key a function key ? */
	{
	  SendFnKey(f);
	  return;
	}
    }
#endif

    /*------------------------------------------- clock window ------------*/
    if(key == W_KEY_MENU && modifier == W_CTRL_MODIFIER)
      {
	if(pm_clockwin == 0)
          pm_clockwin = (_UseFullScreen ? 2 : 1);
	else
	  pm_clockwin--;
	Resize();
        return;
      }

    if(key == W_KEY_HELP)
      {
	hHelpSubSystem(NFSC_HELP, 0);
        return;
      }

    /*------------------------------------- send key to the serial line ---*/
    if (!(key & W_SPECIAL_KEY) && (key!=W_KEY_MENU))
      {
#ifdef HAVE_TERMEMU
	/*
	 * Arrow key translation. This could also be a function key definition,
	 * but I have problems with the keypad mode.
	 */
	if(key >= 256 && key <= 259)
	  {
	    mfb[0] = 0x1b;
	    mfb[1] = cur_appl ? 'O' : '[';
	    mfb[2] = 'A' + (key - 256);
	    P_WRITE(serial, mfb, 3); 
	  }
	else
	  {
	    key = key & 0xff;
	    if(key > 127 && pm_isotoibm)
	      key = tbl_cp2iso[key-128];
	    P_WRITE(serial, (char *)&key, 1);		 /* BigEndian!!! */
	  }
#endif
	return; /* terminal */
      }

    /*------------------------- strip Psion modifier and get menu option ---*/
    key &= ~W_SPECIAL_KEY;
    if((key==W_KEY_MENU) && ((key=uPresentMenus()) <= 0))
      return;
    if (modifier & W_SHIFT_MODIFIER)
      key -= 0x20;

    /*---------------------------------------- switch on command request ---*/
    switch (key) {

        case 'l':   /*--------------------------------------------- Open ---*/
        case 's': { /*------------------------------------------ Save as ---*/
	    H_DI_FSEL  line3={ parmfile, 0 };
	    
	    if(key == 's')
	      line3.flags |= H_FILE_NEW_EDITOR;

	    if (   uOpenDialog ("Configuration")
		|| uAddDialogItem (H_DIALOG_FSEL, 
				   key == 'l' ? c_load : c_save, &line3)
		|| uRunDialog() <= 0)
		return;
	    if(key == 'l')
	      {
		LoadParams(parmfile, 1);
		InitParams();
		Resize();
	      }
	    else
	      SaveParams(parmfile, 1);
	    hSetUpStatusNames(parmfile+1);
	    wsUpdate(WS_UPDATE_NAME);
	    return;
	  }
        case 'a': { /*-------------------------------------------- About ---*/
            H_DI_TEXT  line1={buf+  0,TXT}, 
	               line2={buf+ 40,TXT},
		       line3={buf+ 80,TXT},
		       line4={buf+120,TXT},
		       line5={buf+160,TXT};

    	    p_atos (buf+200, "NFSC, version %s", VERSION_STRING);
            uZTStoBCS (line1.str, "¸ 1995, 1996  N.N, Rudolf K”nig,");
            uZTStoBCS (line2.str, "Michael Schr”der, Jrgen Weigert.");
            uZTStoBCS (line3.str, "Send email to rfkoenig or jnweigert");
	    uZTStoBCS (line4.str, "@immd4.informatik.uni-erlangen.de");
            uZTStoBCS (line5.str, "License: GNU Copyleft (Version 2)");

            if (   uOpenDialog (buf+200)
                || uAddDialogItem (H_DIALOG_TEXT, NULL, &line1)
                || uAddDialogItem (H_DIALOG_TEXT, NULL, &line2)
                || uAddDialogItem (H_DIALOG_TEXT, NULL, &line3)
                || uAddDialogItem (H_DIALOG_TEXT, NULL, &line4)
                || uAddDialogItem (H_DIALOG_TEXT, NULL, &line5)
                || (_UseFullScreen && uAddButtonList ("", W_KEY_RETURN, NULL))
                || uRunDialog() <= 0)
                return;

            return;
        }
        case 'X':   /*--------------------------------------------- Exit ---*/
        case 27:    /* Psion-Esc */
        case 'x':
	    DoExit(key == 'x');
	    return;

#ifdef HAVE_NFSSUPPORT
        case 'g': { /*------------------------------------------ Devices ---*/
            UWORD dev[5];
            INT   i;

            uOpenDialog (cmds[NCMDS-3]+1);
            for(i=0; i < NR_OF_DEVICES; i++)
	      {
		dev[i] = pm_devices[i] + 1;
		if(uAddChoiceList(dev_name[i], &dev[i], c_off, c_on, NULL))
		  return;
	      }
            if(uRunDialog() <= 0)
	      return;
            for(i=0; i<NR_OF_DEVICES; i++)
	      pm_devices[i] = dev[i] - 1;
            return;
        }
        case 't': { /*---------------------------------------- Nfsd support */
            UWORD  prt = pm_protocol+1;

            if (   uOpenDialog (cmds[NCMDS-2]+1)
                || uAddChoiceList("Support:",&prt,
		   "Terminal only", "Old p3nfsd", "Terminal + New p3nfsd", NULL)
                || uRunDialog() <= 0)
                return;
	    pm_protocol = prt-1;
            return;
        }
#endif

/* Serial line */
        case 'p': { /*--------------------------------------------- Port ---*/
            UWORD  baud     = pm_baud + 1;
            UWORD  databits = pm_databits + 1;
            UWORD  stopbits = pm_stopbits;
            UWORD  parity   = pm_parity + 1;
            UWORD  port     = pm_port + 1;
            UWORD  flow;
#define bl baudlist
#define pl paritylist
#define rl portlist
	    extern char *bl[], *pl[], *rl[];

	    flow = (pm_xonxoff ? 1 : (pm_nortscts ? 3 : 2));
            if (   uOpenDialog (cmds[5]+1)
                || uAddChoiceList ("Port", &port,
				rl[0],rl[1],rl[2],rl[3],rl[4], NULL)
                || uAddChoiceList ("Baud rate", &baud,
				bl[0],bl[1],bl[2],bl[3],bl[4],bl[5],bl[6],
				bl[7],bl[8],bl[9],bl[10],bl[11],bl[12],bl[13],
				bl[14],bl[15],bl[16],bl[17],bl[18],NULL)
                || uAddChoiceList ("Data bits",&databits,"5","6","7","8",NULL)
                || uAddChoiceList ("Stop bits", &stopbits, "1", "2", NULL)
                || uAddChoiceList ("Parity",&parity,
					pl[0],pl[1],pl[2],pl[3],pl[4],NULL)
                || uAddChoiceList ("Flowcontrol:",&flow,
		                "software","hardware","none", NULL)
                || uRunDialog() <= 0)
                return;

	    pm_baud     = baud-1;
	    pm_databits = databits-1;
	    pm_stopbits = stopbits;
	    pm_parity   = parity-1;

	    pm_xonxoff = pm_nortscts = pm_dodsr = pm_dodcd = 0;;
	    if(flow == 1)
	      pm_xonxoff = pm_nortscts = 1;
	    else if(flow == 3)
	      pm_nortscts = 1;
	    pm_dodtr = 1;
	    Conv2Ser();

	    if(pm_port != port-1)
	      OpenPort(port-1, pm_port);
	    else
	      SetSer();

            return;
        }
        case 'e': { /*------------------------------- Expert Handshaking ---*/
            UWORD  xon_xoff = pm_xonxoff + 1;
            UWORD  rts_cts  = pm_nortscts + 1;
            UWORD  dsr_dtr  = pm_dodsr + 1;
            UWORD  dcd      = pm_dodcd + 1;
            UWORD  dtr      = pm_dodtr + 1;
            UWORD  ignore   = pm_ignpar + 1;
            if (   uOpenDialog (cmds[6]+1)
                || uAddChoiceList ("Send Xon/Xoff",  &xon_xoff, c_off,c_on,NULL)
                || uAddChoiceList ("Ignore RTS/CTS", &rts_cts,  c_off,c_on,NULL)
                || uAddChoiceList ("Obey DSR/DTR",   &dsr_dtr,  c_off,c_on,NULL)
                || uAddChoiceList ("Obey DCD",       &dcd,      c_off,c_on,NULL)
                || uAddChoiceList ("Assert DTR",     &dtr,      c_off,c_on,NULL)
                || uAddChoiceList ("Ignore parity", &ignore,    c_no,c_yes,NULL)
                || uRunDialog() <= 0)
                return;

	    pm_xonxoff = xon_xoff-1;
	    pm_nortscts= rts_cts-1;
	    pm_dodsr   = dsr_dtr-1;
	    pm_dodcd   = dcd-1;
	    pm_dodtr   = dtr-1;
	    pm_ignpar  = ignore-1;
	    Conv2Ser();
	    SetSer();
            return;
        }
	case 'j': {  /*-------------------------------------Status line -----*/
	  /* dialog to change update frequency: timint */
	  H_DI_NUMBER num;
	  UWORD	show = pm_statusline ? 1 : 2;
	  UWORD clear = 2;

	  num.value= (LONG *)&timint;
	  num.low = 5;
	  num.high = 255;
	  if (   uOpenDialog(cmds[7]+1)
	      || uAddChoiceList ("Status line visible ", &show,c_yes,c_no,NULL)
	      || uAddChoiceList ("Reset statistics",    &clear,c_yes,c_no,NULL)
	      || uAddDialogItem (H_DIALOG_NUMBER, "Update freq.(0.1secs)", &num)
	      || uRunDialog() <= 0)
	    return;
	  
	  if(pm_statusline != 2 - show)
	    {
	      pm_statusline = 2 - show;
	      Resize();
	    }
	  pm_updfreq = timint;
	  if(clear == 1)
	    stats_read = stats_write = 0;
	  return;
	}
        case 'h':   /*--------------------------------------- Hangup --------*/
	  /* Lower DTR for half a second, then raise it again */

	  if(!serial)
	    return;
	  Sw_Serial(OFF);		/* otherwise panic with P_FSET! */
	  p_waitstat (&ttystat);

	  pm_pctrl[1] = pm_pctrl[2] = P_SRDTR_OFF;
	  Check(p_iow(serial,P_FCTRL,&pm_pctrl), c_dtr);

	  p_sleep((ULONG)5);				/* Half a second */

	  pm_pctrl[1] = pm_pctrl[2] = P_SRDTR_ON;
	  Check(p_iow(serial,P_FCTRL,&pm_pctrl), c_dtr);

	  pm_pctrl[1] = 0;
	  Sw_Serial(ON);
	  AddToHistory ('h');
	  return;

	case '+': {  /*-------------------------------------- Send a break --*/
	  /* We can send a break of 10/50sec = 200msec at most. Should be
	     at least 250msec, but it could be better than nothing */
          P_SRCHAR  tty;
	  unsigned char z = 0, i;

	  if(!serial)
	    return;
	  Sw_Serial(OFF);
	  p_iow (serial, P_FFLUSH);
	  p_waitstat (&ttystat);

	  tty = pm_tty;
	  tty.tbaud  = tty.rbaud = P_BAUD_50;
	  tty.frame  = P_DATA_8 | P_PARITY;		 /* This means 200ms */
	  tty.parity = P_PAR_EVEN;
	  tty.hand   = P_IGN_CTS;
	  Check(p_iow(serial, P_FSET, &tty), c_cfgserial);
	  for(i = 0; i < 6; i++)			/* little over 1 sec */
	    P_WRITE(serial, (char *)&z, 1);

	  Check(p_iow(serial,P_FSET,&pm_tty), c_cfgserial);
	  pm_pctrl[1] = 0x00;	/* or ModemUpdate will do it again */
	  p_iow (serial, P_FFLUSH);

	  Sw_Serial(ON);
	  AddToHistory ('b');
	  return;
	}

#ifdef HAVE_TERMEMU
	case 'f': {  /*------------------------------------ Choose font ---*/
            UWORD  font = pm_fontsize > 7 ? 2 : 1;
            UWORD  size = pm_fontsize - (font == 1 ? 3 : 7);

	    if(!_UseFullScreen)
	      return;
	    if(pm_fontsize == 12)
	      font = 3, size = 0;
            if (   uOpenDialog (cmds[10]+1)
                || uAddChoiceList ("Font", &font, "Roman","Swiss","Small",NULL)
                || uAddChoiceList ("Size", &size, "8", "11", "13", "16", NULL)
                || uRunDialog() <= 0)
                return;

	    font = (font == 3 ? 12 : ((font == 1 ? 3 : 7) + size));

	    if ( font != pm_fontsize )
	      {
		pm_fontsize = font;
		Resize();
	      }
	    return;
	  }
	case 'z':    /*------------------------------------- Zoom bigger ---*/
	    if(!_UseFullScreen)
	      return;
	    if(++pm_fontsize > 12)
	      pm_fontsize = 4;
	    Resize();
	    return;
	case 'Z':    /*------------------------------------- Zoom smaller ---*/
	    if(!_UseFullScreen)
	      return;
	    if(--pm_fontsize < 4)
	      pm_fontsize = 12;
	    Resize();
	    return;
	case 'c':    /*--------------------------------------- Copy mode ---*/
	    copying = 1;
	    if(DoCopy(0, 0))
	      copying = 0;
	    return;
	case 'i':    /*--------------------------------------- Paste -*/
	    DoPaste();
	    return;
	case 'b':    /*--------------------------------------- Bring -*/
	    DoBring();
	    return;
	case 'r':    /*--------------------------------------- Reset term --*/
	    Resize();
	    return;
	case 'd': {  /*--------------------------------------- Dial support */
	    H_DI_FSEL line1 = {pm_dialpgm, 0}; 
	    extern char c_filename[];
	    UWORD  ad  = 2-pm_autodial;

	    if(uOpenDialog (cmds[18]+1)
	    || uAddDialogItem (H_DIALOG_FSEL, c_filename, &line1)
            || uAddChoiceList ("Dial at startup", &ad, c_yes, c_no, NULL)
	    || uAddButtonList ("Set", 's', "Execute", W_KEY_RETURN, NULL)
	    || (ret = uRunDialog()) <= 0)
	      return;

	    pm_autodial  = 2-ad;

	    if(ret == W_KEY_RETURN)
	      (void)doexec(pm_dialpgm);
	    return;
	  }
	case 'k': {  /*--------------------------------------- Fn keys -----*/
	    if (uOpenDialog (cmds[17]+1)
		|| (uAddButtonList ("Define", 'd', "Remove", 'r', NULL))
		|| (ret = uRunDialog()) <= 0)
		    return;
	    if(ret == 'd' || ret == 'r')
	      {
		wInfoMsgCorner("Type the function key",W_CORNER_BOTTOM_RIGHT);
		fnkey = ret;
	      }
	    return;
	  }
	case 'q': {  /*---------------------------------------- Settings ---*/
            UWORD  cnv = 2-pm_isotoibm;
            UWORD  jsc = 2-pm_jumpscroll;
            UWORD  log = logfd ? 1 : 2;
	    UWORD  db  = 2-pm_debugterm;
	    UWORD  ml  = pm_maxlines - 23;
	    H_DI_NUMBER sb;
	    LONG   sbn = pm_scrollback;
	    
	    sb.value= &sbn;
	    sb.low  = 0;
	    sb.high = 800;

            if (   uOpenDialog (cmds[19]+1)
                || uAddChoiceList ("ISO8859-1 to IBM850", &cnv, c_on,c_off,NULL)
                || uAddChoiceList ("Jumpscroll",   &jsc, c_on,c_off,NULL)
                || uAddChoiceList ("Debugging",    &db,  c_on,c_off,NULL)
                || uAddChoiceList ("Log terminal output", &log,c_yes,c_no,NULL)
                || uAddChoiceList ("Max. number of rows", 
						&ml,"24","25","26", NULL)
	        || uAddDialogItem (H_DIALOG_NUMBER, "Scrollback", &sb)
                || uRunDialog() <= 0)
                return;

	    pm_isotoibm   = 2-cnv;
	    pm_jumpscroll = 2-jsc;
	    pm_debugterm  = 2-db;
	    pm_maxlines   = 23 + ml;
	    if((ml == 1 && rows == 26) || (ml == 2 && rows == 24))
	      Resize();
	    if(sbn != pm_scrollback)
	      SetScrollback((int)sbn);

	    log = 2 - log;

	    if((logfd && !log) || (!logfd && log)) /* Changed */
	      {
	        if(!log)
		  {
		    p_close(logfd);
		    logfd = 0;
		    wInfoMsgCorner("Logging terminated", W_CORNER_BOTTOM_RIGHT);
		  }
		else
		  {
		    extern char cant_open_the_file[];
		    H_DI_FSEL  line3={ buf, H_FILE_NEW_EDITOR };

		    *buf = 0;

		    if (   uOpenDialog ("Logging")
			|| uAddDialogItem (H_DIALOG_FSEL, "Logfile:", &line3)
			|| uRunDialog() <= 0)
		      return;

		    buf[*buf+1] = 0;
		    log = p_open(&logfd,buf+1,P_FSTREAM|P_FREPLACE|P_FUPDATE);
		    if(log)
		      {
			p_atos(mfb, "open %s", buf+1);
			Check(log, mfb);
		      }
		    else
		      wInfoMsgCorner("Logging started", W_CORNER_BOTTOM_RIGHT);
		  }
	      }
	    return;
	  }
	case 'T':
	  echoon = !echoon;
	  return;
	case 'u':  /*------------------------------------------ XYmodem ----*/
	case 'w':
	case 'y':  /*------------------------------------------ XYmodem ----*/
	  /* XYmodem commands are only allowed in terminal-only mode */
	  if(pm_protocol != 0)
	    {
		H_DI_TEXT  line1={buf+  0,TXT}, 
			   line2={buf+ 50,TXT};

		uZTStoBCS (line1.str, "Sorry, XYmodem commands are only");
		uZTStoBCS (line2.str, "allowed in the \"Terminal only\" mode");

		if (   uOpenDialog ("XYmodem commands:")
		    || uAddDialogItem (H_DIALOG_TEXT, NULL, &line1)
		    || uAddDialogItem (H_DIALOG_TEXT, NULL, &line2)
		    || (uAddButtonList ("",  W_KEY_RETURN, NULL))
		    || (ret = uRunDialog()) <= 0)
		    return;
		return;
	    }
	  DoXYmodem(key);
	  return;
#endif
    }
}


#ifdef HAVE_NFSSUPPORT
/*============================================================== Respond ===*/
VOID Respond(int rc) {

  if(pm_protocol == 1)
    {
      P_WRITE (serial, (char *)&rc, 1); /* Big Endian!!! */
    }
  else
    {
      mfb[0] = PREFIX;
      mfb[1] = rc;
      P_WRITE (serial, mfb, 2);
    }
}

/*=============================================================== DoWork ===*/
VOID DoWork (void) {

    struct  { LONG offset; UINT len; UBYTE req; } *parms;
    void    *file;
    char    *str;
    INT     rc, to_read;
    LONG    offset;
    P_INFO  p_info;
    UWORD   crc, crc2;
    WORD    len;
    LONG    roff = 0;		/* last read offset */

    /*-------------------------------------------------- read length&data ---*/

    len = sch;
    if(pm_protocol == 2)/* We are reading the length here for the new proto */
      {
	len = 0;
	P_READ (serial, &len, 1);	/* Big Endian!! */
      }
    P_READ (serial, buf, len);

    /*------------------ last (few) bytes contain request type and parms ---*/
    parms = (void *) (buf + len - sizeof(*parms));


    /*---------------- string is PASCAL string with C string termination ---*/
    str = buf+1;

    /*---------------------------------- piggyback statd code onto gattr ---*/
    if (parms->req == 0x0C) {
        AddToHistory ('f');
	Respond(0);
    }

    /*------------------------------------ last byte in frame is request ---*/
    switch (parms->req) {

    case 0x01: /*------------------------------------------------- creat ---*/
        rc = p_open (&file, str, P_FSTREAM|P_FREPLACE|P_FUPDATE);
        AddToHistory ((rc == 0) ? 'c' : 'C');
	if(!rc)
	  p_close (file);
	Respond(rc);
        return;

    case 0x02: /*------------------------------------------------- gattr ---*/
        rc = p_finfo (str, &p_info);
        AddToHistory ((rc == 0) ? 'g' : 'G');
        Respond(rc);
        if (rc) return;

        if (p_info.status & P_FADIR) {
    case 0x0C: /* statd */
            /* damned filesystem without linkcount :( */
            p_info.version = 0;
            p_scat (str, "\\");
            p_open (&file, str, P_FDIR);
            while(p_iow (file, P_FREAD, buf, (P_INFO*)mfb) != E_FILE_EOF)
              if(((P_INFO *)mfb)->status & P_FADIR)
	        p_info.version++;
            p_close (file);
        }
        P_WRITE (serial, (char *)&p_info, 12);
        return;

    case 0x03: /*--------------------------------------------------- mkd ---*/
        AddToHistory ('m');
        Respond(p_mkdir(str));
        return;

    case 0x04: /*-------------------------------------------------- read ---*/

        offset = parms->offset;
        len = parms->len;
	rc = 0;

	rc = p_open (&rfile,str,P_FOPEN|P_FSTREAM|P_FRANDOM|P_FSHARE);
        if(rc == 0) rc = p_finfo (str, &p_info);
        if(offset > p_info.size) offset = p_info.size;
        if(offset + len > p_info.size) len = p_info.size - offset;

        if(rc == 0 && offset != 0)
	  rc = p_seek (rfile, P_FABS, &offset);
	AddToHistory ((rc == 0) ? 'r' : 'R');
	Respond(rc);
        if(rc)
	  return;

	roff = offset;

        P_WRITE (serial, (char *)&p_info, 12);
	crc = 0;
        while(len > 0) {
            to_read = (len > sizeof(buf)) ? sizeof(buf) : len;
            to_read = p_read (rfile, buf, to_read);
	    len  -= to_read;
	    roff += to_read;

	    if(pm_protocol > 1)
	      p_crc(&crc, (UBYTE *)buf, (UINT)to_read);
            P_WRITE (serial, buf, to_read);
        }
	if(pm_protocol > 1)
	  P_WRITE (serial, (char *)&crc, 2);

	p_close (rfile);
        return;

    case 0x05: /*-------------------------------------------------- rdir ---*/
        AddToHistory ('l');
        Respond(0);
        p_open (&file, str, P_FDIR);
        while (p_iow (file, P_FREAD, buf, NULL) != E_FILE_EOF)
            P_WRITE (serial, buf, p_slen(buf)+1);
        p_close (file);
        P_WRITE(serial, &zero, 1);
        return;

    case 0x06: /*-------------------------------------------------- remv ---*/
        AddToHistory ('d');
        Respond(p_delete(str));
        return;

    case 0x07: /*-------------------------------------------------- renm ---*/
        AddToHistory ('n');
        len = ((unsigned char *)(&parms->len))[1];
        P_READ (serial, mfb, len);
        mfb[len]=0;
        rc = p_rename (str, mfb+1);
        Respond(rc);
        return;

    case 0x08: /*--------------------------------------------------- rmd ---*/
        AddToHistory ('z');
        Respond(p_delete(str));
        return;

    case 0x09: /*------------------------------------------------- sattr ---*/
        AddToHistory ('s');
        Respond(p_sfstat (str, parms->len,/*attr*/ 0x0F2F));
        return;

    case 0x0A: /*------------------------------------------------- write ---*/
        offset = parms->offset;
        len    = parms->len;

        rc = p_open (&file, str, P_FOPEN | P_FSTREAM | P_FUPDATE | P_FRANDOM);
        if(rc == 0 && offset > 0)
	  rc = p_seek (file, P_FABS, &offset);

	crc = 0;
        while (len) {
            to_read = (len > sizeof(buf)) ? sizeof(buf) : len;
            P_READ(serial, buf, to_read);
	    if(pm_protocol > 1)
	      p_crc(&crc, (UBYTE *)buf, (UINT)to_read);
	    if(rc == 0)
              rc = p_write (file, buf, to_read);
            len -= to_read;
        }
        p_close (file);

	if(pm_protocol > 1)
	  {
	    P_READ (serial, &crc2, 2);
	    if(crc != crc2)
	      {
		AddToHistory ('?');
		if(rc == 0)
		  rc = 1;
	      }
	  }

        Respond(rc);
        AddToHistory ((rc == 0) ? 'w' : 'W');
        return;

    case 0x0B: /*-------------------------------------------------- getd ---*/
        AddToHistory ('>');
        Respond(0);
        for(len=0; len<NR_OF_DEVICES; len++)
	  {
	    if(pm_devices[len] && p_dinfo(dev_name[len], (P_DINFO *)buf) == 0)
	      {
		P_WRITE (serial, dev_name[len], p_slen(dev_name[len])+1);
		P_WRITE (serial, buf, 14);
	      }
	  }
        P_WRITE (serial, &zero, 1);
        return;

#ifdef HAVE_TERMEMU
    case 0x0D: /*------------------------------------------------- ttydata -*/
	TtyEmu((UBYTE *)str, *(UBYTE *)buf);     /* No return this time */
	if(isnfsd == 0)
	  {
	    isnfsd = 1;
	    mfb[0] = 0;                         /* Resize event ... */
	    mfb[1] = rows;
	    mfb[2] = cols;
	    P_WRITE(serial, mfb, 3);
	  }
	return;
#endif
    case 0x0E: /*----------------------------------------------- RPC / Exec -*/
	rc = doexec(buf);
	AddToHistory ((rc == 0) ? 'e' : 'E');
	Respond(rc);
	return;
    case 0x0F: /*---------------------------------------------------- Echo -*/
	wInfoMsgCorner(str, W_CORNER_BOTTOM_RIGHT);
	Respond(0);
	return;

    default:   /*----------------------------------------------- default ---*/
        AddToHistory ('X');
        Respond(1);
        return;
    }
}
#endif /* HAVE_NFSSUPPORT */

/*================================================================= main ===*/
void main (void) {

    /*------------------------------------------- program initialization ---*/
    stats_read = stats_write = 0;
    ScreenInitialize ();

#ifdef HAVE_TERMEMU
    /*------------------------------------------------------ Bring services */
    DoRegister(0);
#endif
    /*---------------------------------------------- Help initialization ---*/
    hInitAppRcb();

    /*------------------------------------------------- open serial port ---*/
    OpenPort(pm_port, -1);

    if(pm_statusline)
      UpdateModem();

    Check(p_open(&timH,"TIM:",-1), "open timer");
    QueueTimer(timint);
    uGetKeyA (&keystat, &key);

    if(pm_autodial)
      (void)doexec(pm_dialpgm);
    /*-------------------------------------------------------- main loop ---*/
    for (;;) {

        /*--------------------------------- wait for something to happen ---*/
        p_iowait();

        /*------------------------------------------- was a key pressed? ---*/
        if (keystat != E_FILE_PENDING) {
            ParseKey (key.keycode, key.modifiers);
            uGetKeyA (&keystat, &key);
            continue;
        }

#ifdef HAVE_TERMEMU
        /*---------------------------------------------- Message for us? ---*/
	if(mstat != E_FILE_PENDING)
	  DoSrvBring();
#endif

        /*------------------------------ did the serial port talk to us? ---*/
        if (ttystat != E_FILE_PENDING && !copying) {
	    stats_read++;
#ifdef HAVE_NFSSUPPORT
	    if(pm_protocol == 1 || (pm_protocol == 2 && sch == PREFIX))
	      DoWork ();
	    else
#endif
	      {
#ifdef HAVE_TERMEMU
		/* 
		   Problem: bytewise reading and screen update is too slow.
		   Let's try to read more if there is available. The problem
		   is that there may be a protocol 2 command in the data.
		   Therefore this is restricted to the "terminal only" mode.
		 */
		if(pm_protocol == 0)
		  {
		    *buf = sch;
		    p_iow(serial, P_FTEST, &one);
		    if(one > 0) /* hmmm */
		      {
			if(one > 255) one = 255;
			p_iow (serial, P_FREAD, buf+1, &one);
		      }
		    one++;   /* the first byte is already read before */
		    TtyEmu((unsigned char *)buf, one);
		  }
		else
		  TtyEmu(&sch, 1);
#endif
	      }

            ShowStats ();
	    Sw_Serial(ON);
            continue;
        }

        /*------------------------------------------ time for a timeout? ---*/
	if (timstat != E_FILE_PENDING)
	  {
	    UpdateModem();
            AddToHistory (0);
	    wFlush();
	    QueueTimer(timint);
	  }
    }
}
