#ifndef TEST_CONIO_H
#define TEST_CONIO_H
#define getch program_test_getch
#define putch program_test_putch
#define clrscr program_test_clrscr
#define gotoxy program_test_gotoxy
#define PETSCII_F1 0x85
#define PETSCII_F3 0x86
#define PETSCII_F7 0x88
#define PETSCII_F8 0x8c
char getch(void);
void putch(char c);
void clrscr(void);
void gotoxy(char x, char y);
#endif
