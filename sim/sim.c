#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

#include "sim.h"

#define MINSIZE 16
#define MAXEMPTY 256
#define UTFMAX 6
#define FILECOUNT 8
#define LENGTH(a) (sizeof(a)/sizeof(a[0]))
#define RED(a) CSI "31m" a CSI "0m"
#define GREEN(a) CSI "32m" a CSI "0m"

typedef long Posn;
typedef ulong Rune;

enum {
	Up = 1,
	Down,
	Left,
	Right,
	HalfUp,
	HalfDown,
	Top,
	Bottom,
	Letter,
	Word,
	EndWord,
	PrevWord,
	Till,
	Line,
	StartLine,
	EndLine,
	Insert,
	Delete,
	Change,
	Ctrl = -0x60,
	Esc = 0x1b,
	Del = 0x7f
};

typedef struct {
	Posn p0, p1;
} Address;

typedef struct {
	Rune *s;
	ulong n; /*number of filled characters*/
	ulong size;
} String;

typedef struct Buffer Buffer;

struct Buffer {
	String is;
	String ds;
	uint c;
	uint arg;
	uint count;
	Posn p0;
	Buffer* next;
	Buffer* prev;
};

typedef struct {
	String s;
	String name;
	Address dot;
	Buffer* b;  /*buffer ll*/
	Buffer* bd; /*disk buffer pointer*/
} File;

typedef struct {
	uchar key;
	void (*func)(int);
	int value;
} Key;

typedef struct {
	Address* a;
	ushort cur;
	ushort n;
	ushort size;
} Frame;

static Rune* Strchr(Rune *s, Rune r);
static String Strn(Rune *r, ulong n);
static Rune* Strnstr(Rune *s1, Rune *s2, ulong n);
static String Utf(char* s);
static void blind_reader(Frame* fr, Posn p0);
static void blind_writer(ushort line, ushort offset, ushort top, ushort bot);
static void buf_add(
	String* is, String* ds, uint c, uint arg, uint count
);
static void buf_free(Buffer* p);
static int c2r(Rune *r, uchar c, uchar *seq);
static void change(int arg);
static void count(int arg);
static void curmov(ushort x, ushort y);
static uint curpos(void);
static void delete(int arg);
static void dot(int arg);
static void redo(int arg);
static void escape(int c);
static uchar getc2(void);
static Rune getr(void);
static void init(void);
static void input(String* s, uint line, char* prefix);
static void insert(int arg);
static int  isword(uchar c);
static void file_close(int arg);
static void file_init(File* f);
static void file_load(File* f);
static void file_open(int arg);
static void file_save(int arg);
static void fr_add(Frame* fr, Address a);
static void fr_calc(void);
static void fr_close(Frame* fr);
static void fr_init(Frame* fr);
static void fr_insert(Frame* p, Frame q, ushort n);
static void fr_insure(Frame* fr, ushort n);
static void fr_update(void);
static void fr_zero(Frame*);
static void gmove(int arg);
static void msg(uint line, char* fmt, ...);
static void paste(int arg);
static void pline(int arg);
static uchar r2u(char *s, Rune r);
static uint runesiz(Rune c, ulong wx);
static void search(int arg);
static int  selection(int arg);
static ulong str2u(char** buf, String s);
static void str_init(String* p);
static void str_close(String* p);
static void str_dup(String* p, String q);
static void str_zero(String* p);
static void str_insure(String* p, ulong n);
static void str_addr(String* p, Rune r);
static void str_adds(String* p, String q);
static void str_delr(String* p);
static void str_insert(String* p, String q, Posn p0);
static void str_delete(String* p, Posn p0, Posn p1);
static void u2str(String *s, char *u);
static void undo(int arg);
static void move(int);
static void quit(int);
static void yank(int arg);

static Frame frame[FILECOUNT], *fr = frame;
static File file[FILECOUNT], *f = file;
static String istr, srch;
static Window w;
static uint counter;
static char *ctrune[0x20] = {
	"nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel",
	"bs" , "\\t", "\\n", "vt" , "ff" , "cr" , "so" , "si",
	"dle", "dc1", "dc2", "dc3", "dc4", "nak", "syn", "etb",
	"can", "em" , "sub", "esc", "fs" , "gs" , "rs" , "us"
};

#include "config.h"

