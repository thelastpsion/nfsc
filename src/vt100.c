/*
 * Copyright 1993-1996 Rudolf Koenig, Michael Schroeder, Juergen Weigert
 * see the GNU Public License V2 for details
 *
 * In this file lines end with CR/LF, as our psion C-compiler lives in the 
 * DOS-WORLD
 */

/*
 * vt100 emulator for p3nfs
 * Rudolf Koenig, Michael Schroeder
 * 5.1.1996
 * Copyright GNU Public License
 */

#include <p_serial.h>		/* P_BAUD */
#include <plib.h>		/* P_FTEXT & co */
#include <wlib.h>		/* WMSG_KEY */
#include "nfsc.h"		/* pm_ & co */

#ifdef HAVE_TERMEMU

extern char c_nomem[];

W_CURSOR wc;

#define MAXARGS 10

static WORD cx, cy,		/* Cursor position */
	    sx, sy,		/* Saved cursor position */
	    sr_u, sr_l;		/* scroll region upper and lower in character */
static char sbuf[MAXCOLS], bufoff, isbold;	/* Speedup buffer */
static UINT clearmode;

unsigned char
	*sb_buf = 0,		/* Scrollback buffer */
	*sb_nl;			
int   	sb_beg = -1,		/* Start of selected area */
	sb_end;			/* End of selected area */
static int 	sb_top,		/* Top of current page */
		l_beg, l_end;

#define E_NORMAL	255
#define E_ESCAPE	254

#define E_WRAP_MODE	0x01
#define E_ORIGIN_MODE	0x02
#define E_INSERT_MODE 	0x04
#define E_ISINVERTED	0x08

static unsigned char
	e_args[MAXARGS],	/* Escape seq. arguments */
	e_state = E_NORMAL,	/* escape state machine */
	e_interm,		/* Intermediate character (?*..) */
	e_flags,		/* WRAP_MODE & co */
	e_gstyle,		/* Bold * co */
	tabs[MAXCOLS + 1];

static unsigned char altchar[0x20] = {
  0x20,
  0x04,0xb1,0x12,0x12,0x12,0x12,0xf8,0xf1,
  0x12,0x12,0xd9,0xbf,0xda,0xc0,0xc5,0xc4,
  0xc4,0xc4,0xc4,0xc4,0xc3,0xb4,0xc1,0xc2,
  0xb3,0x11,0x10,0xf4,0xce,0x9c,0xfa
};
static unsigned char gl, font, charsets[4];

GLREF_C VOID Reset(void)
{
  int i;
  G_GC gc;

  for(i = 0; i < MAXCOLS; i++) /* Reset the tabs */
    tabs[i] = i && (i & 7) == 0;
  cx = cy = 0;
  sx = sy = 0;
  sr_u = 0;
  sr_l = rows;

  bufoff  = 0;
  e_state = E_NORMAL;
  e_flags = E_WRAP_MODE;
  clearmode = G_TRMODE_CLR;
  cur_appl = 0;
  gl = 0;
  font = charsets[0] = charsets[1] = charsets[2] = charsets[3] = 'B';

  isbold  = 0;
  e_gstyle = G_STY_MONO;	/* monospaced fonts, please */

  gc.style = e_gstyle;
  gSetGC(hgc, G_GC_MASK_STYLE, &gc);

  wc.width  = charwidth;	/* Cursor */
  wc.height = charheight;
  wc.ascent = ascent;
  wc.pos.x  = cx * charwidth;
  wc.pos.y  = cy * charheight + ascent;
  wTextCursor(main_win, &wc);
}

#define C_EOD	0
#define C_BOD	1
#define C_D	2
#define C_EOL	3
#define C_BOL	4
#define C_L	5

