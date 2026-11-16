/* The uiirc overlay: IRC server form and chat screen.
 *
 * Plain TCP only; no adapter offers TLS. One server, the Server window and
 * up to four channels or private conversations. Each window keeps the
 * rows it shows in bank 1, so switching windows redraws from there.
 * The protocol handling follows 80Terminal_Flutter/lib/irc/irc_session.dart
 * without member lists: there is no memory for them. The server form and
 * the settings file are part of the uimain overlay (main_screen.c). */
#ifdef IRC_TEST
#include "../../tests/stubs/conio.h"
#else
#include <conio.h>
#endif
#include <string.h>
#include "ircui.h"
#include "irc.h"
#include "ircconfig.h"
#include "bank1.h"
#include "platform.h"
#include "session.h"

#if defined(__OSCAR64C__) && !defined(IRC_TEST)
#pragma code(irccode)
#pragma data(ircdata)
#pragma bss(ircbss)
#endif
#ifdef __OSCAR64C__
/* UI speed does not matter; slot space does. */
#pragma optimize(push, size, outline)
#define IRC_ENTRY __noinline
#else
#define IRC_ENTRY
#endif
#define UI_NO_PANEL
#include "draw.h"
/* The protocol helpers are only needed here, so they are part of this overlay. */
#include "../core/irc.c"

#define WINDOWS 5
/* Longest channel or nickname with a window; 29 makes a window 32 bytes. */
#define NAME_SIZE 29
#define CHAT_ROW 2
#define CHAT_ROWS 20
#define RULE_ROW 22
#define INPUT_ROW 23
#define INPUT_SIZE 200
/* A stored row: one byte of kind (high 3 bits) and prefix length, then 80
 * characters. */
#define ROW_SIZE 81
typedef char rows_fit[WINDOWS * CHAT_ROWS * ROW_SIZE <= BANK1_IRC_ROWS_END - BANK1_IRC_ROWS ? 1 : -1];
#define NONE 0xff

enum { K_MESSAGE, K_OWN, K_MENTION, K_ACTION, K_NOTICE, K_EVENT, K_ERROR };
#define W_USED 1
#define W_CHANNEL 2
#define W_JOINED 4
#define W_UNREAD 8
#define W_MENTION 16

/* The resident session buffer: the line being received, then the lines
 * waiting to be sent. */
#define rx_line ((char *)session_buffer)
#define tx_queue (session_buffer + IRC_LINE_SIZE)
#define TX_SIZE (sizeof(session_buffer) - IRC_LINE_SIZE)

/* ircbss: shared with the transfer overlay's RAM, set before use. */
struct window { char name[NAME_SIZE + 1]; uint8_t flags, head; };
typedef char window_is_32_bytes[sizeof(struct window) == 32 ? 1 : -1];
static struct window windows[WINDOWS];
static struct irc_message message;
static uint8_t row[ROW_SIZE];
static uint8_t chunk[64];
static char input[INPUT_SIZE + 1];
static char text[200];
static char nick[NAME_SIZE + 1];
static uint16_t rx_length, tx_length;
static uint8_t active, input_length, text_length;
static uint8_t rx_overflow, registered, ended, names_wanted;

static const char *const chat_keys[] = {
    "F1", "Next window", "F3", "Close", "F5", "Names", "F7", "Disconnect", 0 };
static const char *const any_key[] = { "Any key", "Main screen", 0 };

static uint8_t is_return(uint8_t key) { return key == 10 || key == 13; }
static uint8_t cancelled(uint8_t key)
{
    return key == PETSCII_STOP || key == PETSCII_F7 || key == 27;
}
static uint8_t wait_key(const struct net_backend *backend)
{
    while (!platform_key_pending()) backend->poll();
    return (uint8_t)getch();
}

/* String comparisons from the C library would be linked into the resident
 * part, which has no room: these stay in the overlay. They index instead
 * of advancing their parameters: see irc_same() in core/irc.c. */