extern void
die(char* fmt, ...)
{
	static char *s;
	va_list ap;
	uint i;

	win_end();
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	if (fmt[0] && fmt[strlen(fmt) - 1] == ':')
		perror(NULL);
	else
		fputc('\n', stderr);
	for (i = 0; i < FILECOUNT; ++i) {
		f = file + i;
		if (!f->s.n)
			continue;
		str_adds(&f->name, Utf(".swap"));
		file_save(i);
		str2u(&s, f->name);
		fprintf(stderr, "file saved to %s\n", s);
	}
	abort();
}

extern void*
emalloc(ulong n)
{
	void* p;

	p = malloc(n);
	if (!p)
		die("malloc:");
	memset(p, 0, n);
	return p;
}

extern void*
erealloc(void* p, ulong n)
{
	p = realloc(p, n);
	if (!p)
		die("realloc:");
	return p;
}

extern void
resize(void)
{
	Window wt;

	win_query(&wt);
	if (wt.wx != w.wx || wt.wy != w.wy) {
		w.wx = wt.wx;
		w.wy = wt.wy;
		fr_zero(fr);
		fr_update();
	}
}

static Rune*
Strchr(Rune *s, Rune r)
{
	for (;*s != '\0'; ++s)
		if (*s == r)
			return s;
	return NULL;
}

static String
Strn(Rune *r, ulong n)
{
	static String s;
	ulong l;

	l = (n + 1) * sizeof(*s.s);
	s.s = erealloc(s.s, l);
	s.n = n;
	memcpy(s.s, r, l);
	s.s[s.n] = '\0';
	return s;
}

static Rune*
Strnstr(Rune *s1, Rune *s2, ulong n)
{
	n *= sizeof(*s2);
	do {
		s1 = Strchr(s1, s2[0]);
		if (s1 != NULL && !memcmp(s1, s2, n))
			return s1;
	} while (s1++ != NULL);
	return NULL;
}

static String
Utf(char* s)
{
	static String t;

	u2str(&t, s);
	return t;
}

static void
blind_reader(Frame* fr, Posn p0)
{
	Address a;
	uint wx;

	a.p0 = a.p1 = p0;
	do {
		for (wx = 0; wx < w.wx && a.p1 < f->s.n;) {
			if (f->s.s[a.p1] == '\n')
				break;
			wx += runesiz(f->s.s[a.p1], wx);
			if (wx < w.wx)
				++a.p1;
			if (wx >= w.wx) {
				if (f->s.s[a.p1] == '\t')
					++a.p1;
				if (f->s.s[a.p1 + 1] == '\n')
					++a.p1;
			}
		}
		fr_add(fr, a);
		a.p0 = ++a.p1;
	} while (a.p1 <= f->s.n && f->s.s[a.p1 - 1] != '\n');
}

static void
blind_writer(ushort line, ushort offset, ushort top, ushort bot)
{
	static char *s;
	Posn i, o;
	ulong n;

	i = o = 0;
	if (offset >= top)
		i = offset - top + 1;
	if (fr->n - offset > bot)
		o = offset + bot - 1;
	else if (fr->n)
		o = fr->n - 1;
	curmov(0, offset > line ? 0 : line - offset);
	i = fr->a[i].p0;
	o = fr->a[o].p1;
	n = str2u(&s, Strn(f->s.s + i, o - i));
	fwrite(s, n, 1, stdout);
}

static void
buf_add(String* is, String* ds, uint c, uint arg, uint count)
{
	if (f->b->next)
		buf_free(f->b->next);
	f->b->next = emalloc(sizeof(*f->b->next));
	f->b->next->prev = f->b;
	f->b = f->b->next;
	if (is != NULL)
		str_insert(&f->b->is, *is, 0);
	if (ds != NULL)
		str_insert(&f->b->ds, *ds, 0);
	f->b->c = c;
	f->b->arg = arg;
	f->b->count = count;
	f->b->p0 = f->dot.p0;
	fr_zero(fr);
}

static void
buf_free(Buffer* p)
{
	if (p->next != NULL)
		buf_free(p->next);
	str_close(&p->is);
	str_close(&p->ds);
	free(p);
}

static int
c2r(Rune *r, uchar c, uchar *seq)
{
	if (*seq != 0) {
		if ((c & 0xc0) != 0x80) {
			*seq = 0;
			return -1;
		}
		*r = (*r << 6) | (c & 0x3f);
		--*seq;
		return 0;
	}
	if ((c & 0x80) == 0) {
		*r = c & 0x7f;
		*seq = 0;
	} else if ((c & 0xe0) == 0xc0) {
		*r = c & 0x1f;
		*seq = 1;
	} else if ((c & 0xf0) == 0xe0) {
		*r = c & 0x0f;
		*seq = 2;
	} else if ((c & 0xf8) == 0xf0) {
		*r = c & 0x07;
		*seq = 3;
	} else if ((c & 0xfc) == 0xf8) {
		*r = c & 0x03;
		*seq = 4;
	} else if ((c & 0xfe) == 0xfc) {
		*r = c & 0x01;
		*seq = 5;
	} else {
		*r = c;
		*seq = 0;
	}
	return 0;
}