static void
clear(int n)
{
  P_RECT pr;

  switch(n)
    {
      /*----------------------------------------------- clear to end of page */
      case C_EOD:
        if(cy + 1 < rows) /* clear but the line itself */
	  {
	    pr.tl.x = 0;
	    pr.tl.y = (cy + 1) * charheight;
	    pr.br.x = t_w;
	    pr.br.y = t_h;
	    gClrRect(&pr, clearmode);
	  }
      /*------------------------------------------------------- clear to eol */
      case C_EOL:
        pr.tl.x = cx * charwidth;
        pr.tl.y = cy * charheight;
	pr.br.x = t_w;
	pr.br.y = pr.tl.y + charheight;
	gClrRect(&pr, clearmode);
	break;
      /*------------------------------------- clear to beginning of the page */
      case C_BOD:
        if(cy > 0) /* clear but the line itself */
	  {
	    pr.tl.x = 0;
	    pr.tl.y = 0;
	    pr.br.x = t_w;
	    pr.br.y = cy * charheight;
	    gClrRect(&pr, clearmode);
	  }
      /*------------------------------------- clear to beginning of the line */
      case C_BOL:
	if(cx > 0)
	  {
	    pr.tl.x = 0;
	    pr.tl.y = cy * charheight;
	    pr.br.x = (cx + 1) * charwidth;
	    pr.br.y = (cy + 1) * charheight;
	    gClrRect(&pr, clearmode);
	  }
	break;
      /*---------------------------------------------- clear the whole screen */
      case C_D:
	pr.tl.x = pr.tl.y = 0;
	pr.br.x = t_w;
	pr.br.y = t_h;
	gClrRect(&pr, clearmode);
	break;
      /*---------------------------------------------- clear the whole line */
      case C_L:
	pr.tl.x = 0;
	pr.tl.y = cy * charheight;
	pr.br.x = t_w;
	pr.br.y = pr.tl.y + charheight;
	gClrRect(&pr, clearmode);
	break;
    }
}

/* Scroll horizontally */

static void
h_scroll(int right, int pixel, int doclear)
{
  P_RECT  r;
  P_POINT o;

  pixel *= charwidth;
  r.tl.x = cx * charwidth;
  r.tl.y = cy * charheight;
  r.br.y = r.tl.y + charheight;
  r.br.x = t_w;
  o.y    = 0;

  if(right)
    {
      o.x = pixel;
      r.br.x -= pixel;
      wScrollRect(main_win, &r, &o);
      r.br.x = r.tl.x + pixel;
    }
  else
    {
      o.x = -pixel;
      r.tl.x += pixel;
      wScrollRect(main_win, &r, &o);
      r.tl.x = r.br.x - pixel;
    }
  if(doclear)
    gClrRect(&r, clearmode);
}

/* It works only within the scroll region (sr_u & sr_l) */
static void
v_scroll(int down, int p)
{
  P_RECT  r;
  P_POINT o;
  int pixel;

  pixel  = p * charheight;
  r.tl.x = 0;
  r.br.x = t_w;
  r.tl.y = sr_u * charheight;
  r.br.y = sr_l * charheight;
  o.x = 0;

  if(down)
    {
      o.y = pixel;
      r.br.y -= pixel;
      wScrollRect(main_win, &r, &o);
      r.br.y = r.tl.y + pixel;
    }
  else
    {
      extern char copying;

      o.y = -pixel;
      r.tl.y += pixel;
      wScrollRect(main_win, &r, &o);
      r.tl.y = r.br.y - pixel;

      /* Clear the next rows in the scrollback buffer */
      if(pm_scrollback && !copying)
	{
	  int i;
	  unsigned char *sp;

	  i = sb_top + rows;
	  /* Move the scrollback pointer */
	  sb_top +=  p;
	  if(sb_top >= pm_scrollback)
	    sb_top -= pm_scrollback;

	  while(p > 0)
	    {
	      if(i >= pm_scrollback)
	        i -= pm_scrollback;
	      down = MAXCOLS;
	      sb_nl[i] = 0;
	      sp = sb_buf + i * down;
	      while(down--)
		*sp++ = ' ';
	      p--; i++;
	    }
	}
    }
  gClrRect(&r, clearmode);
}

static void
marknl(void)
{
  int i;

  if(!pm_scrollback)
    return;
  i = sb_top + cy;
  if(i >= pm_scrollback)
    i -= pm_scrollback;
  sb_nl[i] = cx;
}

static void
nl(void)
{
  if(cy == sr_l - 1)
    v_scroll(0, 1);
  else
    cy += 1;
}

static void
flsbuf(void)
{
  int i;

  if(bufoff == 0)
    return;
  cx -= bufoff;
  if(e_flags & E_INSERT_MODE)
    h_scroll(1, bufoff, 0);
  gPrintText(cx * charwidth, cy * charheight + ascent, sbuf, bufoff);
  if(pm_scrollback)
    {
      i = sb_top + cy;
      if(i >= pm_scrollback)
        i -= pm_scrollback;
      p_bcpy(sb_buf + i * MAXCOLS + cx, sbuf, bufoff);
    }
  cx += bufoff;
  bufoff = 0;
}

