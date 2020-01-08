// Console input and output.
// Input is from the keyboard or serial port.
// Output is written to the screen and serial port.

#include "types.h"
#include "defs.h"
#include "param.h"
#include "traps.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "x86.h"

#define KEY_UP     0xe2
#define KEY_DOWN   0xe3
#define KEY_LEFT   0xe4
#define KEY_RIGHT  0xe5

#define MAX_HISTORY 1000
#define MAX_CMD_LEN 128

char cmdhistory [MAX_HISTORY][MAX_CMD_LEN];
int cmd_his_cnt = 0;
int cur_cmd_idx = 0;

static void consputc(int);
void cursor_forward();
void cursor_backward();
void insert_char(int, int);
void remove_char();
void add_history(char*);

static int panicked = 0;

static struct {
  struct spinlock lock;
  int locking;
} cons;

static void
printint(int xx, int base, int sign)
{
  static char digits[] = "0123456789abcdef";
  char buf[16];
  int i;
  uint x;

  if(sign && (sign = xx < 0))
    x = -xx;
  else
    x = xx;

  i = 0;
  do{
    buf[i++] = digits[x % base];
  }while((x /= base) != 0);

  if(sign)
    buf[i++] = '-';

  while(--i >= 0)
    consputc(buf[i]);
}
//PAGEBREAK: 50

// Print to the console. only understands %d, %x, %p, %s.
void
cprintf(char *fmt, ...)
{
  int i, c, locking;
  uint *argp;
  char *s;

  locking = cons.locking;
  if(locking)
    acquire(&cons.lock);

  if (fmt == 0)
    panic("null fmt");

  argp = (uint*)(void*)(&fmt + 1);
  for(i = 0; (c = fmt[i] & 0xff) != 0; i++){
    if(c != '%'){
      consputc(c);
      continue;
    }
    c = fmt[++i] & 0xff;
    if(c == 0)
      break;
    switch(c){
    case 'd':
      printint(*argp++, 10, 1);
      break;
    case 'x':
    case 'p':
      printint(*argp++, 16, 0);
      break;
    case 's':
      if((s = (char*)*argp++) == 0)
        s = "(null)";
      for(; *s; s++)
        consputc(*s);
      break;
    case '%':
      consputc('%');
      break;
    default:
      // Print unknown % sequence to draw attention.
      consputc('%');
      consputc(c);
      break;
    }
  }

  if(locking)
    release(&cons.lock);
}

void
panic(char *s)
{
  int i;
  uint pcs[10];

  cli();
  cons.locking = 0;
  // use lapiccpunum so that we can call panic from mycpu()
  cprintf("lapicid %d: panic: ", lapicid());
  cprintf(s);
  cprintf("\n");
  getcallerpcs(&s, pcs);
  for(i=0; i<10; i++)
    cprintf(" %p", pcs[i]);
  panicked = 1; // freeze other CPU
  for(;;)
    ;
}

//PAGEBREAK: 50
#define BACKSPACE 0x100
#define CRTPORT 0x3d4
static ushort *crt = (ushort*)P2V(0xb8000);  // CGA memory

static void
cgaputc(int c)
{
  int pos;

  // Cursor position: col + 80*row.
  outb(CRTPORT, 14);
  pos = inb(CRTPORT+1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT+1);

  if(c == '\n')
    pos += 80 - pos%80;
  else if(c == BACKSPACE){
    if(pos > 0) --pos;
  } else
    crt[pos++] = (c&0xff) | 0x0700;  // black on white

  if(pos < 0 || pos > 25*80)
    panic("pos under/overflow");

  if((pos/80) >= 24){  // Scroll up.
    memmove(crt, crt+80, sizeof(crt[0])*23*80);
    pos -= 80;
    memset(crt+pos, 0, sizeof(crt[0])*(24*80 - pos));
  }

  outb(CRTPORT, 14);
  outb(CRTPORT+1, pos>>8);
  outb(CRTPORT, 15);
  outb(CRTPORT+1, pos);
  crt[pos] = ' ' | 0x0700;
}

void
consputc(int c)
{
  if(panicked){
    cli();
    for(;;)
      ;
  }

  if(c == BACKSPACE){
    uartputc('\b'); uartputc(' '); uartputc('\b');
  } else
    uartputc(c);
  cgaputc(c);
}

