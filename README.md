# Nfsc

By Rudolf König, Jürgen Weiger *et al*.

> [!IMPORTANT]
> This project is now hosted at Codeberg: https://codeberg.org/psion/nfsc

## Intro

**Nfsc** is a terminal emulator. If this file is part of the p3nfs package,
then **Nfsc** is also the psion counterpart to the `p3nfsd` daemon on unix with
additional terminal emulation support.

For `p3nfsd` related info refer to the README file in the `p3nfsd` package.

**Nfsc** emulates a DEC VT100 terminal and some features of the newer vt220.
It has following noteworthy features:

- Support for all builtin Series 3a fonts (9 fonts on the S3a).
- Support for bold, underlined, inverse and italic fonts.
- Alternative character set support.
- Defineable function key support.
- Jumpscroll and charset conversion (ISO8859-1 <-> IBM codepage 850).
- Support for history scrollback (over 500 lines are possible).
- Bring server functionality.
- Paste from the scrollback or from builtin applications.
- Dialing/scripting support. Extensible, as it is done in OPL.
- More than one configuration can be saved/loaded.
- Online help.
- Support for baudrates 50 to 115200.
- Should work on Psion Series 3; it is reported to work on the Siena.
- Robust: most (all?) VT100 animations are working.
- X/YMODEM support (partially working).
- Local mode.
- Experimental 3Fax support.
- Automatic dial at startup.
- TTY:I (infrared).

The current release (5.4) fixes some bugs and adds some features mainly of
interest for non-casual *Nfsc* users. See `CHANGELOG.md` for more.

**License:** GNU General Public Licence (Version 2)

## Installation

Place `nfsc.app` in the `\app` directory and install it with **Psion-I**.
If you also want keypad support, place `fnkeys.nfs` into the `\opd`
directory. Read it (with a text editor) for details.

If you want dial support, install the files `dialme.opl` & `nfscdial.opl`
in the `opl` directory and translate them. See below for more.

The rest of the files are documentation and source code. They should not be
copied to the Psion.

## Building Nfsc (If you have the Psion C SDK)

In the `src` directory, from the DOS prompt:

```batch
C:\PROJECTS\NFSC> makeshd nfsc
C:\PROJECTS\NFSC> rcomp nfsc.hlp
C:\PROJECTS\NFSC> tsc /m nfsc
C:\PROJECTS\NFSC> ren nfsc.img nfsc.app
```

## Notes

- X/Ymodem was tested with the only Zmodem package I found for UNIX.
  Xmodem works, Ymodem has a problem: the receiver won't terminate cleanly
  after a file was sent. I think the Psion driver and the Unix program
  behave differently here.
- break. As there is no `p_break()` function or the like, the break is
  generated as a series of 0 bytes sent at 50 baud. This can work, but
  is not guaranteed, as the definition of break is 0 for at least 250ms
  and this method generates more 200ms pieces. It works on SunOS, but not
  on Solaris.
- Note for S3 owners: I only tested it on my S3a in compatibility mode.
  All but the online help should work, as it is too wide for the S3.
  Please drop me a mail if it works on your S3.
- Siena was reported to work OK.

## Dial Support

It is implemented in OPL, so if you want to use it, you have to write
first an OPL program, and have to translate it (typing Psion-T).

Don't be afraid, it may be sufficient to write a program as simple as:

```opl
PROC dialme:
  global nfscPid%
  loadm "nfscdial"
  dialinit:

  puts:("ATDT 0123456789"+chr$(10))
  expect:("login:")
  puts:("my_username"+chr$(10))
  expect:("Password:")
  puts:("my_password"+chr$(10))
ENDP
```

The function `hotk:("x")` calls **Nfsc** to execute "Psion-x".
You can of course add more expect/puts lines or use OPL features if you
like (or have to :)).

Dialing is a second "thread", it works in parallel, so you can type
on the psion while the script is listening (e.g. your password). The
only problem with this method is that the dial script may lose data.


### How Dial Support Works (for programmers only)

#### Sending text to Nfsc

The opl program sends a message with `ID $40` to the **Nfsc** application,
and the address of the OPL string.

#### Receiving data

The opl program sends a message with `ID $41` to the **Nfsc** application,
and waits for data. It sends a return receipt of 1 if it wants to
quit, 0 if it requests more data.

**Note:** if the OPL program receives a string of length 0, this is in fact
a modem status change report. The first data byte is to interpreted as
    follows:

```text
      Signal	Bit
      CTS	0
      DSR	1
      DCD	2
      RTS	3
      DTR	4
```

As it is a string of length 0 it won't disturb any normal OPL routine.

See `nfscdial.opl` for the implementation of `expect`, `puts` & `hotk`.
(It would be nice to have a "`gets`" call.)

## Tips

- You can save your modem parameters with a little opl "dialme" program
  like the following:

```opl
PROC dialme:
  global nfscPid%
  loadm "nfscdial"
  dialinit:

  puts:("ATZ"+chr$(10))
ENDP
```

- If you're logging in to a UNIX system via **Nfsc**, and you dont want to
  start the `p3nfsd` program there (the UNIX counterpart of **Nfsc**), you may
  have an inappropriate number of lines set. `p3nfsd` takes care of that,
  but you have to be root while starting it.

  Here is how to fix the number of rows (a little UNIX exercise):

  ```bash
  # Check the number of lines (rows) with the following command:
  % stty -a
  # To set the number of rows to 20, try
  % stty rows 20
  # If this won't work, then try
  % setenv LINES 20
  # If you now get "setenv: not found", then (you're using a different shell):
  $ LINES=20
  $ export LINES
  ```


- If you like to fiddle with the save files manually, then you can use the
  "include filename" statement. Include files can be nested and "Exit"
  will not overwrite the file if there was an "include" in it.

## History

**Nfsc** was originally developed as an OPL counterpart for the UNIX p3nfs
daemon which is a way to connect psions to UNIX computers.

After N.N. rewrote it as a C program, I (Rudi) added terminal emulator
code to it. Then Michael checked it once more together with me so that
the vt100 animations ran on it. Odd Gripensteim used it on a VMS system
and wished he had key translation, so he got it (but not everything he
wished for).

The famous WWW page ot Steve Litchfield helped me to reorganize the
whole, and add some user features to it, like online help and config
saved in a file. For a Bring server I have to remember the data on the
screen. From here was a little to get scrollback too.

## Sources

```text
nfsc.c        | protocol and dialog routines
vt100.c       | terminal emulator stuff + scrollback handling
xymodem.c     | X/Y modem support
paste.c       | bring + paste server
params.c      | parameter loading and saving
nfsc.hlp      | guess what
```
