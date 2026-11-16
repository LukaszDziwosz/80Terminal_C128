#include "check.h"
#include <stdio.h>
#include <string.h>
#include "irc.h"

static struct irc_message m;
static char line[IRC_LINE_SIZE];
static uint8_t parse(const char *text)
{
    strcpy(line, text);
    return irc_parse(line, &m);
}
static const char *clean(const char *text)
{
    strcpy(line, text);
    irc_clean(line);
    return line;
}
int main(void)
{
    /* Source, command and parameters; the trailing one keeps its spaces. */
    assert(parse(":bob!user@host.example PRIVMSG #retro :hello  there :)"));
    assert(!strcmp(m.nick, "bob") && !strcmp(m.command, "PRIVMSG") && m.count == 2);
    assert(!strcmp(m.param[0], "#retro") && !strcmp(m.param[1], "hello  there :)"));
    assert(parse("PING :irc.example.org") && !m.nick[0] && m.count == 1);
    assert(!strcmp(m.command, "PING") && !strcmp(m.param[0], "irc.example.org"));
    assert(parse("  :srv 001 c128   :Welcome") && !strcmp(m.nick, "srv") && m.count == 2);
    assert(!strcmp(m.command, "001") && !strcmp(m.param[0], "c128") && !strcmp(m.param[1], "Welcome"));
    assert(parse(":a!b@c join #x") && !strcmp(m.command, "JOIN") && m.count == 1 && !strcmp(m.param[0], "#x"));
    assert(parse("@time=1;id=2 :n!u@h NOTICE me :tagged") && !strcmp(m.nick, "n") && !strcmp(m.param[1], "tagged"));
    assert(parse("QUIT") && m.count == 0 && !strcmp(m.command, "QUIT"));
    assert(parse(":x MODE #c +o bob") && m.count == 3 && !strcmp(m.param[2], "bob"));
    assert(parse("C 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16") && m.count == IRC_PARAMS);
    assert(!parse("") && !parse("   ") && !parse(":onlysource") && !parse("@tags"));

    /* Case mapping and names. */
    assert(irc_same("Nick[a]\\^", "nICK{A}|~") && irc_same("#Retro", "#retro"));
    assert(!irc_same("bob", "bobby") && !irc_same("bobby", "bob") && irc_same("", ""));
    assert(irc_is_channel("#a") && irc_is_channel("&a") && !irc_is_channel("bob") && !irc_is_channel(""));

    /* Formatting, colours and UTF-8. */
    assert(!strcmp(clean("\002bold\017 \0034red \00312,4both\003 plain"), "bold red both plain"));
    assert(!strcmp(clean("\0031,2x \003,y \0035"), "x ,y "));
    assert(!strcmp(clean("\004ff00aargb \004ff00aa,00ff00two"), "rgb two"));
    assert(!strcmp(clean("a\tb\001\037\035\026c\177"), "a bc"));
    assert(!strcmp(clean("caf\xc3\xa9 \xe2\x82\xac \xf0\x9f\x98\x80!"), "caf? ? ?!"));

    /* Mentions are whole words, case-insensitive. */
    assert(irc_mentions("hi C128, there", "c128") && irc_mentions("c128", "C128"));
    assert(irc_mentions("ping [bob]: hi", "{bob}") && irc_mentions("to:bob", "bob"));
    assert(!irc_mentions("c1280 xc128 c128_", "c128") && !irc_mentions("anything", ""));

    puts("IRC parser, case mapping and text tests passed");
    return 0;
}
