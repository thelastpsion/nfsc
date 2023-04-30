/*
 * Copyright 1993-1996 Rudolf Koenig, Michael Schroeder, Juergen Weigert
 * see the GNU Public License V2 for details
 *
 * In this file lines end with CR/LF, as our psion C-compiler lives in the 
 * DOS-WORLD
 */

#include <plib.h> 
#include <p_xmodem.h>
#include <p_serial.h>
#include <hwif.h>
#include "nfsc.h"

#ifdef HAVE_TERMEMU
TEXT *XYEnv="NFSC_XY";


static void 
xy_configure(void)
{
  UWORD  xymodem = pm_xymodem;

  if (   uOpenDialog ("Choose the protocol")
      || uAddChoiceList ("Protocol: ", &xymodem, 
			 "Xmodem, 1 Byte Checksum",
			 "Xmodem, 2 Byte CRC",
			 "Xmodem, 2 Byte CRC (1K)",
			 "Ymodem, 2 Byte CRC",
			 "Ymodem, 2 Byte CRC (1K)",
			 "Ymodem-G, 2 Byte CRC",
			 "Ymodem-G, 2 Byte CRC (1K)", NULL)
      || uRunDialog() <= 0)
      return;

  pm_xymodem = xymodem;
  return;
}

static UWORD
setmode(void)
{
  UWORD ret = 0;

  switch(pm_xymodem)
    {
    case XY_XMODEM:
      ret = P_XMDM_CHECKSUMMODE;
      break;
    case XY_XMODEMCRC:
      ret = P_XMDM_CRCMODE;
      break;
    case XY_XMODEMCRC1K:
      ret = P_XMDM_CRCMODE | P_XMDM_ONE_K;
      break;
    case XY_YMODEM:
      ret = P_YMODEM_MODE;
      break;
    case XY_YMODEM1K:
      ret = P_YMODEM_MODE | P_XMDM_ONE_K;
      break;
    case XY_YMODEMG:
      ret = P_YMODEM_G_MODE;
      break;
    case XY_YMODEMG1K:
      ret = P_YMODEM_G_MODE | P_XMDM_ONE_K;
      break;
    }
  return ret;
}

static void *fp;
static char *buf;

static void
errmsg(char *txt)
{
  wInfoMsgCorner(txt, W_CORNER_BOTTOM_RIGHT);
  if(buf) p_free(buf);
  if(fp)  p_close(fp);
  p_close(serial);
  Sw_Serial(ON);
}

static char c_xmd[]      = "XMD:";
static char c_cancel[]   = "Cancel";
static char c_cantopen[] = "Can't open XMD:";
static char c_aborting[] = "XYmodem: Aborting";
static char c_done[]     = "XYmodem: Done";
static char c_timedout[] = "Connection timed out";
char c_filename[] = "Filename: ";
char c_nomem[] = "Not enough memory";

static void
xy_sendfile(void)
{
  H_DI_FSEL line1 = {pm_xyname, 0}; 
  WORD len, l, ret;
  UWORD type, mode;
  P_INFO p_info;
  char *s;

  if (   uOpenDialog ("Select file to send")
      || uAddDialogItem (H_DIALOG_FSEL, c_filename, &line1)
      || (uAddButtonList (c_cancel,W_KEY_ESCAPE,"Send file",W_KEY_RETURN,NULL))
      || uRunDialog() <= 0)
         return;

  /* ---------------------------------------- Let's open the XYmodem device */
  Sw_Serial(OFF); 			/* Cancel outstanding events */
  if(p_open(&serial, c_xmd, -1))
    {
      wInfoMsgCorner(c_cantopen, W_CORNER_BOTTOM_RIGHT);
      Sw_Serial(ON); /* Re-enable input */
      return;
    }

  type = P_XMDM_ACCP;
  mode = setmode();
  len = (mode & P_XMDM_ONE_K) ? 1024 : 128;
  buf = 0;
  fp = 0;

  if(p_open(&fp, pm_xyname+1, P_FOPEN|P_FSTREAM|P_FSHARE))
    {
      extern char cant_open_the_file[];
      errmsg(cant_open_the_file);
      return;
    }
  if(!(buf = p_alloc(len)))
    {
      errmsg(c_nomem);
      return;
    }

  AddToHistory('>');
  if(p_iow(serial, P_FCONNECT, &type, &mode))
    {
      errmsg(c_timedout);
      return;
    }
  AddToHistory('s');

  /* ---------------------------------------------- Transmit the whole file */
  ret = 0;
  if(pm_xymodem >= XY_YMODEM) /* Ymode protocol must send the filename */
    {
      /* Transmit only the filename, not the whole path */
      if((l = p_slocr(pm_xyname+1, '\\')) >= 0 ||
         (l = p_slocr(pm_xyname+1, ':' )) >= 0)
        l++;
      else
        l = 0;
        
      /* Filename + length(decimal) + date(octal) */
      p_bfil(buf, 128, 0);
      p_finfo(pm_xyname+1, &p_info);
      s = p_scpy(buf, pm_xyname+1+l) + 1;
      s += p_gltob(s, p_info.size, 10);
      *s++ = ' ';
      s += p_gltob(s, p_info.modst, 8);

      l = 128;
      ret = p_iow(serial, P_FWRITE, buf, &l);
      AddToHistory(ret ? 'F' : 'f');
    }

  l = 0;
  while(ret == 0 && (l = p_read(fp, buf, len)) > 0)
    {
      ret = p_iow(serial, P_FWRITE, buf, &l);
      AddToHistory(ret ? 'S' : 's');
    }

  if(!ret && (!l || l == E_FILE_EOF))  /* Send EOT */
    {
      l = 0;
      ret = p_iow(serial, P_FWRITE, buf, &l);
      if(pm_xymodem >= XY_YMODEM)
	p_iow(serial, P_FCONNECT, &type, &mode);
    }


  if(l)
    AddToHistory(l < 0 ? 'L' : 'l');
  else
    AddToHistory(ret ? 'C' : 'c');

  /* --------------------------------------------- Close the XYmodem device */
  p_iow(serial, P_FDISCONNECT);
  errmsg((ret || l) ? c_aborting : c_done);
}