static void
put(unsigned char ch)
{
  int i, n;
  int a0, a1;
  int nargs;

  if(e_state == E_NORMAL)
    {
      if(ch >= ' ')
	{
	  if(cx >= cols)			/* last column printable  */
	    {
	      if(e_flags & E_WRAP_MODE)
	        {
		  flsbuf();
		  nl();
		  cx = 0;
		}
	      else
	        {
		  cx = cols - 1;
		  if (bufoff > 0)
		    bufoff--;
	        }
	    }
          if(ch > 127)
	    ch = iso2cp(ch);

	  if(font == 'A' && ch == '#') /* Pound sign */
	    ch = 0x9c;
	  if(font == 0 && ch >= 0x5f && ch <= 0x7e)
	    ch = altchar[ch - 0x5f];
	    
	  sbuf[bufoff++] = ch;

	  /* We can't write bold characters buffered, as they are
	     1 Pixel wider and we NEED a monospaced font */

	  cx++;
	  if(isbold)
	    flsbuf();
	  return;
	}
      flsbuf();

      /* hack: normalize cx for nowrap mode */
      if (!(e_flags & E_WRAP_MODE) && cx >= cols)
        cx = cols - 1;

      switch(ch)
	{
	case 7:		/* Bell*/
	  p_sound(5, 320);
	  break;
	case '\b':	/* BackSpace */
	  if(cx > 0)
	    cx--;
	  return;
	case '\t':
	  if (cx >= cols)		/* curses bug workaround */
	    {
	      nl();
	      cx = 0;
	    }
	  cx++;
	  while (!tabs[cx] && cx < cols - 1)
	    cx++;
	  return;
	case '\n':
	  nl();
	  return;
	case '\r':
	  marknl();
	  cx = 0;
	  return;
	case 0x1b:		/* Escape */
	  e_state = E_ESCAPE;
	  e_interm = 0;
	  return;
	case 0x0e:		/* C-n */
	case 0x0f:		/* C-o */
	  font = charsets[gl = 0x0f - ch];
	  break;
	default:
	  break;
	}
      return;
    }

  if(ch < ' ')		/* that's the way a vt100 does it */
    {
      int oldstate = e_state;
      e_state = E_NORMAL;
      put(ch);
      if (e_state != E_ESCAPE)
        e_state = oldstate;
      return;
    }

  if(e_state == E_ESCAPE) /* first character after escape */
    {
      if(ch <= '/')
        {
	  e_interm = e_interm ? -1 : ch;
	  return;
	}
      switch (e_interm)
	{
	case 0:
	  switch(ch)
	    {
	    case 'E': /* CR/NL */
	      marknl();
	      nl();
	      cx = 0;
	      return;
	    case 'D': /* Scroll down */
	      nl();
	      break;
	    case 'M': /* Scroll up */
	      if(cy - 1 < sr_u)
		{
		  if(cy >= sr_u)
		    v_scroll(1, 1);
		}
	      else
		cy--;
	      break;
	    case 'H':
	      tabs[cx] = 1;
	      break;
	    case 'Z':
	      p_atos(mfb, "\033[?1;2c"); 
	      P_WRITE(serial, mfb, p_slen(mfb));
	      break;
	    case '7': /* save cursor position */
	      sx = cx; sy = cy;
	      break;
	    case '8': /* restore cursor */
	      cx = sx; cy = sy;
	      break;
	    case 'c': /* Reset */
	      clear(2);
	      Reset();
	      break;;
	    case '[': /* the longer ones */
	      e_state = 0;
	      e_interm = 0;
	      e_args[0] = e_args[1] = 0;
	      return;
	    case 'n':
	    case 'o':
	      font = charsets[gl = ch - ('n' - 2)];
	      return;
	    default:
	      break;
	    }
	  break;
	case '#':
	  if (ch == '8')		/* fill with 'E' */
	    {
	      for (i = 0; i < cols; i++)
	      	sbuf[i] = 'E';
	      for (i = 0; i < rows; i++)
		gPrintText(0, i * charheight + ascent, sbuf, cols);
	    }
	  break;
	case '(':
	case ')':
	case '*':
	case '+':
	  charsets[e_interm - '('] = ch;
	  font = charsets[gl];
	  break;
	default:
	  break;
	}
      e_state = E_NORMAL;
      return;
    }

  /* e_state >= 0 : longer escape sequences */

  if(ch >= '0' && ch <= '9')		/* Arguments */
    {
      if(e_state < MAXARGS)
	e_args[e_state] = e_args[e_state] * 10 + (ch - '0');
      return;
    }

  if(ch == ';' || ch == ':')
    {
      e_state++;
      if(e_state < MAXARGS)
	e_args[e_state] = 0;
      return;
    }

  if(ch <= 0x3f)			/* Intermediate */
    {
      e_interm = e_interm ? -1 : ch;
      return;
    }

  nargs = e_state + 1;
  if (nargs >= MAXARGS)
    nargs = MAXARGS;

  a0 = e_args[0];
  if (a0 == 0)
    a0++;
  a1 = e_args[1];
  if (a1 == 0)
    a1++;

  e_state = E_NORMAL;			/* Last character in sequence */

  switch (e_interm)
    {
    case 0:
      switch(ch)
	{
	case 's':				/* save cursor position */
	  sx = cx; sy = cy;
	  break;

	case '8':				/* restore cursor */
	  cx = sx; cy = sy;
	  break;

	case 'H':				/* Cursor movement */
	case 'f':
	  cy = a0 - 1;
	  cx = a1 - 1;

	  if(e_flags & E_ORIGIN_MODE)
	    {
	      cy += sr_u;
	      if(cy > sr_l - 1) cy = sr_l - 1;
	    }
	  else
	    {
	      if(cy > rows - 1) cy = rows - 1;
	    }
	  if(cx > cols - 1) cx = cols - 1;
	  return;

	case 'J':				/* Clear Display*/
	  if(e_args[0] < 3)
	    clear(e_args[0]);
	  return;

	case 'K':				/* Clear Line */
	  if(e_args[0] < 3)
	    clear(e_args[0] + 3);
	  return;

	case 'A':				/* Up / don't scroll */
	  n = (cy >= sr_u) ? sr_u : 0;		/* scroll region */
	  cy -= a0;
	  if(cy < n)
	    cy = n;
	  return;

	case 'B':				/* Down / don't scroll*/
	  n = (cy < sr_l) ? sr_l : rows;	/* scroll region */
	  cy += a0;
	  if(cy >= n)
	    cy = n - 1;
	  return;

	case 'C':				/* Right */
	  cx += a0;
	  if(cx >= cols)
	    cx = cols - 1;
	  return;

	case 'D':				/* Left */
	  cx -= a0;
	  if(cx < 0)
	    cx = 0;
	  return;

	case 'm':				/* Attributes */
	  {
	    G_GC gc;

	    for(i = 0; i < nargs; i++)
	      switch(e_args[i])
		{
		case  0: e_gstyle  = G_STY_MONO;
			 isbold = 0;
			 break;
		case  1: e_gstyle |= G_STY_BOLD; isbold = 1;	break;
		case  2: /* e_gflags  = G_GC_FLAG_GREY_PLANE;*/	break;
		case  3: e_gstyle |= G_STY_ITALIC;		break;
		case  4: e_gstyle |= G_STY_UNDERLINE;		break;
		case  5: /* Blinking */
		case  7: e_gstyle |= G_STY_INVERSE;		break;
		case 22: /*e_gflags  = G_GC_FLAG_BOTH_PLANES;*/	break;
		case 23: e_gstyle &= ~G_STY_ITALIC;		break;
		case 24: e_gstyle &= ~G_STY_UNDERLINE;	break;
		case 25: /* Not Blinking */
		case 27: e_gstyle &= ~G_STY_INVERSE;		break;
		default: break;
		}
	    gc.style = e_flags & E_ISINVERTED ? e_gstyle ^ G_STY_INVERSE
					      : e_gstyle;
	    gSetGC(hgc, G_GC_MASK_STYLE, &gc);
	  }

	case 'g':				/* Tab clear */
	  if(e_args[0] == 3)
	    for(i = 0; i < MAXCOLS; i++) /* Clear all the tabs */
	      tabs[i] = 0;
	  if(e_args[0] == 0)
	    tabs[cx] = 0;
	  break;

	case 'r':				/* Scroll region */

	  i = a0 - 1;

	  if(e_args[0] == 0 && (nargs == 1 || e_args[1] == 0))
	    a1 = rows;

	  if(i  > rows) i  = rows;
	  if(a1 > rows) a1 = rows;

	  if(i >= a1)
	    return;

	  sr_u = i;
	  sr_l = a1;
	  cx = cy = 0;
	  if (e_flags & E_ORIGIN_MODE)
	    cy += sr_u;
	  return;

	case 'I':	/* Forward tabs */
	  while (a0-- > 0)
	    put('\t');
	  return;

	case 'Z':	/* Backward tabs */
	  while(a0-- > 0)
	    {
	      if(cx > 0)
		cx--;
	      while(!tabs[cx] && cx > 0)
		cx--;
	    }
	  return;

	case 'M':			/* delete lines (scroll up) */
	case 'L':			/* Insert lines (scroll down) */
	  if(cy >= sr_l || cy < sr_u)
	    return;			/* not in scroll region */
	  n = sr_u;
	  sr_u = cy;
	  if(sr_u + a0 > sr_l)
	    a0 = sr_l - sr_u;
	  v_scroll(ch == 'M' ? 0 : 1, a0);
	  sr_u = n;
	  break;

	case 'P':			/* delete characters (scroll left) */
	case '@':			/* Insert characters (scroll right) */
	  if(cx > cols)
	    return;

	  if(cx + a0 > cols)
	    a0 = cols - cx;
	  h_scroll(ch == 'P' ? 0 : 1, a0, 1);
	  break;

	case 'h':			/* Set Mode */
	case 'l':			/* Reset Mode */
	  for(i = 0; i < nargs; i++)
	    if(e_args[i] == 4)
	      {
		e_flags &= ~E_INSERT_MODE;
		if (ch == 'h')
		  e_flags |= E_INSERT_MODE;
	      }
	  return;

	case 'c':
	  if (e_args[0] == 0)
	    {
	      p_atos(mfb, "\033[?1;2c"); 
	      P_WRITE(serial, mfb, p_slen(mfb));
	    }
	  return;

	case 'n':
	  if(a0 == 5)		/* terminal status */
	    {
	      p_atos(mfb, "\033[0n");
	      P_WRITE(serial, mfb, p_slen(mfb));
	    }
	  if(a0 == 6)		/* get position / resize needs it */
	    {
	      p_atos(mfb, "\033[%d;%dR", cy+1, cx+1);
	      P_WRITE(serial, mfb, p_slen(mfb));
	    }
	  return;

	case 'x':			/* decreqtparm */
/*
 * Response CSI sol; par; nbits; xspeed; rspeed; clkmul; flags x
 *                        DECREPTPARM Report of terminal parameters
 *        sol
 *        0       terminal can send unsolicited reports, supported as sol = 1
 *        1       terminal reports only on request
 *        2       this is a report (DECREPTPARM)
 *        3       terminal reporting only on request
 *        par = 1 none, 2 space, 3 mark, 4 odd, 5 even
 *        nbits = 1 (8 bits/char), 2 (7 bits/char)
 *        xspeed, rspeed = transmit and receive speed index:
 *  0,8,16,24,32,40,48,56,64,72,80,88,96,104,112,120,128 correspond to speeds of
 *  50,75,110,134.5,150,200,300,600,1200,1800,2000,2400,3600,4800,9600,19200,
 *        clkmul = 1 (clock rate multiplier is 16)
 *        flags = 0-15 (Setup Block #5), always 0 here
 */
	  if (a0 == 1)
	    {
	      /* Convert baudrate */
	      a0 = pm_baud - 1;
	      if(a0 >= P_BAUD_300 && a0 < P_BAUD_7200)
	        a0++;
	      a0 *= 8;
	      p_atos(mfb, "\033[%d;%d;%d;%d;%d;1;0x",
	        e_args[0] + 2,
		pm_parity ? 6 - pm_parity : 0,
		4 - pm_databits,
		a0, a0);
	      P_WRITE(serial, mfb, p_slen(mfb));
	    }
	  return;

	default:
	  break;
	}
      break;

    case '?':		/* DEC private */
      switch(ch)
	{
	  case 'h':
	  case 'l':
	    for(i = 0; i < nargs; i++)
	      {
		switch(e_args[i])
		  {
		  case 1: 
		    cur_appl = ch == 'h';
		    break;
		  case 5: 
		    if((ch == 'h' && !(e_flags & E_ISINVERTED)) ||
		       (ch == 'l' && (e_flags & E_ISINVERTED)))
		      {
		      	G_GC gc;
			P_RECT p;

			p.tl.x = 0; p.tl.y = 0;
			p.br.x = t_w; p.br.y = t_h;
			gClrRect(&p, G_TRMODE_INV);
			e_flags ^= E_ISINVERTED;
			gc.style = e_gstyle;
			if (e_flags & E_ISINVERTED)
			  {
			    gc.style ^= G_STY_INVERSE;
			    clearmode = G_TRMODE_SET;
			  }
			else
			  clearmode = G_TRMODE_CLR;
			gSetGC(hgc, G_GC_MASK_STYLE, &gc);
		      }
		    break;
		  case 6: 
		    e_flags &= ~E_ORIGIN_MODE;
		    if (ch == 'h')
		      e_flags |= E_ORIGIN_MODE;
		    cx = cy = 0;
		    if(e_flags & E_ORIGIN_MODE)
		      cy += sr_u;
		    break;
		  case 7:
		    e_flags &= ~E_WRAP_MODE;
		    if (ch == 'h')
		      e_flags |= E_WRAP_MODE;
		    break;
		  }
	      }
	    break;
	  default:
	    break;
	}
      break;
    case '>':
      if (ch == 'c' && e_args[0] == 0)
	{
	  p_atos(mfb, "\033[>%d;%d;0c", 'P', VERSION_NUM);
	  P_WRITE(serial, mfb, p_slen(mfb));
	}
      break;
    default:
      break;
    }
}