// How many back in this line
int back_cnt = 0;

#define INPUT_BUF 128
struct {
  struct spinlock lock;
  char buf[INPUT_BUF];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  uint pos; // Current position = row * 80 + pos
} input;

#define C(x)  ((x)-'@')  // Control-x

void
consoleintr(int (*getc)(void))
{
  int c, doprocdump = 0;
  char buf[MAX_CMD_LEN];

  acquire(&cons.lock);
  while((c = getc()) >= 0){
    switch(c){
    case C('P'):  // Process listing.
      // procdump() locks cons.lock indirectly; invoke later
      procdump();
      /* doprocdump = 1; */
      break;
    case C('U'):  // Kill line.
      while(input.e != input.w &&
            input.buf[(input.e-1) % INPUT_BUF] != '\n'){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case C('H'): case '\x7f':  // Backspace
      if(input.e != input.w){
        input.e--;
        consputc(BACKSPACE);
      }
      break;
    case KEY_LEFT:
      if (input.pos > input.r) {
        input.pos--;
        back_cnt++;
        cursor_backward();
      }
      break;
    case KEY_RIGHT:
      if (input.pos < input.e) {
        input.pos++;
        back_cnt--;
        cursor_forward();
      }
      break;
    case KEY_UP:
      if (cur_cmd_idx > 0) {
        // Move cursor to the rightmost position
        for (int i = input.pos; i < input.e; i++) {
          cursor_forward();
        }

        // Clear input
        while (input.e > input.w) {
          input.e--;
          remove_char();
        }

        int ch;
        for (int i = 0; i < strlen(cmdhistory[cur_cmd_idx]); i++) {
          ch = cmdhistory[cur_cmd_idx][i];
          consputc(ch);
          input.buf[input.e++] = ch;
        }
        cur_cmd_idx--;
        input.pos = input.e;
      }
      break;
    case KEY_DOWN:
      if (cur_cmd_idx < cmd_his_cnt - 1) {
        for (int i = input.pos; i < input.e; i++) {
          cursor_forward();
        }

        while (input.e > input.w) {
          input.e--;
          remove_char();
        }

        int ch;
        cur_cmd_idx++;
        for (int i = 0; i < strlen(cmdhistory[cur_cmd_idx]); i++) {
          ch = cmdhistory[cur_cmd_idx][i];
          consputc(ch);
          input.buf[input.e++] = ch;
        }
        input.pos = input.e;
      }
      break;
    default:
      if(c != 0 && input.e-input.r < INPUT_BUF){
        /* c = (c == '\r') ? '\n' : c; */
        /* input.buf[input.e++ % INPUT_BUF] = c; */
        /* consputc(c); */
        /* if(c == '\n' || c == C('D') || input.e == input.r+INPUT_BUF){ */
        /*   input.w = input.e; */
        /*   input.pos = input.e; */
        /*   wakeup(&input.r); */
        /* } */
        uartputc('-');
        uartputc(c);

        c = (c == '\r') ? '\n' : c;
        if (c == '\n' || c == C('D') || input.e == input.r + INPUT_BUF) {
          input.buf[input.e++ % INPUT_BUF] = c;
          consputc(c);

          back_cnt = 0;

          for(int i = input.w, k = 0; i < input.e - 1; i++, k++){
            buf[k] = input.buf[i % INPUT_BUF];
          }
          buf[(input.e - 1 - input.w) % INPUT_BUF] = 0;

          add_history(buf);

          input.w = input.e;
          input.pos = input.e;
          wakeup(&input.r);
        } else {
          if(back_cnt == 0){

            input.buf[input.e++ % INPUT_BUF] = c;
            input.pos ++;

            consputc(c);
          } else {
            for(int k = input.e; k >= input.pos; k--){
              input.buf[(k + 1) % INPUT_BUF] = input.buf[k % INPUT_BUF];
            }

            input.buf[input.pos % INPUT_BUF] = c;

            input.e++;
            input.pos++;

            insert_char(c, back_cnt);
          }
        }
      }
      break;
    }
  }
  release(&cons.lock);
}

int
consoleread(struct inode *ip, char *dst, int n)
{
  uint target;
  int c;

  iunlock(ip);
  target = n;
  acquire(&cons.lock);
  while(n > 0){
    while(input.r == input.w){
      if(myproc()->killed){
        release(&cons.lock);
        ilock(ip);
        return -1;
      }
      sleep(&input.r, &cons.lock);
    }
    c = input.buf[input.r++ % INPUT_BUF];
    if(c == C('D')){  // EOF
      if(n < target){
        // Save ^D for next time, to make sure
        // caller gets a 0-byte result.
        input.r--;
      }
      break;
    }
    *dst++ = c;
    --n;
    if(c == '\n')
      break;
  }
  release(&cons.lock);
  ilock(ip);

  return target - n;
}

int
consolewrite(struct inode *ip, char *buf, int n)
{
  int i;

  iunlock(ip);
  acquire(&cons.lock);
  for(i = 0; i < n; i++)
    consputc(buf[i] & 0xff);
  release(&cons.lock);
  ilock(ip);

  return n;
}

void
consoleinit(void)
{
  initlock(&cons.lock, "console");

  devsw[CONSOLE].write = consolewrite;
  devsw[CONSOLE].read = consoleread;
  cons.locking = 1;

  ioapicenable(IRQ_KBD, 0);
}

void cursor_backward() {
  int pos;

  // Get current cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  // Cursor backward
  pos--;

  // Reset
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, (unsigned char)(pos & 0xff));
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, (unsigned char)((pos >> 8) & 0xff));
}