static uint8_t equal(const char *a, const char *b)
{
    uint8_t i = 0;
    while (a[i] && a[i] == b[i]) ++i;
    return a[i] == b[i];
}
/* Whether s begins with start. */
static uint8_t begins(const char *s, const char *start)
{
    uint8_t i;
    for (i = 0; start[i]; ++i) if (s[i] != start[i]) return 0;
    return 1;
}
/* Up to max characters and a NUL. */
static void copy(char *to, const char *from, uint8_t max)
{
    uint8_t i;
    for (i = 0; i < max && from[i]; ++i) to[i] = from[i];
    to[i] = 0;
}
/* Where a window's row is in bank 1. Added up: a multiplication would
 * bring the 16-bit multiply routine into the resident part. */
static uint16_t row_at(uint8_t w, uint8_t index)
{
    uint16_t address = BANK1_IRC_ROWS;
    while (w--) address += CHAT_ROWS * ROW_SIZE;
    while (index--) address += ROW_SIZE;
    return address;
}
static uint8_t has_dot(const char *s)
{
    uint8_t i;
    for (i = 0; s[i]; ++i) if (s[i] == '.') return 1;
    return 0;
}

/* ---- Rows ---- */
static uint8_t body_attr(uint8_t kind)
{
    if (kind <= K_OWN) return UI_TEXT;
    if (kind == K_MENTION) return UI_ACTION;
    if (kind == K_ACTION) return UI_INFO;
    if (kind == K_NOTICE) return UI_GOOD;
    return kind == K_EVENT ? UI_MUTED : UI_ERROR;
}
static void draw_row(uint8_t y)
{
    uint8_t i, kind = (uint8_t)(row[0] >> 5), prefix = row[0] & 31;
    for (i = 0; i < UI_COLS; ++i) run[i] = screen_code((char)row[i + 1]);
    screen_write_run(UI_CHARS + at(0, y), run, UI_COLS);
    screen_fill_run(UI_ATTRS + at(0, y), body_attr(kind), UI_COLS);
    if (prefix && kind <= K_OWN)
        screen_fill_run(UI_ATTRS + at(0, y), kind == K_OWN ? UI_ACTION : UI_INFO, prefix);
}
static void blank_row(uint8_t kind)
{
    memset(row + 1, ' ', UI_COLS);
    row[0] = (uint8_t)(kind << 5);
}
static void draw_tabs(void)
{
    uint8_t w, col = 0, attr, flags;
    char tab[13];
    repeat(0, 1, UI_COLS, 32, UI_TEXT);
    for (w = 0; w < WINDOWS; ++w) {
        flags = windows[w].flags;
        if (!(flags & W_USED)) continue;
        attr = w == active ? UI_BAR : flags & W_MENTION ? UI_ACTION :
               flags & W_UNREAD ? UI_HEADING : UI_MUTED;
        copy(tab, windows[w].name, 12);
        col = ui_text(col, 1, " ", attr);
        col = ui_text(col, 1, tab, attr);
        col = ui_text(col, 1, " ", attr);
        col = (uint8_t)(col + 1);
    }
}
static void draw_window(void)
{
    uint8_t i, index = windows[active].head;
    for (i = 0; i < CHAT_ROWS; ++i) {
        bank1_read(row, row_at(active, index), ROW_SIZE);
        draw_row((uint8_t)(CHAT_ROW + i));
        if (++index == CHAT_ROWS) index = 0;
    }
}
/* Adds row to a window: its oldest row is replaced. */
static void append_row(uint8_t w)
{
    struct window *win = &windows[w];
    uint8_t kind = (uint8_t)(row[0] >> 5), flags = win->flags;
    bank1_write(row_at(w, win->head), row, ROW_SIZE);
    if (++win->head == CHAT_ROWS) win->head = 0;
    if (w == active) {
        screen_copy_run(UI_CHARS + at(0, CHAT_ROW), UI_CHARS + at(0, CHAT_ROW + 1), (CHAT_ROWS - 1) * UI_COLS);
        screen_copy_run(UI_ATTRS + at(0, CHAT_ROW), UI_ATTRS + at(0, CHAT_ROW + 1), (CHAT_ROWS - 1) * UI_COLS);
        draw_row(CHAT_ROW + CHAT_ROWS - 1);
    } else if (kind <= K_NOTICE) {
        /* Conversation text, not joins and parts, marks a window unread. */
        flags |= W_UNREAD;
        if (kind == K_MENTION || !(flags & W_CHANNEL)) flags |= W_MENTION;
        if (flags != win->flags) { win->flags = flags; draw_tabs(); }
    }
}
/* One message, wrapped at spaces onto as many rows as it needs. */
static void emit(uint8_t w, uint8_t kind, const char *prefix, const char *body)
{
    uint16_t from = 0;
    uint8_t col = 0, n, cut;
    blank_row(kind);
    for (n = 0; prefix[n] && col < 20; ++n) row[1 + col++] = (uint8_t)prefix[n];
    if (col) { row[0] |= col; ++col; }
    for (;;) {
        n = 0;
        while (body[from + n] && n < UI_COLS - col) ++n;
        cut = n;
        if (body[from + n]) {
            while (cut && body[from + cut] != ' ') --cut;
            if (!cut) cut = n;
        }
        memcpy(row + 1 + col, body + from, cut);
        append_row(w);
        from += cut;
        while (body[from] == ' ') ++from;
        if (!body[from]) break;
        blank_row(kind);
        col = 2;
    }
}