void
setcursor(int x, int y)
{
  wc.pos.x = x*charwidth;
  wc.pos.y = y*charheight+ascent;
  if(x >= cols) 			/* last column printable */
    wc.pos.x = t_w - charwidth;
  wTextCursor(main_win, &wc);
}

GLDEF_C VOID TtyEmu(unsigned char *buf, INT len)  
{
  extern void *logfd;
  extern char echoon;

  if(logfd)				/* Log into the file */
    p_write(logfd, buf, len);
  if(sendtopid)
    {
      p_pcpyto(sendtopid, (VOID*)sendtoaddr, (VOID*)&len, 1);
      p_pcpyto(sendtopid, (VOID*)(sendtoaddr+1), (VOID*)buf, len);
      if(p_msendreceivew(sendtopid, 0, &sendtopid) != 0)
	sendtopid = 0;
    }
  if(echoon == 0)
    return;

  while(len-- > 0)
    {
      if(pm_debugterm)
        {
	  if(*buf >=' ' && *buf < 0x80)
	    AddToHistory(*buf);
	  else
	    {
	      p_atos(mfb, "%02x", *buf);
	      AddToHistory('\\');
	      AddToHistory(mfb[0]);
	      AddToHistory(mfb[1]);
	    }
        }

      /* Last line in scroll region && Jumpscroll */
      if(*buf == '\n' && pm_jumpscroll && cy == sr_l-1)
        {
	  int i, n;

	  n = 1;
	  for(i = 1; i <= len; i++)
	    if(buf[i] < ' ')
	      {
		if(buf[i] == '\n')
		  n++;
		else if(buf[i] != '\r')
		  break;
	      }

	  if(n >= rows)
	    {
              cy = 0;
	      clear(C_D);
	    }
	  else if(n > 1)
	    {
	      v_scroll(0, n);
	      cy -= n;
	    }
	}

      put(*buf++);
    }

  flsbuf();

  setcursor(cx, cy);
  wFlush();
}