static void
change(int arg)
{
	String s;
	Address *a;
	uint count;

	if (!arg)
		arg = getr();
	switch (arg) {
	case 'x': arg = Letter; break;
	case 'c': arg = Line; break;
	case 'G': arg = Bottom; break;
	case 'g': arg = Top; break;
	case 'w': arg = Word; break;
	case 't': arg = Till; break;
	}
	count = counter;
	arg = selection(arg);
	if (arg == Word || arg == Letter || arg > 0x7f)
		++f->dot.p1;
	str_init(&s);
	a = &f->dot;
	if (a->p0 != a->p1) {
		str_adds(&s, Strn(f->s.s + a->p0, a->p1 - a->p0));
		str_delete(&f->s, a->p0, a->p1);
		a->p1 = a->p0;
	}
	fr_zero(fr);
	insert(0);
	if (s.n)
		str_insert(&f->b->ds, s, 0);
	f->b->c = Change;
	f->b->arg = arg;
	f->b->count = count;
	str_close(&s);
	fr_update();
}

static void
count(int arg)
{
	if (!counter && !arg) {
		move(StartLine);
		return;
	}
	counter *= 10;
	counter += arg;
}

static void
curmov(ushort x, ushort y)
{
	printf(CSI "%hu;%huH", y, x);
}

static uint
curpos(void)
{
	ulong i, wx;

	wx = 0;
	for (i = fr->a[fr->cur].p0; i < f->dot.p1; ++i)
		wx += runesiz(f->s.s[i], wx);
	return wx;
}

static void
delete(int arg)
{
	String s;
	Address *a;
	uint count;

	if (!f->s.n)
		return;
	if (!arg) {
		switch (arg = getr()) {
		case 'x': arg = Letter; break;
		case 'd': arg = Line; break;
		case 'G': arg = Bottom; break;
		case 'g': arg = Top; break;
		case 'w': arg = Word; break;
		case 't': arg = Till; break;
		default:
			return;
		}
	}
	count = counter;
	if ((arg = selection(arg)) < 0)
		return;
	str_init(&s);
	a = &f->dot;
	str_adds(&s, Strn(f->s.s + a->p0, a->p1 + 1 - a->p0));
	str_dup(&istr, s);
	buf_add(NULL, &s, Delete, arg, count);
	str_delete(&f->s, a->p0, a->p1 + 1);
	str_close(&s);
	if (a->p0 == f->s.n && a->p0)
		--a->p0;
	a->p1 = a->p0;
	fr_update();
}

static void
dot(int arg)
{
	String ds;
	Address *a;

	if (f->b->prev == NULL)
		return;
	a = &f->dot;
	arg = f->b->arg;
	counter = f->b->count;
	switch (f->b->c) {
	case Insert:
		if (arg == Down)
			move(EndLine);
		else
			move(arg);
		str_insert(&f->s, f->b->is, a->p0);
		buf_add(&f->b->is, NULL, Insert, arg, counter);
		break;
	case Delete:
		delete(arg);
		break;
	case Change:
		str_init(&ds);
		if (f->b->ds.n) {
			selection(arg);
			if (arg == Word || arg == Letter || arg > 0x7f)
				++a->p1;
			str_adds(&ds
				, Strn(f->s.s + a->p0, a->p1 - a->p0)
			);
			str_delete(&f->s, a->p0, a->p1);
		}
		if (f->b->is.n)
			str_insert(&f->s, f->b->is, f->dot.p0);
		buf_add(&f->b->is, &ds, Change, arg, f->b->count);
		str_close(&ds);
		f->dot.p1 = f->dot.p0;
		break;
	}
	fr_update();
}

static void
redo(int arg)
{
	if (f->b->next == NULL)
		return;
	if (arg) {
		for (;f->b->next != NULL;)
			redo(0);
		return;
	}
	f->b = f->b->next;
	if (f->b->ds.n)
		str_delete(&f->s, f->b->p0, f->b->p0 + f->b->ds.n);
	if (f->b->is.n)
		str_insert(&f->s, f->b->is, f->b->p0);
	f->dot.p0 = f->dot.p1 = f->b->p0;
	fr_zero(fr);
	fr_update();
}

