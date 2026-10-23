#pragma once
#define OK 0
#define ERR (-1)
#define TRUE 1
#define FALSE 0
#define stdscr ((void *)0)
#define COLS sl_columns
#define LINES sl_rows
#define SIGINT 2
#define SIG_IGN 0
#define signal(a,b) ((void)(a),(void)(b))
#define noecho() ((void)0)
#define curs_set(n) ((void)(n))
#define nodelay(a,b) ((void)(a),(void)(b))
#define leaveok(a,b) ((void)(a),(void)(b))
#define scrollok(a,b) ((void)(a),(void)(b))
#define initscr sl_initscr
#define mvaddch sl_mvaddch
#define getch sl_getch
#define refresh sl_refresh
#define usleep sl_usleep
#define mvcur sl_mvcur
#define endwin sl_endwin
static int sl_initscr(void);
static int sl_mvaddch(int row,int column,unsigned ch);
static int sl_getch(void);
static int sl_refresh(void);
static int sl_usleep(unsigned usec);
static int sl_mvcur(int oldrow,int oldcolumn,int row,int column);
static int sl_endwin(void);