static void
xy_receivefile(void)
{
  H_DI_FSEL line1 = {pm_xyname, H_FILE_NEW_EDITOR}; 
  WORD len, l, ret;
  UWORD type, mode;

  mode = setmode();
  len = (mode & P_XMDM_ONE_K) ? 1024 : 128;

  /*
  p_bfil(pm_xyname, sizeof(pm_xyname), 0);
  */
  if(mode >= XY_YMODEM)
    line1.flags = H_FILE_JUST_DIRS;
  if (   uOpenDialog ("XYmodem receive")
      || uAddDialogItem (H_DIALOG_FSEL, 
                         mode >= XY_YMODEM? "Directory: ":c_filename, &line1)
      || (uAddButtonList (c_cancel,W_KEY_ESCAPE,"Receive",W_KEY_RETURN,NULL))
      || uRunDialog() <= 0)
         return;

  /* ---------------------------------------- Let's open the XYmodem device */
  Sw_Serial(OFF); 			/* Cancel outstanding events */
  if(p_open(&serial, c_xmd, -1))
    {
      wInfoMsgCorner(c_cantopen, W_CORNER_BOTTOM_RIGHT);
      Sw_Serial(ON); /* Re-enable input */
      return;
    }

  type = P_XMDM_INIT;
  buf = 0;
  fp = 0;

  if(!(buf = p_alloc(len)))
    {
      errmsg(c_nomem);
      return;
    }

  for(;;)
    {
      AddToHistory('>');
      if(p_iow(serial, P_FCONNECT, &type, &mode))
	{
	  errmsg(c_timedout);
	  return;
	}
      if(pm_xymodem >= XY_YMODEM) /* Ymodem: the first frame is filename */
	{
	  l = len;
	  if((ret = p_iow(serial, P_FREAD, buf, &l)) != 0)
	    {
	      errmsg(c_timedout);
	      return;
	    }
	  if(p_slen(buf) == 0)
	    {
	      p_iow(serial, P_FDISCONNECT);
	      errmsg(c_done);
	      return;
	    }
	  if((l = p_slocr(buf, '\\')) >= 0 || (l = p_slocr(buf, '/')) >= 0)
	    l++;
	  else
	    l = 0;
	  p_scpy(mfb, pm_xyname+1);
	  p_scat(mfb, buf+l);
	}
      else
	p_scpy(mfb, pm_xyname+1);

      if(p_open(&fp, mfb, P_FUPDATE|P_FSTREAM|P_FREPLACE))
	{
	  p_iow(serial, P_FDISCONNECT);
	  /*
	  errmsg("Can't write the file");
	  */
	  errmsg(mfb);
	  return;
	}
      AddToHistory('f');

      /* ------------------------------------------ Transmit the whole file */
      ret = 0;
      while(ret == 0)
	{
	  l = len;
	  if((ret = p_iow(serial, P_FREAD, buf, &l)) != 0)
	    break;
	  ret = p_write(fp, buf, l);
	  AddToHistory(ret ? 'R' : 'r');
	}
      p_close(fp); fp = 0;
      if(pm_xymodem < XY_YMODEM)
        break;
      if(ret != E_FILE_EOF)
        break;
    }

  AddToHistory(ret == E_FILE_EOF ? 'c' : 'C');

  /* --------------------------------------------- Close the XYmodem device */
  p_iow(serial, P_FDISCONNECT);
  errmsg((ret || l) ? c_aborting : c_done);
}

void 
DoXYmodem(char key)
{
  if(key == 'u')
    xy_configure();
  if(key == 'w')
    xy_sendfile();
  if(key == 'y')
    xy_receivefile();
}
#endif /* HAVE_TERMEMU */