/* ---- Event text, built in pieces ---- */
static void add(const char *s)
{
    uint16_t i;
    for (i = 0; s[i] && text_length < sizeof(text) - 1; ++i) text[text_length++] = s[i];
    text[text_length] = 0;
}
static void say(uint8_t w, uint8_t kind, const char *a, const char *b, const char *c)
{
    text_length = 0;
    add(a);
    if (b) add(b);
    if (c) add(c);
    emit(w, kind, "", text);
}
/* Parameters from index first on, separated by spaces. */
static void add_params(uint8_t first)
{
    for (; first < message.count; ++first) {
        add(message.param[first]);
        if (first + 1 < message.count) add(" ");
    }
}

/* ---- Windows ---- */
static uint8_t find(const char *name)
{
    uint8_t w;
    for (w = 1; w < WINDOWS; ++w)
        if ((windows[w].flags & W_USED) && irc_same(windows[w].name, name)) return w;
    return NONE;
}
static void create(uint8_t w, const char *name, uint8_t flags)
{
    uint8_t i;
    strcpy(windows[w].name, name);
    windows[w].flags = flags;
    windows[w].head = 0;
    blank_row(K_EVENT);
    for (i = 0; i < CHAT_ROWS; ++i)
        bank1_write(row_at(w, i), row, ROW_SIZE);
}
/* The window of a channel or nickname, made if there is a free one. */
static uint8_t window_open(const char *name)
{
    uint8_t w = find(name);
    if (w != NONE) return w;
    if (strlen(name) > NAME_SIZE) return NONE;
    for (w = 1; w < WINDOWS; ++w) {
        if (windows[w].flags & W_USED) continue;
        create(w, name, irc_is_channel(name) ? W_USED | W_CHANNEL : W_USED);
        draw_tabs();
        return w;
    }
    return NONE;
}
static void window_select(uint8_t w)
{
    active = w;
    windows[w].flags &= (uint8_t)~(W_UNREAD | W_MENTION);
    draw_tabs();
    draw_window();
}
static void close_window(uint8_t w)
{
    windows[w].flags = 0;
    if (w == active) window_select(0);
    else draw_tabs();
}
/* Where a QUIT or NICK is shown: without member lists the channel is only
 * certain when exactly one is joined. Otherwise the Server window. */
static uint8_t shared_window(void)
{
    uint8_t w, found = 0;
    for (w = 1; w < WINDOWS; ++w) {
        if (!(windows[w].flags & W_JOINED)) continue;
        if (found) return 0;
        found = w;
    }
    return found;
}

