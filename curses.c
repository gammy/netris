/*
 * Netris -- A free networked version of T*tris
 * Copyright (C) 1994-2016  Mark H. Weaver et al
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * For more riveting literature, see the X/Open Issue 4 Version 2 Standard:
 * http://pubs.opengroup.org/onlinepubs/9693989999/toc.pdf
 */

#include "netris.h"
#include <time.h>
#include <sys/types.h>
#include <unistd.h>
#include <curses.h>
#include <term.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

#ifdef NCURSES_VERSION
// PDCurses *also* sets NCURSES_VERSION, and supports the same features
#define HAVE_ENHANCED_CURSES
#endif

#ifndef HAVE_ENHANCED_CURSES
#pragma message("Compiling without enhanced curses support - colors disabled")

static char *term_vi;	/* String to make cursor invisible */
static char *term_ve;	/* String to make cursor visible */

int curs_set(int visibility) {
    char *code = visibility == 0 ? term_vi: term_ve;
    if(! code)
        return(1);
	fputs(code, stdout);
	fflush(stdout);
	return(0);
}

/* Attempt to obtain hide/show cursor codes via termcap, or
 * revert to hardcoded defaults if such a lookup fails */
ExtFunc void GetTermcapInfo(void)
{
	char *term, *buf, *data;
	int bufSize = 10240;

	if (!(term = getenv("TERM"))) {
		fprintf(stderr, "TERM environment variable is not set\n");
		return;
	}

	int ret = tgetent(scratch, term);
	switch(ret) {
		default:
			fprintf(stderr, "tgetent: No termcap database found\n");
			break;
		case 0:
			fprintf(stderr, "tgetent: No termcap entry for terminal '%s'\n", 
					term);
			break;
		case 1:
			/*
			 * Make the buffer HUGE, since tgetstr is unsafe.
			 * Allocate it on the heap too.
			 */
			data = buf = malloc(bufSize);

			/*
			 * There is no standard include file for tgetstr, no prototype
			 * definitions.  I like casting better than using my own prototypes
			 * because if I guess the prototype, I might be wrong, especially
			 * with regards to "const".
			 */
			term_vi = (char *)tgetstr("vi", &data); // vi - DECTCEM Hide cursor
			term_ve = (char *)tgetstr("ve", &data); // ve - DECTCEM	Show cursor

			/* Okay, so I'm paranoid; I just don't like unsafe routines */
			if (data > buf + bufSize)
				fatal("tgetstr overflow, you must have a very sick termcap");

			/* Trim off the unused portion of buffer */
			buf = realloc(buf, data - buf);
			break;
	}

	/*
	 * If that fails, use hardcoded vt220 codes.
	 * They don't seem to do anything bad on vt100's, so
	 * we'll try them just in case they work.
	 */
	if (!term_vi || !term_ve) {
		static char *vts[] = {
				"vt100", "vt101", "vt102",
				"vt200", "vt220", "vt300",
				"vt320", "vt400", "vt420",
				"screen", "xterm", NULL };
		int i;

		for (i = 0; vts[i]; i++)
			if (!strcmp(term, vts[i]))
			{
				term_vi = "\033[?25l";
				term_ve = "\033[?25h";
				break;
			}
	}
	if (!term_vi || !term_ve)
		term_vi = term_ve = NULL;
}

#else

static struct
{
	BlockType type;
	short color;
} myColorTable[] =
{
	{ BT_white,		COLOR_WHITE },
	{ BT_blue,		COLOR_BLUE },
	{ BT_magenta,	COLOR_MAGENTA },
	{ BT_cyan,		COLOR_CYAN },
	{ BT_yellow,	COLOR_YELLOW },
	{ BT_green,		COLOR_GREEN },
	{ BT_red,		COLOR_RED },
	{ BT_none, 0 }
};

#endif

static void PlotBlock1(int scr, int y, int x, BlockType type);
static MyEventType KeyGenFunc(EventGenRec *gen, MyEvent *event);

static EventGenRec keyGen =
		{ NULL, 0, FT_read, STDIN_FILENO, KeyGenFunc, EM_key };

static int boardYPos[MAX_SCREENS], boardXPos[MAX_SCREENS];
static int statusYPos, statusXPos;
static int haveColor;
static int screens_dirty = 0;