void
AddFnKey(key, modifier, str)
  int key;
  int modifier;
  char *str;
{
  extern struct fkey *fk;
  struct fkey **fp, *f;
  char *s;
  int l;

  for(fp = &fk; *fp; fp = &(*fp)->next)
    if((*fp)->key == key && (*fp)->modifier == modifier)
      break;
  
  l = *str + sizeof(struct fkey) + 1; 
  if((f = (struct fkey *)p_realloc(*fp, l)) == 0)
    {
      wInfoMsgCorner(c_nomem, W_CORNER_BOTTOM_RIGHT);
      return;
    }

  f->key = key;
  f->modifier = modifier;
  if(!*fp)
    f->next = 0;
  s = (char *)f + sizeof(struct fkey);
  p_bcpy(s, str+1, *str);
  s[*str] = 0;
  *fp = f;
}

/* 
 * Parse \<nnn> and \s<nn>
 * NOTE: \<nn> and \<n> are also accepted.
 */
void
SendFnKey(f)
  struct fkey *f;
{
  char *s;
  unsigned char n;

#define isnum(s) s >= '0' && s <= '9'
  for(s = (char *)f + sizeof(struct fkey); *s; s++)
    {
      if(*s == '\\')
        {
	  if(s[1] == 's') /* sleep */
	    {
	      s++; n = 0;
	      if(isnum(s[1])) { s++; n =          s[0] - '0'; }
	      if(isnum(s[1])) { s++; n = n * 10 + s[0] - '0'; }
	      p_sleep((ULONG)n);
	      continue;
	    }
	  else if(isnum(s[1]))
	    {
	      s++; n = s[0] - '0';
	      if(isnum(s[1])) { s++; n = n * 10 + s[0] - '0'; }
	      if(isnum(s[1])) { s++; n = n * 10 + s[0] - '0'; }
	    }
	  else if(s[1] == 0)		/* \somenthing or */
	    break;
	  else
	    n = *s++;
	}
      else
	n = *s;

      P_WRITE(serial, (char *)&n, 1); 
    }
}