/* ---- Sending ---- */
/* Queues one line made of the given pieces (NULL ones are skipped). */
static uint8_t out(const char *a, const char *b, const char *c, const char *d)
{
    const char *part[4];
    const char *s;
    uint16_t at_start = tx_length;
    uint8_t i;
    part[0] = a; part[1] = b; part[2] = c; part[3] = d;
    for (i = 0; i < 4; ++i) {
        s = part[i];
        if (!s) continue;
        while (*s && tx_length < TX_SIZE - 2) tx_queue[tx_length++] = (uint8_t)*s++;
        if (*s) {
            tx_length = at_start;
            say(active, K_ERROR, "Not sent: the send queue is full.", 0, 0);
            return 0;
        }
    }
    tx_queue[tx_length++] = 13;
    tx_queue[tx_length++] = 10;
    return 1;
}
/* Returns 0 if the connection failed. */
static uint8_t flush(const struct net_backend *backend)
{
    int sent;
    if (!tx_length) return 1;
    sent = backend->write(tx_queue, tx_length);
    if (sent < 0) return 0;
    if (sent) {
        tx_length -= (uint16_t)sent;
        memmove(tx_queue, tx_queue + sent, tx_length);
    }
    return 1;
}
static void finish(const char *reason)
{
    ended = 1;
    say(active, K_ERROR, "Disconnected: ", reason, 0);
}

