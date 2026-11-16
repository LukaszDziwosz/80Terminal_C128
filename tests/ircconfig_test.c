#include "check.h"
#include <stdio.h>
#include "ircconfig.h"

int main(void)
{
    assert(irc_port("6667") == 6667 && irc_port("1") == 1 && irc_port("65535") == 65535);
    assert(!irc_port("") && !irc_port("0") && !irc_port("65536") && !irc_port("99999") && !irc_port("66a"));

    assert(irc_valid_nick("c128user") && irc_valid_nick("[a]-_9") && irc_valid_nick("abcdefghijklmnop"));
    assert(irc_valid_nick("Lukas") && irc_valid_nick("{x|y}") && irc_valid_nick("_under"));
    assert(!irc_valid_nick("") && !irc_valid_nick("9lives") && !irc_valid_nick("a b"));
    assert(!irc_valid_nick("-a") && !irc_valid_nick("a#b") && !irc_valid_nick("abcdefghijklmnopq"));
    assert(!irc_valid_nick("a~b") && !irc_valid_nick("@op"));

    assert(irc_valid_channels("") && irc_valid_channels("#retro") && irc_valid_channels("#a,&b,#c64"));
    assert(!irc_valid_channels("retro") && !irc_valid_channels("#") && !irc_valid_channels("#a,"));
    assert(!irc_valid_channels("#a,,#b") && !irc_valid_channels("#a b") && !irc_valid_channels("#a,b"));
    puts("IRC settings validation tests passed");
    return 0;
}