void cursor_forward() {
  int pos;

  // Get current cursor position
  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  // Cursor forward
  pos++;

  // Reset
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, (unsigned char)(pos & 0xff));
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, (unsigned char)((pos >> 8) & 0xff));
}

void insert_char(int c, int back_cnt) {
  int pos;

  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  for (int i = pos + back_cnt; i >= pos; i++) {
    crt[i + 1] = crt[i];
  }
  crt[pos] = (c & 0xff) | 0x0700;

  pos++;

  outb(CRTPORT, 14);
  outb(CRTPORT + 1, pos >> 8);
  outb(CRTPORT, 15);
  outb(CRTPORT + 1, pos);
  crt[pos + back_cnt] = ' ' | 0x0700;
}

void remove_char() {
  int pos;

  outb(CRTPORT, 14);
  pos = inb(CRTPORT + 1) << 8;
  outb(CRTPORT, 15);
  pos |= inb(CRTPORT + 1);

  pos--;

  outb(CRTPORT, 15);
  outb(CRTPORT + 1, (unsigned char)(pos & 0xff));
  outb(CRTPORT, 14);
  outb(CRTPORT + 1, (unsigned char)((pos >> 8) & 0xff));
  crt[pos] = ' ' | 0x0700;
}


/*
 * Get history cmd of index cmdidx and put it into buf
 */
int sys_history(void) {
  char *buf;
  int cmdidx;

  if (argstr(0, &buf) < 0) {
    return -1;
  }

  if (argint(1, &cmdidx) < 0) {
    return -1;
  }

  if (cmdidx >= cmd_his_cnt) {
    return -1;
  }

  if (cmdidx < 0 || cmdidx >= MAX_HISTORY) {
    return -2;
  }

  memmove(buf, cmdhistory[cmdidx], MAX_CMD_LEN * sizeof(char));
  return 0;
}

/*
 * Add a history command
 */
void add_history(char *cmd) {
  if (!cmd) {
    return;
  }

  int len = strlen(cmd);
  len = len <= MAX_CMD_LEN ? len : MAX_CMD_LEN;

  if (cmd_his_cnt < MAX_HISTORY) {
    cmd_his_cnt++;
  } else {
    for (int i = 0; i < MAX_HISTORY - 1; i++) {
      memmove(cmdhistory[i], cmdhistory[i + 1], MAX_CMD_LEN * sizeof(char));
    }
  }

  memmove(cmdhistory[cmd_his_cnt - 1], cmd, len * sizeof(char));
  cmdhistory[cmd_his_cnt - 1][len] = 0;
  cur_cmd_idx = cmd_his_cnt - 1;
}
