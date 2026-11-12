#ifndef TEST_CONIO_H
#define TEST_CONIO_H
#define getch program_test_getch
#define kbhit program_test_kbhit
#define putch program_test_putch
#define clrscr program_test_clrscr
#define gotoxy program_test_gotoxy
#define PETSCII_STOP 0x03
#define PETSCII_DEL 0x14
#define PETSCII_HOME 0x13
#define PETSCII_CURSOR_UP 0x91
#define PETSCII_CURSOR_DOWN 0x11
#define PETSCII_F1 0x85
#define PETSCII_F3 0x86
#define PETSCII_F5 0x87
#define PETSCII_F7 0x88
#define PETSCII_F8 0x8c
char getch(void);
char kbhit(void);
void putch(char c);
void clrscr(void);
void gotoxy(char x, char y);
#endif
