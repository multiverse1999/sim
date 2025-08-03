#include <stdio.h>

#include <sys/ioctl.h>
#include <signal.h>
#include <termios.h>

#include "sim.h"

static void handle(int);

static struct termios initial_state;

extern void
win_end(void)
{
	printf(ED CSI "H");
	tcsetattr(0, TCSAFLUSH, &initial_state);
}

extern void
win_init(Window* w)
{
	struct termios raw_state;

	tcgetattr(0, &initial_state);
	raw_state = initial_state;
	raw_state.c_lflag &= ~(ECHO|ICANON|ISIG);
	tcsetattr(0, TCSAFLUSH, &raw_state);
	setbuf(stdout, NULL);
	printf(CSI "H\t" CSI "6n");
	scanf("\x1b[%*d;%huR", &w->t);
	--w->t;
	handle(SIGWINCH);
}

extern void
win_query(Window* w)
{
	struct winsize ws;

	ioctl(0, TIOCGWINSZ, &ws);
	w->wx = ws.ws_col;
	w->wy = ws.ws_row;
}

static void
handle(int sig)
{
	resize();
	signal(SIGWINCH, handle);
}
