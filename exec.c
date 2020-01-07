#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "defs.h"
#include "x86.h"
#include "elf.h"

// Max number of env vars
#define MAX_ENV_VAR_CNT 100

typedef struct EnvVar {
  char key[32];
  char val[1024];
  int keylen;
  int vallen;
} EnvVar;

typedef struct EnvVars {
  // All environment variables
  EnvVar vars[MAX_ENV_VAR_CNT];
  int length;
} EnvVars;

// Global env vars
EnvVars env_vars;

int env_var_existed(char *key) {
  for (int i = 0; i < env_vars.length; i++) {
    if (!strncmp(key, env_vars.vars[i].key, strlen(key))) {
      return i;
    }
  }

  return -1;
}

/*
 * Set environment variable
 */
int sys_set_env(void) {
  char *key, *value;

  // Get env var key
  if (argstr(0, &key) < 0) {
    return -1;
  }

  // Get env var value
  if (argstr(1, &value) < 0) {
    return -1;
  }

  int index = env_var_existed(key);
  if (index == -1) {
    int keylen = strlen(key);
    int vallen = strlen(value);
    strncpy(env_vars.vars[env_vars.length].key, key, keylen);
    strncpy(env_vars.vars[env_vars.length].val, value, vallen);
    env_vars.vars[env_vars.length].keylen = keylen;
    env_vars.vars[env_vars.length].vallen = vallen;
    env_vars.length++;
  } else {
    strncpy(env_vars.vars[index].val, value, strlen(value));
  }

  return 0;
}

int sys_get_env(void) {
  char *key, *dst;

  if (argstr(0, &key) < 0) {
    return -1;
  }

  if (argstr(1, &dst) < 0) {
    return -1;
  }

  int index = env_var_existed(key);
  if (index == -1) {
    return -1;
  }

  strncpy(dst, env_vars.vars[index].val, env_vars.vars[index].vallen);
  return 0;
}

int
exec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint argc, sz, sp, ustack[3+MAXARG+1];
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pde_t *pgdir, *oldpgdir;
  struct proc *curproc = myproc();

  begin_op();

  if((ip = namei(path)) == 0){
    end_op();
    cprintf("exec: fail\n");
    return -1;
  }
  ilock(ip);
  pgdir = 0;

  // Check ELF header
  if(readi(ip, (char*)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;
  if(elf.magic != ELF_MAGIC)
    goto bad;

  if((pgdir = setupkvm()) == 0)
    goto bad;

  // Load program into memory.
  sz = 0;
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, (char*)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if((sz = allocuvm(pgdir, sz, ph.vaddr + ph.memsz)) == 0)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    if(loaduvm(pgdir, (char*)ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  // Allocate two pages at the next page boundary.
  // Make the first inaccessible.  Use the second as the user stack.
  sz = PGROUNDUP(sz);
  if((sz = allocuvm(pgdir, sz, sz + 2*PGSIZE)) == 0)
    goto bad;
  clearpteu(pgdir, (char*)(sz - 2*PGSIZE));
  sp = sz;

  // Push argument strings, prepare rest of stack in ustack.
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp = (sp - (strlen(argv[argc]) + 1)) & ~3;
    if(copyout(pgdir, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[3+argc] = sp;
  }
  ustack[3+argc] = 0;

  ustack[0] = 0xffffffff;  // fake return PC
  ustack[1] = argc;
  ustack[2] = sp - (argc+1)*4;  // argv pointer

  sp -= (3+argc+1) * 4;
  if(copyout(pgdir, sp, ustack, (3+argc+1)*4) < 0)
    goto bad;

  // Save program name for debugging.
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(curproc->name, last, sizeof(curproc->name));

  // Commit to the user image.
  oldpgdir = curproc->pgdir;
  curproc->pgdir = pgdir;
  curproc->sz = sz;
  curproc->tf->eip = elf.entry;  // main
  curproc->tf->esp = sp;
  switchuvm(curproc);
  freevm(oldpgdir);
  return 0;

 bad:
  if(pgdir)
    freevm(pgdir);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}