void
SetScrollback(n)
  int n;
{
  int mem;

/* TODO: realloc */
  if(sb_buf) p_free(sb_buf);
  if(n < rows) n = 0;
  sb_top = 0;
  sb_beg = -1;
  if(n)
    {
      mem = MAXCOLS * n;
      if((sb_buf = p_alloc(mem + n)) == NULL)
        {
	  wInfoMsgCorner(c_nomem, W_CORNER_BOTTOM_RIGHT);
	  pm_scrollback = 0;
	  return;
	}
      sb_nl = sb_buf;
      while(mem--)
        *sb_nl++ = ' ';
      for(mem = 0; mem < n; mem++)
	sb_nl[mem] = 0;
    }
  pm_scrollback = n;
}

void
inverse(int on)
{
  G_GC gc;

  gc.style = G_STY_MONO; 
  if(on) gc.style |= G_STY_INVERSE;
  gSetGC(hgc, G_GC_MASK_STYLE, &gc);
}

int
getsbll(int row)	/* ScrollBack LineLength */
{
  int sp, ret, i;

  ret = cols;
  sp = row * MAXCOLS;
  if(sb_nl[row] > 0)
    {
      for(ret = i = sb_nl[row]; i < cols; i++)
	if(sb_buf[sp+i] != ' ')
	  ret = i;
      if(ret > cols)
	ret = cols;
    }
  return ret;
}

