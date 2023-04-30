/*
 * Copyright 1993-1996 Rudolf Koenig, Michael Schroeder, Juergen Weigert
 * see the GNU Public License V2 for details
 *
 * In this file lines end with CR/LF, as our psion C-compiler lives in the 
 * DOS-WORLD
 */

#include <plib.h> 
#include <wlib.h> 
#include <p_serial.h>
#include "nfsc.h"

#define DEFAULT_NFSDAEMON

static char c_none[] = "none",
	    c_small[] ="small";

char *portlist[] = {c_none, "TTY:A", "TTY:B", "TTY:I", "TTY:SRX", 0};
char *dev_name[] = {"LOC::M:", "LOC::A:", "LOC::B:", "LOC::C:", "ROM::", 0};
char *baudlist[] = {	  "50",  "75", "110",  "134",  "150",  "300",
			 "600","1200","1800", "2000", "2400", "3600",
			"4800","7200","9600","19200","38400","57600",
			"115200", 0};
char *databitlist[] = {	"5", "6", "7", "8", 0 };
char *paritylist[] = {	c_none, "even", "odd", "mark", "space", 0 };
char *clocklist[] = {	c_none, "big", c_small, 0 };
char *fontlist[] = {	"roman", "swiss", c_small, 0 };
char *fsizelist[] = {	"8", "11", "13", "16", 0 };
char *xylist[] = {	"-", "xmodem", "xmodemcrc", "xmodemcrc1k", "ymodem",
			"ymodem1k", "ymodemg", "ymodemg1k", 0};
char *protolist[] = {	"terminal", "old", "new", 0, };
char cansave = 1;

static int GetDevs(char *, int, int *);
static int DoFnKeys(char *, int, int *);
extern char cant_open_the_file[];

unsigned char
	pm_baud		= P_BAUD_19200-1,
	pm_databits	= P_DATA_8,
	pm_stopbits	= 1,
	pm_port		= 1,
	pm_parity	= 0,
	pm_xonxoff	= 0,
	pm_nortscts	= 0,
	pm_dodsr	= 0,
	pm_dodcd	= 0,
	pm_dodtr	= 1,
	pm_ignpar	= 1,
	pm_statusline	= 0,
	pm_clockwin	= 0,
#ifdef DEFAULT_NFSDAEMON
	pm_protocol	= 2,
#else
	pm_protocol	= 0,
#endif
	pm_jumpscroll	= 1,
	pm_debugterm	= 0,
	pm_font		= 0,
	pm_fontsize	= 0,
	pm_isotoibm	= 1,
	pm_xymodem	= 4,
	pm_maxlines	= 25,
	pm_autodial	= 0,
	pm_updfreq	= 20;
WORD	pm_scrollback	= 100;
char    pm_xyname[129]	= "X\\opd\\xmodem.out";
char    pm_dialpgm[129]	= "X\\opo\\dialme.opo";
char    pm_devices[NR_OF_DEVICES] = { 1, 1, 1, 1, 1};


struct parameters pm[] = {
	{"port",		4,	&pm_port,	portlist},
	{"baudrate",		4,	&pm_baud,	baudlist},
	{"databits",		4,	&pm_databits,	databitlist},
	{"stopbits",		1,	&pm_stopbits,	0},
	{"parity",		4,	&pm_parity,	paritylist},
	{"sendxonxoff",		3,	&pm_xonxoff,	0},
	{"ignorertscts",	3,	&pm_nortscts,	0},
	{"obeydsr",		3,	&pm_dodsr,	0},
	{"obeydcd",		3,	&pm_dodcd,	0},
	{"assertdtr",		3,	&pm_dodtr,	0},
	{"ignoreparity",	3,	&pm_ignpar,	0},
	{"device",		2,	(unsigned char *)GetDevs,	0},
	{"statusline",		3,	&pm_statusline,	0},
	{"clockwindow",		4,	&pm_clockwin,	clocklist},
	{"protocol",		4,	&pm_protocol,	protolist},
	{"jumpscroll",		3,	&pm_jumpscroll,	0},
	{"debugterm",		3,	&pm_debugterm,	0},
	{"font",		4,	&pm_font,	fontlist},
	{"fontsize",		4,	&pm_fontsize,	fsizelist},
	{"xymodem",		4,	&pm_xymodem,	xylist},
	{"isotoibm",		3,	&pm_isotoibm,	0},
	{"maxlines",		1,	&pm_maxlines,	0},
	{"autodial",		3,	&pm_autodial,	0},
	{"fnkey",		2,	(unsigned char *)DoFnKeys,	0},
	{"dialpgm",		0,	(unsigned char *)pm_dialpgm+1,	0},
	{"xymodemout",		0,	(unsigned char *)pm_xyname+1,	0},
	{"scrollback",		5,	(unsigned char *)&pm_scrollback,0},
	{0,			0,	0,		0}
};

