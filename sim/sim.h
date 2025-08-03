#define ED "\x1b[2J"
#define EL "\x1b[2K"
#define CSI "\x1b["

typedef unsigned char  uchar;
typedef unsigned int   uint;
typedef unsigned long  ulong;
typedef unsigned short ushort;

typedef struct {
	ushort wx, wy;
	ushort t;
} Window;

extern void  die(char* fmt, ...);
extern void* emalloc(ulong n);
extern void* erealloc(void* p, ulong n);
extern void  resize(void);
extern void  win_end(void);
extern void  win_init(Window* w);
extern void  win_query(Window* w);
