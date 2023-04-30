/*
 * Copyright 1993-1996 Rudolf Koenig, Michael Schroeder, Juergen Weigert
 * see the GNU Public License V2 for details
 *
 * In this file lines end with CR/LF, as our psion C-compiler lives in the 
 * DOS-WORLD
 */

/* This module was inspired by the link-paste example from psion */
/* Thanks to Colly for his modules (BTW Colly who?) */ 

#include <plib.h>
#include <p_serial.h>
#include <nfsc.h>
#ifdef HAVE_TERMEMU
#include <hwif.h>
#include "lp.h"

static MESS *pM;
static HANDLE clpid = 0;
static int clwhere;

static char c_nodata[] = "No suitable data";
HANDLE sendtopid = 0;
char  *sendtoaddr= 0;

void
DoBring()
{
  HANDLE srvPid;
  ULONG  fmt, *pfmt;
  UWORD	 args[2];
  int    ret;

  pfmt = &fmt;
  srvPid = p_msendreceivew(p_pidfind("SYS$WSRV.*"), SY_LINK_PASTE, &pfmt);
  if(srvPid <= 0 || !(fmt & DF_LINK_TEXT))
    {
      wInfoMsgCorner(c_nodata, W_CORNER_BOTTOM_RIGHT);
      return;
    }

  args[0] = DF_LINK_TEXT_VAL;
  args[1] = 0;
  if((ret = p_msendreceivew(srvPid, TY_LINKSV_STEP, args)) == 0)
    {
      for(;;)
        {
	  args[0] = (UWORD)mfb;
	  args[1] = 255;
	  if((ret = p_msendreceivew(srvPid, TY_LINKSV_STEP, args)) <= 0)
	    break;
	  mfb[ret++] = '\n';
	  P_WRITE(serial, mfb, ret);
	}
      if (ret == E_FILE_EOF)
        ret = 0;
    }
}

void
DoRegister(int state)
{
  ULONG  fmt;

  if(state == 0)
    {
      clpid = 0;
      Check(p_minit(4, sizeof(MESS)-sizeof(E_MESSAGE)), "Initialise messaging");
      p_mreceive(&mstat, &pM);
    }
  fmt = DF_LINK_TEXT;
  p_msendreceivew(p_pidfind("SYS$WSRV.*"), SY_LINK_SERVER, &fmt);
}

static int
getaline(int where, int *next, int buflen)
{
  int ret, line, start;

  /* Compute the length of the string to copy */
  line = where / MAXCOLS;
  start = line * MAXCOLS;
  ret = getsbll(line) - (where - start);

  line++;
  if(line > pm_scrollback)
    line -= pm_scrollback;
  *next = line * MAXCOLS;

  if(start <= sb_end && start + MAXCOLS > sb_end)
    {
      *next = sb_end;
      if(sb_end - where < ret)
	ret = sb_end - where;
    }
  if(ret > buflen)
    {
      ret = buflen;
      *next = where + ret;
    }
  return ret;
}

void
DoPaste()
{
  int ret, next, where;

  if(sb_beg == -1)
    {
      wInfoMsgCorner(c_nodata, W_CORNER_BOTTOM_RIGHT);
      return;
    }
  where = sb_beg;
  while(where != sb_end)
    {
      ret = getaline(where, &next, 128);
      P_WRITE(serial, (char *)sb_buf+where, ret);
      where = next;
    }
}

void
DoSrvBring(void)
{
  int ret, next;

  ret=E_GEN_NSUP;
  switch(pM->m.type)
    {
      case TY_LINKSV_STEP:
	if(clpid != pM->m.pid)
	  {
	    if(clpid != 0)
	      p_logoff(clpid, TY_LINKSV_DEATH);
	    if(pM->arg1 == DF_LINK_TEXT_VAL)
	      {
		clpid = pM->m.pid;
		p_logon(clpid, TY_LINKSV_DEATH);
		clwhere = sb_beg;
		ret = 0;
	      }
	  }
	else if(clwhere == sb_end || sb_beg == -1 || !pM->arg1 || !pM->arg2)
	  {
	    p_logoff(clpid,TY_LINKSV_DEATH);
	    clpid = 0;
	    ret=E_FILE_EOF;
	  }
	else
	  {
	    /* Compute the length of the string to copy */
	    ret = getaline(clwhere, &next, pM->arg2);
	    p_pcpyto(clpid, (VOID*)pM->arg1, (VOID*)(sb_buf+clwhere), ret);
	    clwhere = next;
	  }
        break;
      case TY_LINKSV_DEATH:
	clpid = 0;
	ret = 0;
        break;

  /* The next 3 types are for OPL support */

      case 0x40: /* opl sent us sumething */

	p_pcpyfr(pM->m.pid, (char *)pM->arg1, mfb, 1);
	p_pcpyfr(pM->m.pid, (char *)pM->arg1+1, mfb+1, *(unsigned char *)mfb);

	/* Disable sending the data, as this would lock up nfsc & the OPL */
	next = sendtopid; sendtopid = 0;
	P_WRITE(serial, (char *)mfb+1, *(unsigned char *)mfb);
	wInfoMsgCorner("Dialstring", W_CORNER_BOTTOM_RIGHT);
	sendtopid = next;
	break;

      case 0x41: /* opl waits for a string */
	sendtopid = pM->m.pid;
	sendtoaddr= (char *)pM->arg1;
	SendModem();
	break;

      case 0x42: /* opl wants to exec menu entry */
	p_mfree(pM,ret);		/* Exit Handling */
	p_mreceive(&mstat,&pM);
	ParseKey(W_SPECIAL_KEY | pM->arg1, 0);
	return;

    }

  p_mfree(pM,ret);
  p_mreceive(&mstat,&pM);
}
#endif