static int 
GetDevs(buf, load, which)
  char *buf;
  int load, *which;
{
  if(load)
    {
      if(*which == 0)
	for(load = 0;dev_name[load]; load++)
	  pm_devices[load] = 0;

      (*which)++;
      for(load = 0;dev_name[load]; load++)
        if(p_scmpi(dev_name[load], buf) == 0)
	  {
	    pm_devices[load] = 1;
	    return 0;
	  }
      return 1;
    }
  else
    {
      for(load = *which; dev_name[load] != 0; load++)
        if(pm_devices[load])
	  {
	    p_scpy(buf, dev_name[load]);
	    *which = load+1;
	    return 1;
	  }
      return 0;
    }
}

static int 
DoFnKeys(buf, load, which)
  char *buf;
  int load, *which;
{
#ifdef HAVE_TERMEMU
  extern struct fkey *fk;
  static struct fkey *f = (struct fkey *)-1;
  UWORD key, modifier;
  char *p;
  int len;

  if(load)
    {
      len = p_slen(buf);
      p = buf;   p_stog(&p, &key, 16);
      p = buf+5; p_stog(&p, &modifier, 16);
      p = buf+7; *p = len - 8;
      AddFnKey(key, modifier, p);
      return 0;
    }
  else
    {
      if(f == (struct fkey *)-1)
        f = fk;
      if(f == 0)
        {
	  f = (struct fkey *)-1;
	  return 0;
	}
      p_atos(buf, "%04x %02x %s", f->key, f->modifier, (char *)f+sizeof(*f));
      f = f->next;
      return 1;
    }
#endif
  return 0;
}

#define BLEN 256
void
InitParams()
{
  extern ULONG timint;

  timint = pm_updfreq;
  if(pm_font == 0) pm_fontsize += 4;
  if(pm_font == 1) pm_fontsize += 8;
  if(pm_font == 2) pm_fontsize = 12;
  *pm_xyname = p_slen(pm_xyname+1);
  *pm_dialpgm = p_slen(pm_dialpgm+1);
#ifdef HAVE_TERMEMU
  SetScrollback(pm_scrollback);
#endif
  Conv2Ser();
}
/*
 * Note: LoadParams and SaveParams require an OPL string
 */
