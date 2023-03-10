extern int Fork(void);
extern int Exec(char *, char **);
extern void Exit(int) __attribute__ ((noreturn));
extern int Wait(int *);
extern int GetPid(void);
extern int Brk(void *);
extern int Delay(int);
extern int TtyRead(int, void *, int);
extern int TtyWrite(int, void *, int);