/* ---- Receiving ---- */
static void nick_prefix(char *to, char open_mark, const char *name, char close_mark)
{
    uint8_t n = 0;
    *to++ = open_mark;
    while (name[n] && n < IRC_NICK_SIZE) *to++ = name[n++];
    if (close_mark) *to++ = close_mark;
    *to = 0;
}
/* "/me": one row, "* nick does something". */
static void action_row(uint8_t w, const char *who, const char *body)
{
    text_length = 0;
    add("* "); add(who); add(" "); add(body);
    emit(w, K_ACTION, "", text);
}
static void receive_text(uint8_t notice)
{
    char prefix[IRC_NICK_SIZE + 3];
    char *body = message.param[message.count - 1];
    const char *target = message.param[0], *from = message.nick;
    uint8_t w, kind = notice ? K_NOTICE : K_MESSAGE;
    if (message.count < 2) return;
    if (body[0] == 1) {
        /* CTCP: only ACTION ("/me") is shown. */
        if (!begins(body + 1, "ACTION ")) return;
        body += 8;
        kind = K_ACTION;
    }
    irc_clean(body);
    if (irc_is_channel(target)) w = find(target);
    else if (notice && (!from[0] || has_dot(from) || target[0] == '*')) w = 0;
    else w = window_open(irc_same(target, nick) ? from : target);
    /* No window (five are open, or a channel not joined): the Server one. */
    if (w == NONE) w = 0;
    if (kind == K_ACTION) { action_row(w, from, body); return; }
    nick_prefix(prefix, notice ? '-' : '<', from, notice ? '-' : '>');
    if (kind == K_MESSAGE && irc_mentions(body, nick)) kind = K_MENTION;
    emit(w, kind, prefix, body);
}
static void add_reason(void)
{
    add(" (");
    add(message.param[message.count - 1]);
    add(")");
}
static void receive(void)
{
    const char *command, *from, *first, *last;
    uint8_t w, mine, failure;
    if (!irc_parse(rx_line, &message)) return;
    command = message.command;
    from = message.nick;
    first = message.count ? message.param[0] : "";
    last = message.count ? message.param[message.count - 1] : "";
    mine = irc_same(from, nick);
    if (equal(command, "PING")) { out("PONG :", last, 0, 0); return; }
    if (equal(command, "PRIVMSG")) { receive_text(0); return; }
    if (equal(command, "NOTICE")) { receive_text(1); return; }
    if (message.count) irc_clean(message.param[message.count - 1]);
    text_length = 0;
    if (equal(command, "JOIN")) {
        if (!message.count) return;
        w = mine ? window_open(first) : find(first);
        if (w == NONE) {
            if (!mine) return;
            /* Joined, but nowhere to show it: leave again. */
            out("PART ", first, 0, 0);
            say(0, K_ERROR, "No free window for ", first, ". Close one with F3 first.");
            return;
        }
        if (mine) { windows[w].flags |= W_JOINED; window_select(w); }
        say(w, K_EVENT, from, " joined ", first);
    } else if (equal(command, "PART")) {
        w = find(first);
        if (w == NONE) return;
        if (mine) {
            close_window(w);
            say(0, K_EVENT, "You left ", first, 0);
            return;
        }
        add(from); add(" left "); add(first);
        if (message.count > 1) add_reason();
        emit(w, K_EVENT, "", text);
    } else if (equal(command, "KICK")) {
        if (message.count < 2) return;
        w = find(first);
        if (w == NONE) return;
        if (irc_same(message.param[1], nick)) windows[w].flags &= (uint8_t)~W_JOINED;
        add(message.param[1]); add(" was kicked by "); add(from);
        if (message.count > 2) add_reason();
        emit(w, K_EVENT, "", text);
    } else if (equal(command, "QUIT")) {
        add(from); add(" quit");
        if (message.count) add_reason();
        emit(shared_window(), K_EVENT, "", text);
    } else if (equal(command, "NICK")) {
        if (!message.count) return;
        say(shared_window(), K_EVENT, from, " is now known as ", first);
        w = find(from);
        if (w != NONE && strlen(first) <= NAME_SIZE) { strcpy(windows[w].name, first); draw_tabs(); }
        if (mine) copy(nick, first, NAME_SIZE);
    } else if (equal(command, "TOPIC")) {
        w = find(first);
        if (w != NONE && message.count > 1) say(w, K_EVENT, from, " changed the topic to ", last);
    } else if (equal(command, "MODE")) {
        w = find(first);
        if (w == NONE) return;
        add(from); add(" sets mode "); add_params(1);
        emit(w, K_EVENT, "", text);
    } else if (equal(command, "ERROR")) {
        finish(message.count ? last : "the server closed the connection.");
    } else if (equal(command, "001")) {
        if (!message.count) return;
        copy(nick, first, NAME_SIZE);
        registered = 1;
        say(0, K_EVENT, last, 0, 0);
        if (irc_config.channels[0]) out("JOIN ", irc_config.channels, 0, 0);
    } else if (equal(command, "332")) {
        if (message.count < 3) return;
        w = find(message.param[1]);
        if (w != NONE) say(w, K_EVENT, "Topic: ", last, 0);
    } else if (equal(command, "353")) {
        /* Names arrive on every join too: only shown when asked for. */
        if (!names_wanted || message.count < 4) return;
        w = find(message.param[2]);
        say(w == NONE ? 0 : w, K_EVENT, "Names: ", last, 0);
    } else if (equal(command, "366")) {
        names_wanted = 0;
    } else if (equal(command, "331") || equal(command, "333")) {
        /* No topic set; who set the topic. */
    } else if (equal(command, "433") && !registered) {
        /* Nickname in use while registering: try it with an underscore. */
        w = (uint8_t)strlen(nick);
        if (w >= IRC_NICK_SIZE) { finish("that nickname is in use."); return; }
        nick[w] = '_'; nick[w + 1] = 0;
        say(0, K_EVENT, "Nickname in use, trying ", nick, 0);
        out("NICK ", nick, 0, 0);
    } else {
        /* Other numerics and commands: the Server window. 400 and above are
         * errors (422 only says there is no message of the day). */
        failure = command[0] >= '4' && command[0] <= '9' && !equal(command, "422");
        if (command[0] >= '0' && command[0] <= '9') add_params(1);
        else { add(command); add(" "); add_params(0); }
        emit(0, failure ? K_ERROR : K_EVENT, "", text);
        if (failure && active) emit(active, K_ERROR, "", text);
        /* text is what finish() builds its row in: give it a fixed reason. */
        if (failure && !registered) finish("the server refused the registration.");
    }
}
static void receive_bytes(uint8_t count)
{
    uint8_t i, c;
    for (i = 0; i < count; ++i) {
        c = chunk[i];
        if (c == 13 || c == 10) {
            rx_line[rx_length] = 0;
            if (rx_length && !rx_overflow) receive();
            rx_length = 0; rx_overflow = 0;
        } else if (rx_length < IRC_LINE_SIZE - 1) rx_line[rx_length++] = (char)c;
        else rx_overflow = 1;
    }
}