static void
escape(int c)
{
	counter = 0;
	c = getc2() - 0x30;
	if (c > 0 && c <= 8) {
		--c;
		f = &file[c];
		fr = &frame[c];
		return;
	}
	ungetc(c + 0x30, stdin);
}

static uchar
getc2(void)
{
	uchar c;

get:
	c = getchar();
	if (c == (uchar)EOF) {
		if (!feof(stdin))
			goto get;
		exit(0);
	}
	return c;
}

static Rune
getr(void)
{
	Rune r;
	uchar c, seq;

	r = 0;
	seq = 0;
	do {
		c = getc2();
		if (c2r(&r, c, &seq) == -1) {
			seq = 0;
			ungetc(c, stdin);
		}
	} while (seq != 0);
	return r;
}

static void
input(String* s, uint line, char* prefix)
{
	static char *t;
	Rune r;

	for (;;) {
		str2u(&t, *s);
		msg(line, "%s%s", prefix, t);
		switch (r = getr()) {
		case Esc:
			str_zero(s);
		case '\n':
			return;
		case Del:
			str_delr(s);
			break;
		default:
			str_addr(s, r);
		}
	}
	s->s[s->n] = '\0';
}

static int
isword(uchar c)
{
	switch (c) {
	case ' ': case '\t': case '\n': case '.':
	case '(': case ')':  case '{':  case '}':
	case '[': case ']':  case ':':  case ';':
	case ',': case '<':  case '>':  case '#':
	case '*': case '+':  case '-':  case '!':
	case '%': case '\\': case '/':  case '"':
	case '=':
	return 0;
	}
	return 1;
}

static void
insert(int arg)
{
	String s, c;

	if (f->s.s[f->s.n - 1] != '\n')
		str_addr(&f->s, '\n');
	str_init(&s), str_init(&c);
	str_addr(&c, '\0');
	switch (arg) {
	case StartLine:
	case EndLine:
	case Right:
		move(arg);
		break;
	case Down:
		move(EndLine);
		str_addr(&s, '\n');
		str_insert(&f->s, s, f->dot.p0);
		++f->dot.p1;
		break;
	}
	for (;;) {
		fr_update();
		switch (c.s[0] = getr()) {
		case Esc:
			goto endmode;
		case Del:
			if (f->dot.p1 != f->dot.p0) {
				str_delr(&s);
				str_delete(&f->s, f->dot.p1 - 1, f->dot.p1);
				--f->dot.p1;
			}
			break;
		default:
			str_addr(&s, c.s[0]);
			str_insert(&f->s, c, f->dot.p1);
		}
		f->dot.p1 = f->dot.p0 + s.n;
	}
	endmode:
	str_dup(&istr, s);
	for (counter ? --counter : 0; counter; --counter) {
		str_insert(&istr, s, istr.n);
		str_insert(&f->s, s, f->dot.p0);
	}
	buf_add(&istr, NULL, Insert, arg, counter);
	str_close(&s), str_close(&c);
	f->dot.p0 = f->dot.p1;
	if (f->dot.p1 >= f->s.n)
		move(Left);
}

static void
init(void)
{
	uint i;

	for (i = 0; i < FILECOUNT; ++i) {
		file_init(&file[i]);
		fr_init(&frame[i]);
	}
	str_init(&srch);
	str_init(&istr);
	win_init(&w);
}

static void
file_close(int arg)
{
	static char *s;

	if (arg != -1)
		f = &file[arg];
	if (f->bd != f->b) {
		str2u(&s, f->name);
		msg(w.wy / 2, RED("Save %s?") " [y/n]"
			, f->name.n ? s	: "-unnamed-"
		);
		if (getr() == 'y')
			file_save(arg);
	}
	str_close(&f->s);
	str_close(&f->name);
	for (;f->b->prev != NULL; f->b = f->b->prev);
	buf_free(f->b);
	file_init(f);
	fr_zero(fr);
}

static void
file_init(File* f)
{
	str_init(&f->s);
	str_init(&f->name);
	f->dot.p0 = 0;
	f->dot.p1 = 0;
	f->b = emalloc(sizeof(*f->b));
	f->bd = f->b;
}

