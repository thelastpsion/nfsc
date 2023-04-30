# nfsc 5.4 (Iain Tuddenham's wish list)

> Fri Jan 30 12:15:19 SGT 1998

- Now nfsc save files support the include directive:
  include \<path\> reads in the specified file. Note that
  in this case "exit" wont save the file.
- "Duplicate character in insert mode" bug fixed.
  This could be seen in programs like emacs and pine. They actually
  use insert mode.
- Some hotkeys changed to be more "psion" conform.
  (Exit without saving, Paste)
- Saving X/Ymodem mode bug fixed
- UNIX-Mode: Only the drives marked for exporting are exported to p3nfsd.
- Modem state changes are now reported to the OPL program.
- "Toggle echo" menu entry added for switching off the display, used for
  speeding up ascii downloads.
- Sending more than 127 bytes from the OPL program is handled correctly now.
- OPL program: lockups occured at sending data while waiting for text are
  eliminated. The data sent to nfsc will not be sent back to the OPL program,
  as the OPL program waits for the ack from the send, so it cannot listen
  for new data.
- limited the number of scrollback lines to 800 as the old limit (1000)
  caused allocating 81000-65536=15464 bytes