static void
showsb(int soff, int boff, int lines)
{
  int sp, x, c, i;

  i = boff * MAXCOLS;
  inverse((i >= l_beg && i < l_end) ? ON : OFF);

  boff += sb_top+rows;
  if(lines == rows)
    clear(C_D);

  soff = soff * charheight + ascent;
  while(lines)
    {
      if(boff >= pm_scrollback)
        boff -= pm_scrollback;

      sp = boff * MAXCOLS;
      c = getsbll(boff);

      x = 0;
      if(sp <= sb_beg && (sp + MAXCOLS) > sb_beg)
        {
	  i = sb_beg - sp;
	  if(x+i > c) i = c - x;
	  if(i < 0) i = 0;
	  if(i)
	    gPrintText(0, soff, (char *)sb_buf+sp, i);
	  inverse(ON);
	  sp += i;
	  x += i;
	}
      if(sp <= sb_end && (sp + MAXCOLS - x) > sb_end)
        {
	  i = sb_end - sp;
	  if(x+i > c) i = c - x;
	  if(i < 0) i = 0;
	  if(i)
	    gPrintText(x*charwidth, soff, (char *)sb_buf+sp, i);
	  inverse(OFF);
	  x += i;
	  sp += i;
	}
      if(c-x > 0)
	gPrintText(x*charwidth, soff, (char *)sb_buf+sp, c-x);

      boff++;
      lines--;
      soff += charheight;
    }
}

