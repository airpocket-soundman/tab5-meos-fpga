/* Native MeOS adapter for Toyoda Masashi's SL5.03. See third_party/sl/LICENSE.
 * Minimal curses-shaped canvas operations only, not an ncurses implementation. */
#include "sl_app.h"
#include <m5os/arch.h>
#include <m5os/hal_time.h>
#include <m5os/kmalloc.h>
#include <m5os/kprintf.h>
#include <m5os/mutex.h>
#include <m5os/task.h>
#include "../../drivers/lcd/fbcon.h"
#include <string.h>
#define SL_SMOKE_MAX 128
#define SL_COLS_MAX 160
#define SL_ROWS_MAX 80
#define SL_DEADLINE_US 20000000u
typedef struct {int y,x,ptrn,kind;} sl_smoke_t;
typedef struct {
    uint8_t *canvas,*previous;
    size_t cells;
    uint64_t deadline;
    int cancelled,smoke_count;
    bool screen_started;
    sl_smoke_t smoke[SL_SMOKE_MAX];
} sl_state_t;
static sl_state_t *sl_context;
static int sl_columns,sl_rows;
static mutex_t sl_lock=MUTEX_INIT;
#define main sl_source_main
#define ACCIDENT sl_accident
#define LOGO sl_logo
#define FLY sl_fly
#define C51 sl_c51
#define add_smoke sl_add_smoke
#define add_man sl_add_man
#define add_C51 sl_add_c51
#define add_D51 sl_add_d51
#define add_sl sl_add_train
#define option sl_options
#define my_mvaddstr sl_mvaddstr
#include "sl_engine.inc"
#undef main

static int sl_initscr(void) {
    memset(sl_context->canvas,' ',sl_context->cells);
    memset(sl_context->previous,' ',sl_context->cells);
    kputs("\x1b[?1049h\x1b[0m\x1b[?25l\x1b[?7l\x1b[2J\x1b[H");
    sl_context->screen_started=true;
    return OK;
}
static int sl_mvaddch(int row,int column,unsigned ch) {
    if(row<0 || column<0 || row>=sl_rows || column>=sl_columns)return ERR;
    sl_context->canvas[(size_t)row*sl_columns+column]=(uint8_t)ch;
    return OK;
}
static int sl_getch(void) {
    if(hal_time_now_us()>=sl_context->deadline)sl_context->cancelled=124;
    int last=ERR;
    for(unsigned i=0;i<64;i++) {
        int c=kconsole_getc();if(c<0)break;last=c;
        if(c==3 || c==27){sl_context->cancelled=130;break;}
    }
    return last;
}
static int sl_refresh(void) {
    unsigned emitted=0;
    for(int row=0;row<sl_rows;row++)for(int col=0;col<sl_columns;) {
        size_t at=(size_t)row*sl_columns+col;
        if(sl_context->canvas[at]==sl_context->previous[at]){col++;continue;}
        int start=col;
        do {col++;at++;}while(col<sl_columns && sl_context->canvas[at]!=sl_context->previous[at]);
        char cursor[32];int n=ksnprintf(cursor,sizeof cursor,"\x1b[%d;%dH",row+1,start+1);
        kconsole_write(cursor,(size_t)n);
        size_t count=(size_t)(col-start),offset=(size_t)row*sl_columns+start;
        kconsole_write((const char *)sl_context->canvas+offset,count);
        memcpy(sl_context->previous+offset,sl_context->canvas+offset,count);
        emitted+=(unsigned)n+(unsigned)count;
        /* The shared SSH console has a small retained-output ring. Give its
         * drain task opportunities between bursts; no remote PTY is claimed. */
        if(emitted>=512){task_yield();emitted=0;}
    }
    return OK;
}
static int sl_usleep(unsigned usec) {
    uint32_t ms=(usec+999)/1000;
    while(ms && !sl_context->cancelled) {
        uint32_t slice=ms>5?5:ms;
        task_sleep(slice);ms-=slice;sl_getch();
    }
    return 0;
}
static int sl_mvcur(int oldrow,int oldcolumn,int row,int column) {
    (void)oldrow;(void)oldcolumn;
    if(row<0 || row>=sl_rows || column<0 || column>=sl_columns)return ERR;
    char cursor[32];int n=ksnprintf(cursor,sizeof cursor,"\x1b[%d;%dH",row+1,column+1);
    kconsole_write(cursor,(size_t)n);return OK;
}
static int sl_endwin(void) {
    if(sl_context->screen_started) {
        kputs("\x1b[0m\x1b[?7h\x1b[?25h\x1b[?1049l");
        sl_context->screen_started=false;
    }
    return OK;
}
int sl_cmd_main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        if(!strcmp(argv[i],"--help") || !strcmp(argv[i],"-h")) {
            kputs("usage: sl [-aFlc] (Ctrl-C/Esc stops; maximum20 seconds)\n");return 0;
        }
        if(argv[i][0]!='-'){kputs("sl: only -a -F -l -c options are supported\n");return 1;}
        for(const char *p=argv[i]+1;*p;p++)
            if(*p!='a' && *p!='F' && *p!='l' && *p!='c') {
                kputs("sl: only -a -F -l -c options are supported\n");return 1;
            }
    }
    if(!mutex_trylock(&sl_lock)){kputs("sl: already running\n");return 1;}
    int rows=24,cols=80;fbcon_get_geometry(&rows,&cols,NULL,NULL);
    if(rows<1)rows=24;
    if(rows>SL_ROWS_MAX)rows=SL_ROWS_MAX;
    if(cols<1)cols=80;
    if(cols>SL_COLS_MAX)cols=SL_COLS_MAX;
    size_t cells=(size_t)rows*cols;
    /* One PSRAM block owns all mutable animation/canvas state; no shell-stack
     * arrays and no upstream16KiB static smoke pool. */
    sl_state_t *state=kmalloc_psram(sizeof *state+2*cells);
    if(!state){mutex_unlock(&sl_lock);kputs("sl: PSRAM allocation failed\n");return 1;}
    memset(state,0,sizeof *state);
    state->canvas=(uint8_t *)(state+1);state->previous=state->canvas+cells;state->cells=cells;
    state->deadline=hal_time_now_us()+SL_DEADLINE_US;
    sl_context=state;sl_rows=rows;sl_columns=cols;
    ACCIDENT=LOGO=FLY=C51=0;
    (void)sl_source_main(argc,argv);
    sl_endwin();
    int result=state->cancelled;
    sl_context=NULL;kfree(state);mutex_unlock(&sl_lock);
    return result;
}