static void
file_load(File* f)
{
	FILE *disk;
	ulong n;
	char *s;

	s = NULL;
	str2u(&s, f->name);
	disk = fopen(s, "r");
	if (disk == NULL)
		return;
	s = NULL;
	n = 0;
	do {
		s = erealloc(s, n + 4096);
		n += fread(s + n, 1, 4096, disk);
	} while (!feof(disk));
	s[n] = '\0';
	u2str(&f->s, s);
	free(s);
}

static void
file_open(int arg)
{
	file_close(-1);
	input(&f->name, w.wy / 2, GREEN("$ "));
	file_load(f);
}

static void
file_save(int arg)
{
	static char *s;
	FILE *disk;
	ulong n;

	if (arg != -1)
		f = &file[arg];
	if (!f->name.n) {
		input(&f->name, w.wy / 2, "File name: ");
		if (!f->name.n)
			return;
	}
	str2u(&s, f->name);
	disk = fopen(s, "w");
	n = str2u(&s, f->s);
	fwrite(s, n, 1, disk);
	fclose(disk);
	f->bd = f->b;
}

static void
fr_add(Frame* fr, Address a)
{
	fr_insure(fr, ++fr->n);
	fr->a[fr->n - 1] = a;
}

static void
fr_calc(void)
{
	Frame fr0;
	Posn  p0;

	for (;f->dot.p1 < fr->a[fr->cur].p0 && fr->cur; --fr->cur);
	for (;f->dot.p1 > fr->a[fr->cur].p1 && fr->cur + 1 < fr->n
		; ++fr->cur
	);
	if (!fr->n
		|| f->dot.p1 != f->dot.p0
		|| f->dot.p1 < fr->a[0].p0
		|| f->dot.p1 > fr->a[fr->n - 1].p1
		|| (fr->cur < w.wy && fr->a[0].p0)
		|| (fr->cur + w.wy > fr->n
			&& fr->a[fr->n - 1].p1 + 1 < f->s.n
		)
	) {
		/*dot + bottom addresses*/
		fr_zero(fr);
		for (p0 = f->dot.p1; p0 && f->s.s[p0 - 1] != '\n'; --p0);
		for (;p0 < f->s.n && fr->n < w.wy * 2;) {
			blind_reader(fr, p0);
			p0 = fr->a[fr->n - 1].p1 + 1;
		}
		/*top addresses*/
		for (fr_init(&fr0)
			; fr->a[0].p0 && fr->cur < w.wy
			; fr->cur += fr0.n
		) {
			for (p0 = fr->a[0].p0 - 1
				; p0 && f->s.s[p0 - 1] != '\n'
				; --p0
			);
			blind_reader(&fr0, p0);
			fr_insert(fr, fr0, 0);
			fr_zero(&fr0);
		}
		fr_close(&fr0);
		for (; f->dot.p1 > fr->a[fr->cur].p1 && fr->cur < fr->n
			; ++fr->cur
		);
	}
}

static void
fr_close(Frame* fr)
{
	free(fr->a);
}

static void
fr_init(Frame* fr)
{
	fr->a = emalloc(32 * sizeof(*fr->a));
	fr->cur = 0;
	fr->n = 0;
	fr->size = 32;
	fr->a[fr->cur].p0 = 0;
	fr->a[fr->cur].p1 = 0;
}

static void
fr_insert(Frame* p, Frame q, ushort n)
{
	fr_insure(p, p->n + q.n);
	memmove(p->a + n + q.n, p->a + n, (p->n - n) * sizeof(*p->a));
	memmove(p->a + n, q.a, q.n * sizeof(*p->a));
	p->n += q.n;
}

static void
fr_insure(Frame* fr, ushort n)
{
	if (n > fr->size) {
		fr->size += n + 32;
		fr->a = erealloc(fr->a, fr->size * sizeof(*fr->a));
	}
}

static void
fr_update(void)
{
	static char stat[128], u[UTFMAX + 1];
	static char *fname, *urune;
	Rune rune;
	uint half;

	half = w.wy / 2;
	if (f->s.n) {
		fr_calc();
		printf(ED);
		blind_writer(half, fr->cur, half, half + (w.wy % 2));
	} else
		printf(ED);
	str2u(&fname, f->name.n ? f->name : Utf("-unnamed-"));
	rune = f->s.s[f->dot.p1];
	if (rune < 0x20)
		urune = ctrune[rune];
	else if (rune == 0x7f)
		urune = "del";
	else {
		r2u(u, rune);
		urune = u;
	}
	snprintf(stat, w.wx, STATUS); /*i dont care. TODO: care*/
	msg(w.wy, "%s", stat);
	curmov(curpos() + 1, half);
}