int
LoadParams(fname, warn)
  char *fname;
  int warn;
{
  VOID *fcb;
  WORD num;
  char *p, *p2;
  char **ps;
  int len, i, j, line, which = 0;

  fname[*fname+1] = 0;
  if(p_open(&fcb, fname+1, P_FTEXT | P_FSHARE))
    {
      if(warn)
	wInfoMsgCorner(cant_open_the_file, W_CORNER_BOTTOM_RIGHT);
      return 1;
    }
  line = 0;
  while((len = p_read(fcb, mfb, BLEN-1)) > 0)
    {
      line++;
      if(*mfb == '#' || *mfb == 0)	/* Allow for comments */
        continue;

      /* Chop the line into parameter-name and argument */
      mfb[len] = 0;
      while(mfb[len-1] == '\n' || mfb[len-1] == '\r')
        mfb[--len] = 0;
      p = p_skipch(mfb);
      if(*p) *p++ = 0;
      if(*p) p = p_skipwh(p);
      
      if(p_scmpi(mfb, "include") == 0)
        {
	  cansave = 0;
	  *(p-1) = p_slen(p);
	  if(LoadParams(p-1, warn))
	    return 1;
	  continue;
	}

      for(i = 0; pm[i].name; i++)
        if(p_scmpi(pm[i].name, mfb) == 0)
	  break;
      if(pm[i].name == 0 || *p == 0)
	break;
      
      switch(pm[i].type)
        {
	  case 0:
	    p_scpy((char *)pm[i].value, p);
	    break;
	  case 1:
	    p2 = p;
	    p_stoi(&p2, &num);
	    *pm[i].value = num;
	    break;
	  case 2:
	    if(((int (*)(char *, int, int *))pm[i].value)(p, 1, &which))
	      len = -1;
	    break;
	  case 3:
	    if(!p_scmpi(p, "on") || !p_scmpi(p, "yes"))
	      *pm[i].value = 1;
	    else
	      *pm[i].value = 0;
	    break;
	  case 4:
	    ps = (char **)pm[i].arg;
	    for(j = 0; *ps[j] != 0; j++)
	      if(!p_scmpi(ps[j], p))
	        {
		  *pm[i].value = j;
		  break;
		}
	    if(*ps[j] == 0)
	      len = -1;
	    break;
	  case 5:
	    p2 = p;
	    p_stoi(&p2, (WORD *)pm[i].value);
	    break;
	}
      if(len < 0)
        {
	  *p = 0;
	  break;
	}
    }
  p_close(fcb);

  if(warn)
    {
      if(pm[i].name == 0 || *p == 0)
	{
	  p_atos(mfb, "Bogus %s in line %d", *p == 0 ? "value" : "name", line);
	  wInfoMsgCorner(mfb, W_CORNER_BOTTOM_RIGHT);
	  p_sleep(20);
	  return 1;
	}
      else
	wInfoMsgCorner("Loaded", W_CORNER_BOTTOM_RIGHT);
    }

  InitParams();
  return 0;
}

int
SaveParams(fname, warn)
  char *fname;
  int warn;
{
  VOID *fcb;
  char **ps, *p;
  int i, which = 0;

  if(pm_fontsize == 12) 
    pm_font = 2, pm_fontsize = 0;
  else if(pm_fontsize >= 8)
    pm_font = 1, pm_fontsize -= 8;
  else if(pm_fontsize >= 4)
    pm_font = 0, pm_fontsize -= 4;
  else
    pm_font = 0, pm_fontsize = 0;

  fname[*fname+1] = 0;
  if(p_open(&fcb, fname+1, P_FTEXT | P_FREPLACE | P_FUPDATE))
    {
      if(warn)
	wInfoMsgCorner(cant_open_the_file, W_CORNER_BOTTOM_RIGHT);
      return;
    }
  p_atos(mfb, "# Do not append spaces at the end of line");
  p_write(fcb, mfb, p_slen(mfb));
  for(i = 0; pm[i].name; i++)
    {
      p_atos(mfb, "%s\t%s", pm[i].name, p_slen(pm[i].name) < 8 ? "\t" : "");
      p = mfb + p_slen(mfb);
      switch(pm[i].type)
        {
	  case 0:
      	    p_scpy(p, (char *)pm[i].value);
	    break;
	  case 1:
      	    p_atos(p, "%d", (WORD)(*pm[i].value));
	    break;
	  case 2:
	    if(((int (*)(char *, int, int *))pm[i].value)(p, 0, &which))
	      i--;
	    else
	      continue;
	    break;
	  case 3:
	    p_scpy(p, *pm[i].value ? "on" : "off");
	    break;
	  case 4:
	    ps = (char **)pm[i].arg;
	    p_scpy(p, ps[*pm[i].value]);
	    break;
	  case 5:
      	    p_atos(p, "%d", *(WORD *)pm[i].value);
	    break;
	}
      p_write(fcb, mfb, p_slen(mfb));
    }
  p_close(fcb);
  if(warn)
    wInfoMsgCorner("Saved", W_CORNER_BOTTOM_RIGHT);
  return 0;
}