/* ---- Input ---- */
static void draw_input(void)
{
    uint8_t start = input_length > 77 ? (uint8_t)(input_length - 77) : 0;
    ui_text_field(2, INPUT_ROW, 78, input + start, UI_HEADING);
    ui_text_field((uint8_t)(2 + input_length - start), INPUT_ROW, 1, "", UI_BAR);
}
static void usage(const char *text_)
{
    say(active, K_ERROR, text_, 0, 0);
}
/* A message or action from this user, shown in the target's window. */
static void send_text(const char *target, const char *body, uint8_t action)
{
    char prefix[IRC_NICK_SIZE + 3];
    uint8_t w = irc_is_channel(target) ? find(target) : window_open(target);
    if (w == NONE || ((windows[w].flags & W_CHANNEL) && !(windows[w].flags & W_JOINED))) {
        usage(irc_is_channel(target) ? "Join that channel first: /join #channel"
                                     : "No free window. Close one with F3 first.");
        return;
    }
    if (!out("PRIVMSG ", target, action ? " :\001ACTION " : " :", body)) return;
    if (action) {
        /* The CTCP end marker follows the text, before the line ending. */
        tx_queue[tx_length - 2] = 1; tx_queue[tx_length - 1] = 13; tx_queue[tx_length++] = 10;
        action_row(w, nick, body);
        return;
    }
    nick_prefix(prefix, '<', nick, '>');
    emit(w, K_OWN, prefix, body);
    if (w != active) window_select(w);
}
static void command_line(char *line)
{
    char *argument = line;
    uint8_t is_channel;
    while (*argument && *argument != ' ') {
        if (*argument >= 'A' && *argument <= 'Z') *argument = (char)(*argument + 32);
        ++argument;
    }
    if (*argument) *argument++ = 0;
    while (*argument == ' ') ++argument;
    is_channel = irc_is_channel(argument[0] ? argument : windows[active].name);
    if (equal(line, "join")) {
        if (irc_is_channel(argument)) out("JOIN ", argument, 0, 0);
        else usage("Use /join #channel [key]");
    } else if (equal(line, "part")) {
        if (is_channel) out("PART ", argument[0] ? argument : windows[active].name, 0, 0);
        else usage("Use /part #channel");
    } else if (equal(line, "names")) {
        if (is_channel) { names_wanted = 1; out("NAMES ", argument[0] ? argument : windows[active].name, 0, 0); }
        else usage("Use /names #channel");
    } else if (equal(line, "nick")) {
        if (argument[0]) out("NICK ", argument, 0, 0);
        else usage("Use /nick name");
    } else if (equal(line, "msg")) {
        line = argument;
        while (*line && *line != ' ') ++line;
        if (!*line) { usage("Use /msg name text"); return; }
        *line++ = 0;
        send_text(argument, line, 0);
    } else if (equal(line, "query")) {
        if (!argument[0] || irc_is_channel(argument)) { usage("Use /query name"); return; }
        is_channel = window_open(argument);
        if (is_channel == NONE) usage("No free window. Close one with F3 first.");
        else window_select(is_channel);
    } else if (equal(line, "me")) {
        if (!argument[0] || !active) usage("Use /me action, in a channel or private window");
        else send_text(windows[active].name, argument, 1);
    } else if (equal(line, "quit")) {
        ended = 2;
    } else if (equal(line, "help")) {
        say(active, K_EVENT, "/join #channel [key]  /part  /names  /nick name  /msg name text", 0, 0);
        say(active, K_EVENT, "/query name  /me action  /quit  //text sends text that starts with /", 0, 0);
    } else usage("Unknown command. /help lists the commands.");
}
static void submit(void)
{
    if (!input_length) return;
    if (!registered) usage("Not registered with the server yet.");
    else if (input[0] == '/' && input[1] != '/') command_line(input + 1);
    else if (!active) usage("Join a channel first: /join #channel, or /query name");
    else send_text(windows[active].name, input[0] == '/' ? input + 1 : input, 0);
    input_length = 0; input[0] = 0;
    draw_input();
}
static void chat_key(uint8_t key)
{
    uint8_t w;
    if (is_return(key)) { submit(); return; }
    if (key == PETSCII_F1) {
        w = active;
        do { if (++w == WINDOWS) w = 0; } while (!(windows[w].flags & W_USED));
        if (w != active) window_select(w);
    } else if (key == PETSCII_F3) {
        if (windows[active].flags & W_JOINED) out("PART ", windows[active].name, 0, 0);
        else if (active) close_window(active);
    } else if (key == PETSCII_F5) {
        if (windows[active].flags & W_JOINED) { names_wanted = 1; out("NAMES ", windows[active].name, 0, 0); }
    } else {
        if (key == PETSCII_HOME) input_length = 0;
        else if (key == PETSCII_DEL) { if (input_length) --input_length; }
        else if (key >= 32 && key <= 126 && input_length < INPUT_SIZE) input[input_length++] = (char)key;
        else return;
        input[input_length] = 0;
        draw_input();
    }
}