static void
fr_zero(Frame* fr)
{
	fr->n = 0;
	fr->cur = 0;
	fr->a[fr->cur].p0 = 0;
	fr->a[fr->cur].p1 = 0;
}

static void
move(int arg)
{
	switch (arg) {
	case Left:
		if (f->dot.p1)
			--f->dot.p1;
		break;
	case Right:
		if (f->dot.p1 + 1 < f->s.n)
			++f->dot.p1;
		break;
	case Up:
		if (fr->cur)
			f->dot.p1 = fr->a[fr->cur - 1].p0;
		else
			f->dot.p1 = 0;
		break;
	case Down:
		if (!fr->a[fr->cur].p1)
			return;
		if (fr->cur < fr->n - 1)
			f->dot.p1 = fr->a[fr->cur + 1].p0;
		else
			f->dot.p1 = fr->a[fr->cur].p1 - 1;
		break;
	case HalfUp:
		if (fr->cur < w.wy/2)
			f->dot.p1 = 0;
		else
			f->dot.p1 = fr->a[fr->cur - w.wy/2].p0;
		break;
	case HalfDown:
		if (!f->s.n)
			break;
		if (fr->n - fr->cur <= w.wy/2) {
			if (fr->a[fr->n - 1].p1 <= f->s.n)
				f->dot.p1 = fr->a[fr->n - 1].p0;
			else
				f->dot.p1 = f->s.n;
		} else
			f->dot.p1 = fr->a[fr->cur + w.wy/2].p0;
		break;
	case Top:
		f->dot.p1 = 0;
		break;
	case Bottom:
		if (f->s.n)
			f->dot.p1 = f->s.n - 1;
		break;
	case StartLine:
		if (fr->cur)
			for (;fr->cur > 1
				&& f->s.s[fr->a[fr->cur].p0 - 1] != '\n'
				; --fr->cur
			);
		f->dot.p1 = fr->a[fr->cur].p0;
		break;
	case EndLine:
		for (;f->dot.p1 + 1 < f->s.n && f->s.s[f->dot.p1] != '\n'
			; ++f->dot.p1
		);
		break;
	case Word:
		for (;f->dot.p1 + 1 < f->s.n && isword(f->s.s[f->dot.p1])
			; ++f->dot.p1
		);
		for (;f->dot.p1 + 1 < f->s.n && !isword(f->s.s[f->dot.p1])
			; ++f->dot.p1
		);
		break;
	case EndWord:
		move(Right);
		for (;f->dot.p1 < f->s.n && !isword(f->s.s[f->dot.p1])
			; ++f->dot.p1
		);
		for (;f->dot.p1 < f->s.n && isword(f->s.s[f->dot.p1])
			; ++f->dot.p1
		);
		move(Left);
		break;
	case PrevWord:
		move(Left);
		for (;f->dot.p1 > 0 && !isword(f->s.s[f->dot.p1])
			; --f->dot.p1
		);
		for (;f->dot.p1 > 0 && isword(f->s.s[f->dot.p1])
			; --f->dot.p1
		);
		if (f->dot.p1)
			move(Right);
		break;
	};
	f->dot.p0 = f->dot.p1;
	fr_calc();
}

static void
quit(int arg)
{
	uint i;

	for (i = 0; i < FILECOUNT; ++i)
		file_close(i);
	win_end();
	exit(arg);
}

static void
gmove(int arg)
{
	if (arg == Top) {
		move(Top);
		for (counter ? --counter : 0; counter; --counter) {
			move(EndLine);
			move(Right);
		}
		return;
	}
	for (!counter ? ++counter : 0; counter; --counter)
		move(arg);
}

static void
msg(uint line, char* fmt, ...)
{
	va_list ap;

	curmov(0, line);
	printf(EL);
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}

static void
paste(int arg)
{
	str_insert(&f->s, istr, f->dot.p0);
	buf_add(&istr, NULL, Insert, 0, 1);
}

static void
pline(int arg)
{
	ulong i, l, h, t;

	l = 1;
	t = fr->a[fr->cur].p0;
	for (i = 0; i < t; ++i)
		if (f->s.s[i] == '\n')
			++l;
	h = w.wy / 2;
	for (i = 0; i < h; ++i)
		if (h - i < l) {
			curmov(0, i);
			printf("%4lu ", h - i);
		}
	t = fr->n - fr->cur;
	for (i = h + 1; i < w.wy; ++i)
		if (i - h < t) {
			curmov(0, i);
			printf("%4lu ", i - h);
		}
	curmov(0, h);
	printf("%-4lu ", l);
	i = getc2();
	if (i != Esc)
		ungetc(i, stdin);
}