int
DoCopy(key, modifier)
  int key, modifier;
{
  static int bofp, omod, lp;
  static int ccx, ccy;
  int i, j, n, ocy, ocx;

  if(!pm_scrollback)
    {
      wInfoMsgCorner("Scrollback is off", W_CORNER_BOTTOM_RIGHT);
      return 1;
    }

  ocx = ccx; ocy = ccy;

  switch(key)
    {
      case 0: /* Enter the problems */
	wInfoMsgCorner("Copy mode", W_CORNER_BOTTOM_RIGHT);
	Sw_Serial(OFF);
	ccx = cx;
	ccy = cy;
	omod = 0;
	bofp = pm_scrollback - rows;
	l_beg = l_end = -1;
	sb_beg = sb_end = -1;
	showsb(0, bofp, rows);
	setcursor(ccx, ccy);
        return 0;
      case 27: /* Escape the dungeon */
      case 'x'|W_SPECIAL_KEY:
	wInfoMsgCorner("Emulator mode", W_CORNER_BOTTOM_RIGHT);
	Sw_Serial(ON);
	inverse(OFF);
	wc.pos.x = cx*charwidth;
	wc.pos.y = cy*charheight+ascent;
	showsb(0, pm_scrollback - rows, rows);
	wTextCursor(main_win, &wc);
	return 1;
      case 256: ccy--; break;
      case 257: ccy++; break;
      case 258: ccx++; break;
      case 259: ccx--; break;
      case 260: ccy -= rows; break;
      case 261: ccy += rows; break;
      case 262: ccx = 0; break;
      case 263: ccx = cols-1; break;
      default:
        return 0;
    }

  if((modifier & 2) && omod != 1) /* Shifted, i.e. markmode */
    {
      DoRegister(1);
      if(l_end != -1)
        {
	  sb_beg = sb_end = -1;
	  l_beg = l_end = -1;
	  showsb(0, bofp, rows);
	}
      lp = l_end = l_beg = (bofp+ocy) * MAXCOLS + ocx;
      sb_beg = sb_top+rows+bofp+ocy;
      if(sb_beg >= pm_scrollback)
        sb_beg -= pm_scrollback;
      sb_beg = sb_beg * MAXCOLS + ocx;
    }
  omod = (modifier & 2) ? 1 : 0;

  n = 0;
  if(ccx < 0) ccx = 0;
  if(ccx >= cols) ccx = cols-1;

  if(ccy < 0) 
    {
      if(bofp < -ccy) ccy = -bofp;
      n = ccy;
      ccy = 0;
    }
  if(ccy >= rows)
    {
      if(bofp + ccy >= pm_scrollback)
      	ccy = pm_scrollback - bofp -1;
      n = ccy - rows + 1;
      ccy = rows-1;
    }
  bofp += n;

  if(omod)
    {
      j = (bofp+ccy) * MAXCOLS + ccx;
      i = sb_top+rows+bofp+ccy;
      if(i >= pm_scrollback)
	i -= pm_scrollback;
      i = i * MAXCOLS + ccx;

      if(lp == l_end)
        {
	  lp = l_end = j;
	  sb_end = i;
	}
      else
        {
	  lp = l_beg = j;
	  sb_beg = i;
	}
      if(l_end < l_beg) /* sort */
        {
	  i = sb_end; sb_end = sb_beg; sb_beg = i;
	  i = l_end;  l_end = l_beg;   l_beg = i;
	}
    }

  if(-n >= rows || n >= rows)
    showsb(0, bofp, rows);
  else if(n)
    {
      if(n > 0)
        {
	  v_scroll(0, n);
	  if(omod)
	    n = n + (ccy == ocy ? 1 : (ccy - ocy));
	  showsb(rows - n, bofp+rows-n, n);
	}
      else
        {
	  v_scroll(1, -n);
	  if(omod)
	    n = n - (ccy == ocy ? 1 : (ocy - ccy));
	  showsb(0, bofp, -n);
	}
    }
  else if(ccx != ocx && omod)
    showsb(ccy, bofp+ccy, 1);
  else if(ccy != ocy && omod)
    {
      if(ccy < ocy)
        i = ccy, n = ocy - ccy;
      else
        i = ocy, n = ccy - ocy;
      showsb(i, bofp+i, n+1);
    }

  setcursor(ccx, ccy);
  return 0;
}

#endif /* HAVE_TERMEMU */