/* ---- The chat screen ---- */
static void chat(const struct net_backend *backend)
{
    int count, state;
    uint8_t key;
    uint16_t started;
    memset(windows, 0, sizeof(windows));
    active = 0;
    rx_length = tx_length = 0;
    rx_overflow = registered = ended = names_wanted = input_length = 0;
    input[0] = 0;
    strcpy(nick, irc_config.nick);
    ui_clear();
    ui_title("IRC");
    create(0, "Server", W_USED);
    draw_tabs();
    repeat(0, RULE_ROW, UI_COLS, SC_HLINE, UI_RIM);
    ui_text(0, INPUT_ROW, ">", UI_ACTION);
    draw_input();
    ui_keys(chat_keys);
    text_length = 0;
    add("Connecting to "); add(irc_config.host); add(":"); add(irc_config.port_text); add(" (plain IRC, no TLS)...");
    emit(0, K_EVENT, "", text);
    /* The adapter may still be finishing a close or a DHCP renewal. */
    for (;;) {
        backend->poll();
        state = backend->state();
        if (state != NET_INITIALIZING && state != NET_CONNECTING) break;
        if (platform_key_pending() && cancelled((uint8_t)getch())) return;
    }
    if (backend->connect(irc_config.host, irc_config.port) >= 0) {
        for (;;) {
            backend->poll();
            state = backend->state();
            if (state != NET_INITIALIZING && state != NET_CONNECTING) break;
            if (platform_key_pending() && cancelled((uint8_t)getch())) return;
        }
    }
    if (backend->state() != NET_CONNECTED) finish(backend->status);
    else {
        say(0, K_EVENT, "Connected. Registering as ", nick, "...");
        out("NICK ", nick, 0, 0);
        out("USER ", nick, " 0 * :80Terminal C128", 0);
    }
    while (!ended) {
        if (!flush(backend)) { finish(backend->status); break; }
        backend->poll();
        count = backend->read(chunk, sizeof(chunk));
        if (count > 0) {
            receive_bytes((uint8_t)count);
            /* The server ended it: a key now is for the summary below. */
            if (ended) break;
        } else {
            state = backend->state();
            if (count < 0 || state == NET_FAILED || state == NET_EOF) { finish(backend->status); break; }
        }
        if (!platform_key_pending()) continue;
        key = (uint8_t)getch();
        if (key == PETSCII_F7) ended = 2;
        else chat_key(key);
    }
    if (ended == 2) {
        /* The user left: say goodbye, and give the line a second to go. */
        tx_length = 0;
        out("QUIT :80Terminal C128", 0, 0, 0);
        started = platform_ticks();
        while (tx_length && flush(backend) && (uint16_t)(platform_ticks() - started) < 60) backend->poll();
        return;
    }
    ui_keys(any_key);
    wait_key(backend);
}

IRC_ENTRY void irc_run(const struct net_backend *backend)
{
    chat(backend);
    backend->close();
    while (backend->state() == NET_CLOSING) backend->poll();
}

#ifdef __OSCAR64C__
#pragma optimize(pop)
#endif
#if defined(__OSCAR64C__) && !defined(IRC_TEST)
#pragma code(code)
#pragma data(data)
#pragma bss(bss)
#endif