static uchar
r2u(char *s, Rune r)
{
	char* p;

	p = s;
	if (r < (1 << 7)) {
		*p++ = r;
	} else if (r < (1 << 11)) {
		*p++ = 0xc0 | (r >> 6);
		*p++ = 0x80 | (r & 0x3f);
	} else if (r < (1 << 16)) {
		*p++ = 0xe0 | (r >> 12);
		*p++ = 0x80 | ((r >> 6) & 0x3f);
		*p++ = 0x80 | (r & 0x3f);
	} else if (r < (1 << 21)) {
		*p++ = 0xf0 | (r >> 18);
		*p++ = 0x80 | ((r >> 12) & 0x3f);
		*p++ = 0x80 | ((r >> 6) & 0x3f);
		*p++ = 0x80 | (r & 0x3f);
	} else if (r < (1 << 26)) {
		*p++ = 0xf8 | (r >> 24);
		*p++ = 0x80 | ((r >> 18) & 0x3f);
		*p++ = 0x80 | ((r >> 12) & 0x3f);
		*p++ = 0x80 | ((r >> 6) & 0x3f);
		*p++ = 0x80 | (r & 0x3f);
	} else if (r < (1 << 31)) {
		*p++ = 0xfe | (r >> 30);
		*p++ = 0x80 | ((r >> 24) & 0x3f);
		*p++ = 0x80 | ((r >> 18) & 0x3f);
		*p++ = 0x80 | ((r >> 12) & 0x3f);
		*p++ = 0x80 | ((r >> 6) & 0x3f);
		*p++ = 0x80 | (r & 0x3f);
	}
	*p = '\0';
	return p - s;
}

static uint
runesiz(Rune c, ulong wx)
{
	if (c == '\n')
		die("runesiz(): newline.");
	return (c == '\t') ? w.t - (wx % w.t) : 1;
}

static void
search(int arg)
{
	Posn pos;
	Rune *r;

	f->s.s[f->s.n] = 0;
	if (arg == '/' || arg == '?') {
		str_zero(&srch);
		input(&srch, w.wy, "/");
		for (pos = 0; pos < srch.n; ++pos)
			if (srch.s[pos] == '^')
				srch.s[pos] = '\n';
	}
	if (arg == '/' || arg == 'n') {
		move(Right);
		r = Strnstr(f->s.s + f->dot.p0, srch.s, srch.n);
		if (r == NULL) {
			move(Left);
			return;
		}
	} else {
		pos = f->dot.p1;
		if (srch.s[0] == '\n' && srch.s[1] != '\n')
			move(Left);
		for (;;) {
			for (;move(Left), f->dot.p1
				&& f->s.s[f->dot.p1] != srch.s[0]
				;
			);
			if (!memcmp(f->s.s + f->dot.p1
				, srch.s , srch.n * sizeof(*srch.s)
			))
				break;
			if (!f->dot.p1) {
				f->dot.p0 = f->dot.p1 = pos;
				return;
			}
		}
		r = f->s.s + f->dot.p1;
	}
	f->dot.p0 = f->dot.p1 = r - f->s.s;
	if (srch.s[0] == '\n' && srch.s[1] != '\n')
		move(Right);
	fr_update();
}

static int
selection(int arg)
{
	Posn p0;

	if (!counter)
		++counter;
	if (arg > 0x7f) {
		arg -= 0x7f;
		goto till;
	}
	p0 = f->dot.p1 = f->dot.p0;
	switch (arg) {
	case Letter:
		p0 = f->dot.p0;
		for (;counter > 1; --counter)
			move(Right);
		break;
	case Line:
		move(StartLine);
		p0 = f->dot.p0;
		for (;counter; --counter) {
			move(EndLine);
			move(Right);
		}
		if (f->dot.p1 + 1 < f->s.n)
			move(Left);
		break;
	case Bottom:
		move(StartLine);
		move(Left);
		p0 = f->dot.p0;
		move(Bottom);
		break;
	case Top:
		p0 = 0;
		move(EndLine);
		break;
	case Word:
		p0 = f->dot.p0;
		for (;counter; --counter)
			move(EndWord);
		break;
	case Till:
		arg = getr();
		if (arg == Esc)
			return 0;
		till:
		p0 = f->dot.p0;
		for (;counter && f->dot.p1 + 1 < f->s.n; --counter)
			for (++f->dot.p1
				; f->dot.p1 + 1 < f->s.n
					&& f->s.s[f->dot.p1 + 1] != arg
				; ++f->dot.p1
			);
		if (f->s.s[f->dot.p1 + 1] != arg) {
			f->dot.p1 = f->dot.p0;
			return -1;
		}
		arg += 0x7f;
		break;
	}
	f->dot.p0 = p0;
	counter = 0;
	return arg;
}