ExtFunc void InitScreens(void)
{
	MySigSet oldMask;

	/*
	 * Block signals while initializing curses.  Otherwise a badly timed
	 * Ctrl-C during initialization might leave the terminal in a bad state.
	 */
	BlockSignals(&oldMask, SIGINT, 0);

#ifndef HAVE_ENHANCED_CURSES
	GetTermcapInfo();
#endif
	initscr();

#ifdef CURSES_HACK
	{
		extern char *CS;

		CS = 0;
	}
#endif

#ifdef HAVE_ENHANCED_CURSES
	colorEnable = 1;
	haveColor = colorEnable && has_colors();
	if (haveColor)
	{
		start_color();
		// X/Open Curses Issue 4, Version 2 makes no mention of 
		// `use_default_colors()`, but at least ncurses and pdcurses has it.
		// This should assist in supporting modern VTs with support for 
		// such things as RGB colors and transparent backgrounds>
		use_default_colors();

		int i = 0;
		for (i = 0; myColorTable[i].type != BT_none; ++i)
			init_pair(myColorTable[i].type, COLOR_BLACK,
					  myColorTable[i].color);
	}
#else
	haveColor = 0;
#endif

	AtExit(CleanupScreens);
	screens_dirty = 1;
	RestoreSignals(NULL, &oldMask);

	cbreak();
	noecho();
	curs_set(0);
	AddEventGen(&keyGen);

	move(0, 0);
	addstr("Netris ");
	addstr(version_string);
	addstr(" (C) 1994-2016  Mark H. Weaver et al");
	mvprintw(0, 55, "\"netris -h\" for more info");

	statusYPos = 22;
	statusXPos = 0;
}

ExtFunc void CleanupScreens(void)
{
	if (screens_dirty) {
		RemoveEventGen(&keyGen);
		endwin();
		curs_set(1);
		screens_dirty = 0;
	}
}

ExtFunc void InitScreen(int scr)
{
	int y, x;

	if (scr == 0)
		boardXPos[scr] = 1;
	else
		boardXPos[scr] = boardXPos[scr - 1] +
					2 * boardWidth[scr - 1] + 3;
	boardYPos[scr] = 22;
	if (statusXPos < boardXPos[scr] + 2 * boardWidth[scr] + 3)
		statusXPos = boardXPos[scr] + 2 * boardWidth[scr] + 3;
	for (y = boardVisible[scr] - 1; y >= 0; --y) {
		move(boardYPos[scr] - y, boardXPos[scr] - 1);
		addch('|');
		for (x = boardWidth[scr] - 1; x >= 0; --x)
			addstr("  ");
		move(boardYPos[scr] - y, boardXPos[scr] + 2 * boardWidth[scr]);
		addch('|');
	}
	for (y = boardVisible[scr]; y >= -1; y -= boardVisible[scr] + 1) {
		move(boardYPos[scr] - y, boardXPos[scr] - 1);
		addch('+');
		for (x = boardWidth[scr] - 1; x >= 0; --x)
			addstr("--");
		addch('+');
	}
}

ExtFunc void InvertScreen(int scr)
{
	int y, x;

	if (scr == 0)
		boardXPos[scr] = 1;
	else
		boardXPos[scr] = boardXPos[scr - 1] +
					2 * boardWidth[scr - 1] + 3;

	for (y = 0; y < boardVisible[scr]; y++) {
		int ry = 3 + y;
		for (x = 0; x < 2 * boardWidth[scr]; x++) {
			int rx = boardXPos[scr] + x;
			move(ry, rx);
			chtype attrs = inch();
#ifdef HAVE_ENHANCED_CURSES
			int colorpair = PAIR_NUMBER(attrs);
			if (colorpair != 0)
				chgat(1, A_REVERSE, colorpair, NULL);
#else
			if(attrs != ' ') {
				attrs ^= A_STANDOUT;
				addch(attrs);
			}
#endif
		}
	}
}

ExtFunc void CleanupScreen(int scr)
{
}

static void PlotBlock1(int scr, int y, int x, BlockType type)
{
	int colorIndex = abs(type);

	move(boardYPos[scr] - y, boardXPos[scr] + 2 * x);

	if (type == BT_none)
		addstr("  ");
	else
	{
		if (standoutEnable)
		{

#ifdef COLOR_PAIR
			if (haveColor)
				attrset(COLOR_PAIR(colorIndex));
			else
#endif
				standout();
		}

		addstr(type > 0 ? "[]" : "$$");
		standend();
	}
}

ExtFunc void PlotBlock(int scr, int y, int x, BlockType type)
{
	if (y >= 0 && y < boardVisible[scr] && x >= 0 && x < boardWidth[scr])
		PlotBlock1(scr, y, x, type);
}