static ulong
str2u(char** u, String s)
{
	ulong i, n;

	n = 0;
	*u = erealloc(*u, (s.n + 1) * UTFMAX);
	for (i = 0; i < s.n; ++i)
		n += r2u(*u + n, s.s[i]);
	(*u)[n] = '\0';
	return n;
}

static void
str_init(String* p)
{
	p->s = emalloc(MINSIZE * sizeof(*p->s));
	p->n = 0;
	p->size = MINSIZE;
	p->s[p->n] = '\0';
}

static void
str_close(String* p)
{
	free(p->s);
}

static void
str_dup(String* p, String q)
{
	str_zero(p);
	str_adds(p, q);
}

static void
str_zero(String* p)
{
	if (p->size > MAXEMPTY) {
		p->s = erealloc(p->s, MAXEMPTY * sizeof(*p->s));
		p->size = MAXEMPTY;
	}
	p->n = 0;
	memset(p->s, 0, p->size * sizeof(*p->s));
}

static void
str_insure(String* p, ulong n)
{
	if (p->size < n + 1) {
		p->size = n + MAXEMPTY;
		p->s = erealloc(p->s, p->size * sizeof(*p->s));
	}
}

static void
str_addr(String* p, Rune r)
{
	str_insure(p, p->n + 1);
	p->s[p->n++] = r;
	p->s[p->n] = '\0';
}

static void
str_adds(String *p, String q)
{
	str_insure(p, p->n + q.n);
	memcpy(p->s + p->n, q.s, q.n * sizeof(*q.s));
	p->n += q.n;
	p->s[p->n] = '\0';
}

static void
str_delr(String* p)
{
	if (p->n)
		p->s[--p->n] = '\0';
}

static void
str_insert(String* p, String q, Posn p0)
{
	str_insure(p, p->n + q.n);
	memmove(p->s + p0 + q.n, p->s + p0
		, (p->n - p0) * sizeof(*p->s)
	);
	memmove(p->s + p0, q.s, q.n * sizeof(*q.s));
	p->n += q.n;
	p->s[p->n] = '\0';
}

static void
str_delete(String* p, Posn p0, Posn p1)
{
	memmove(p->s + p0, p->s + p1, (p->n - p1) * sizeof(*p->s));
	p->n -= p1 - p0;
	p->s[p->n] = '\0';
}

static void
u2str(String *s, char *u)
{
	Rune r;
	uchar seq;

	str_zero(s);
	for (seq = 0; *u != '\0'; ++u) {
		if (c2r(&r, *u, &seq) == -1)
			--u;
		else if (seq == 0)
			str_addr(s, r);
	}
}

static void
undo(int arg)
{
	if (f->b->prev == NULL)
		return;
	if (arg) {
		for (;f->b->prev != NULL;)
			undo(0);
		return;
	}
	if (f->b->is.n)
		str_delete(&f->s, f->b->p0, f->b->p0 + f->b->is.n);
	if (f->b->ds.n)
		str_insert(&f->s, f->b->ds, f->b->p0);
	f->dot.p0 = f->dot.p1 = f->b->p0;
	f->b = f->b->prev;
	fr_zero(fr);
	fr_update();
}

static void
yank(int arg)
{
	Address a;

	if (!f->s.n)
		return;
	if (!arg) {
		switch (arg = getr()) {
		case 'y': arg = Line; break;
		case 'G': arg = Bottom; break;
		case 'g': arg = Top; break;
		case 'w': arg = Word; break;
		case 't': arg = Till; break;
		default:
			return;
		}
	}
	if ((arg = selection(arg)) < 0)
		return;
	a = f->dot;
	str_dup(&istr, Strn(f->s.s + a.p0, a.p1 + 1 - a.p0));
}

int
main(int argc, char* argv[])
{
	uint i;
	uchar c;

	init();
	if (argv[1]) {
		str_adds(&f->name, Utf(argv[1]));
		file_load(f);
	}
	for (;;) {
		fr_update();
		c = getr();
		for (i = 0; i < LENGTH(keys); ++i) {
			if (keys[i].key == c) {
				keys[i].func(keys[i].value);
				break;
			}
		}
	}
}