ExtFunc void PlotUnderline(int scr, int x, int flag)
{
	move(boardYPos[scr] + 1, boardXPos[scr] + 2 * x);
	addstr(flag ? "==" : "--");
}

ExtFunc void ClearStatus(void)
{
	move(statusYPos - 1, statusXPos);
	clrtoeol();
	refresh();
}

ExtFunc void PrintStatus(const char *fmt, ...)
{
	va_list argp;
	move(statusYPos - 1, statusXPos);

#ifdef HAVE_ENHANCED_CURSES
	va_start(argp, fmt);
	vw_printw(stdscr, fmt, argp);
	va_end(argp);
#else
	char tmp[64];
	va_start(argp, fmt);
	vsnprintf(tmp, 64, fmt, argp); // "At most 64 bytes, including NULL"
	va_end(argp);
	printw(tmp);
#endif

	clrtoeol();
	refresh();
}

ExtFunc void ShowDisplayInfo(void)
{

	move(statusYPos - 9, statusXPos);
	printw("Seed:  %d", initSeed);
	clrtoeol();
	move(statusYPos - 8, statusXPos);
	printw("Speed: %dms", speed / 1000);
	clrtoeol();

	if(gameType == GT_onePlayer) {
		mvprintw(statusYPos - 5, statusXPos, "Games lost       %3d", 
				 lost);
		mvprintw(statusYPos - 4, statusXPos, "Rows (this game) %3d", 
				 myLinesCleared);
		mvprintw(statusYPos - 3, statusXPos, "Rows (all games) %3d", 
				 myTotalLinesCleared);
	} else {
		move(statusYPos - 7, statusXPos + 15);
		addstr(robotEnable ? "Robot" : "  You");

		move(statusYPos - 7, statusXPos + 24);
		if((opponentFlags & SCF_usingRobot)) {
			addstr("   Robot");
			if(opponentFlags & SCF_fairRobot)
				addstr("(fair)");
		} else
			addstr("Opponent");
		clrtoeol();

		mvprintw(statusYPos - 6, statusXPos, "Games won        %3d", 
				 won);
		mvprintw(statusYPos - 5, statusXPos, "Rows (this game) %3d", 
				 myLinesCleared);
		mvprintw(statusYPos - 4, statusXPos, "Rows (all games) %3d", 
				 myTotalLinesCleared);

		mvprintw(statusYPos - 6, statusXPos + 22, "%3d", 
				 lost);
		mvprintw(statusYPos - 5, statusXPos + 22, "%3d", 
				 opponentLinesCleared);
		mvprintw(statusYPos - 4, statusXPos + 22, "%3d", 
				 opponentTotalLinesCleared);
	}

}

ExtFunc void UpdateOpponentDisplay(void)
{
	move(1, 0);
	printw("Playing %s@%s", opponentName, opponentHost);
	clrtoeol();
}

ExtFunc void ShowPause(int pausedByMe, int pausedByThem)
{
	move(statusYPos - 2, statusXPos);

	if(! pausedByThem && ! pausedByMe) {
		clrtoeol();
		return;
	}

	if(pausedByMe && pausedByThem)
		printw("Paused by you & opponent");
	else if(pausedByMe)
		printw("Paused by you");
	else if(pausedByThem)
		printw("Paused by opponent");

	clrtoeol();
}

ExtFunc void Message(char *s)
{
	static int line = 0;

	move(statusYPos - 20 + line, statusXPos);
	addstr(s);	/* XXX Should truncate long lines */
	clrtoeol();
	line = (line + 1) % 10;
	move(statusYPos - 20 + line, statusXPos);
	clrtoeol();
}

ExtFunc void RefreshScreen(void)
{
	static char timeStr[2][32];
	time_t theTime;

	time(&theTime);
	strftime(timeStr[0], 30, "%I:%M %p", localtime(&theTime));
	/* Just in case the local curses library sucks */
	if (strcmp(timeStr[0], timeStr[1]))
	{
		move(statusYPos, statusXPos);
		addstr(timeStr[0]);
		strcpy(timeStr[1], timeStr[0]);
	}
	move(boardYPos[0] + 1, boardXPos[0] + 2 * boardWidth[0] + 1);
	refresh();
}

ExtFunc void ScheduleFullRedraw(void)
{
	touchwin(stdscr);
}

static MyEventType KeyGenFunc(EventGenRec *gen, MyEvent *event)
{
	if (MyRead(gen->fd, &event->u.key, 1))
		return E_key;
	else
		return E_none;
}

/*
 * vi: ts=4 ai
 * vim: noai si
 */
