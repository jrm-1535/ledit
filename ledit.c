
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <malloc.h>
#include <assert.h>

#include <unistd.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <termios.h>

#include "ledit.h"

// interface to a small line editor keeping history of previously entered line

typedef struct _history {
    struct _history *previous,      // previous in list
                    *next;          // next in list
    char            *data;          // actual text
    bool            saved;          // temprarily stored or saved
} history_t;

struct _line {
    char            buffer[ MAX_LINE_SIZE ];
    void            *context;       // available to cmplt if needed
    history_t       *head,          // the head (oldest entry in history)
                    *tail,          // tail, yougest entry in history, if any
                    *source;        // source of current data, if any

    char            *clipboard;     // text copied from any source
    char            *prompt;        // prompt before text
    int             prompt_sz,      // prompt character count
                    history_count,  // number of entries in history
                    sel_beg,        // clipboard selection begining column
                    sel_end,        // clipbaord selection ending column
                    cursor,         // cursor position [0:end_col]
                    end_col;        // offset of terminating 0

    complete_f      cmplt;          // if not NULL, called when TAB is pressed
    hunt_sep_f      hunt;           // if not NULL, called when CTL <- or CTL ->  
                                    // is pressed. A default implementaion is
                                    // provided for separators '"', ':', ',',
                                    // ';', '.', '[', ']', '{', '}', '/'
    struct termios  original;
};

// some ASCII codes used by he editor

#define ETX 0x03        // CTL-C   used for Copy selected text
#define EOT 0x04        // CTL-D   used for line debug
#define ENQ 0x05        // CTL-E   used for Erase end of line
#define DC1 0x11        // CTL-Q   used to quit editing?
#define SYN 0x16        // CTL-V   used to paste copied text

#define ESC 0x1b
#define TAB 0x09
#define CR  0x0d

#define DEL 0x7f        // Backspace

#define CSI "\x1b["     // ESC '[' Control Sequence Introducer (VTE/ANSI)

#define COPY            ETX
#define ERASE_TO_END    ENQ
#define PASTE           SYN
#define QUIT            DC1
#define DEBUG           EOT

#ifdef LEDIT_DEBUG

#define STR(s) #s
#define VSTR(s) STR(s)

static void line_debug( line_t *line )
{
    printf("\r\n"CSI"27mPrompt \"%s\" (%d char)", line->prompt, line->prompt_sz);
    printf("\r\nBuffer \"%s\" (%d cols, limit "VSTR(MAX_LINE_SIZE)" , cursor @%d)",
                                    line->buffer, line->end_col, line->cursor);
    printf("\n\rSelection beg %d, end %d", line->sel_beg, line->sel_end);
    printf("\n\rClipboard \"%s\"", (line->clipboard) ? line->clipboard : "");
    if ( 0 == MAX_HISTORY_SIZE ) {
        printf("\n\rhistory: (current %d, no limit)",
                                        line->history_count);
    } else {
        printf("\n\rhistory: (current %d, limit "VSTR(MAX_HISTORY_SIZE)")",
                                                line->history_count);
    }

    if ( line->tail ) {
        history_t *next = NULL;
        char *header = "tail";
        bool source_inside = false;
        for ( history_t *h = line->tail; h; h = h->previous ) {
            printf("\n\r%s \"%s\" (%s)", header, h->data,
                                         (h->saved) ? "saved" : "temporary");
            if (line->source == h) {
                printf(" SOURCE");
                source_inside = true;
            }
            if ( h->next != next ) {
                printf( " => BROKEN next link");
            }
            next = h;
            header = "    ";
        }
        if ( ! source_inside ) {
            printf("\n\rSource OUTSIDE history");
        }
    } else {
        printf(" empty");
        if (line->source) {
            printf(" DANGLING source");
        }
    }
    if ( line->source ) {
        printf("\n\rSource \"%s\"", line->source->data);
    } else {
        printf("\n\rNo source defined");
    }
    printf( "\n\r%s"CSI"K%s"CSI"%dG", line->prompt, line->buffer,
                                      1 + line->cursor + line->prompt_sz );
    fflush(stdout);
}
#endif

static void set_term_raw_mode( struct termios *original )
{
    if (tcgetattr(STDIN_FILENO, original) != 0) {
        perror("unable to get terminal mode");
        exit( 1 );
    }
    struct termios tcattr = *original;

    tcattr.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
    tcattr.c_oflag &= ~OPOST;
    tcattr.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tcattr.c_cflag &= ~(CSIZE | PARENB);
    tcattr.c_cflag |= CS8;

    if (tcsetattr(STDIN_FILENO, TCSANOW, &tcattr) != 0) {
        perror("unable to set terminal mode");
        exit ( 1 );
    }
}

static void reset_term( struct termios *original )
{
    if (tcsetattr(STDIN_FILENO, TCSANOW, original) != 0) {
        perror("unable to reset terminal mode");
        exit( 2 );
    }
}

static void no_memory( line_t *line, const char *s )
{
    printf("\n\rNo memory for %s allocation, aborting\n", s );
    if ( NULL != line ) {
        reset_term( &line->original );
    }
    exit( 1) ;
}

extern line_t *new_line( char *prompt, complete_f cmplt, hunt_sep_f hunt )
{
    line_t *line = malloc( sizeof(line_t) );
    if ( NULL == line ) {
        no_memory( NULL, "line" );
    }
    memset( line, 0, sizeof(line_t) );
    line->prompt = prompt;
    line->prompt_sz = strlen( prompt );
    line->cmplt = cmplt;
    line->hunt = hunt;
    return line;
}

extern void line_free( line_t *line )
{
    history_t *sp = line->tail;
    while ( sp ) {
        history_t *to_free = sp;
        sp = sp->previous;
        free( to_free->data );
        free( to_free );
    }
    if ( NULL != line->clipboard ) {
        free( line->clipboard );
    }
    free( line );
}

extern void line_set_context( line_t *line, void *context )
{
    assert( line );
    line->context = context;
}

extern void *line_get_context( line_t *line )
{
    assert( line );
    return line->context;
}

extern const char *line_get_segment( line_t *line, segment_t which,
                                     size_t *length )
{
    const char *s = NULL;
    switch( which ) {
    case START_TO_CURSOR:
        s = line->buffer;
        *length = line->cursor;
        break;
    case CURSOR_TO_END:
        s = &line->buffer[line->cursor];
        *length = line->end_col-line->cursor;
        break;
    case START_TO_END:
        s = line->buffer;
        *length = line->end_col;
    default:
        break;
    }
    return s;
}

// ============================ line editor ===================================

static bool is_line_modified( line_t *line )
{
    if ( line->end_col > 0 ) {          // line is not empty
        if ( NULL == line->source ) {   // if no source
            return true;                // it is modified
        }
        if ( 0 != strcmp( line->buffer, line->source->data ) ) {
            return true;                // has a source but now it is different
        }
    }
    return false;           // always consider an empty line as not modified
}

static void line_discard_temporarily_saved_texts( line_t *line )
{
    history_t *previous = NULL;
    for ( history_t *h = line->tail; NULL != h; h = previous ) {
        if ( h->saved ) {       // no more temporary stored to discard
            break;
        }
        previous = h->previous;
        if ( NULL == h->next ) {    // was at the history bottom
            line->tail = previous;
            if ( NULL != previous ) {
                previous->next = NULL;
            }
        } else {                    // not at the history bottom
            h->next->previous = previous;
        }

        if ( previous ) {
            previous->next = h->next;
        }
        --line->history_count;
        free( h->data );
        free( h );
    }
}

static void line_push_history( line_t *line, history_t *h )
{
    if ( 0 != MAX_HISTORY_SIZE && MAX_HISTORY_SIZE == line->history_count ) {
        history_t *to_discard = line->head;
        line->head = line->head->next;
        line->head->previous = NULL;
        --line->history_count;
        free(to_discard->data);
        free(to_discard);
    }
    h->previous = line->tail;
    h->next = NULL;
    if ( line->tail ) {
        line->tail->next = h;
    } else {
        line->head = h;
    }
    line->tail = h;
    ++line->history_count;
}

static bool line_recycle_history_entry( line_t *line )
{
    for ( history_t *h = line->tail; NULL != h; h = h->previous ) {
        if ( 0 == strcmp( line->buffer, h->data ) ) {
            // first remove h from the history list
            if ( h->previous ) {
                h->previous->next = h->next;
            }
            if ( h->next ) {
                h->next->previous = h->previous;
            }
            if ( line->head == h ) {
                line->head = h->next;
            }
            if ( line->tail == h ) {
                line->tail = h->previous;
            }
            --line->history_count;
            // then add as first entry in the list
            line_push_history( line, h );

            return true;
        }
    }
    return false;
}

extern void line_forget_history( line_t *line )
{
    history_t *prev = NULL;
    for ( history_t *h = line->tail; NULL != h; h = prev ) {
        prev = h->previous;
        free(h->data);
        free(h);
    }
    line->tail = line->head = NULL;
    line->history_count = 0;
}

static void line_store( line_t *line, bool save )
{
    if ( save ) {   // no modification allowed at that point, clean up history
        line_discard_temporarily_saved_texts( line );
        line->source = NULL;   // no previous source of text
    }

    if ( 0 == line->end_col ) {
        return;                     // no text to push in history
    }

    if ( line_recycle_history_entry( line ) ) {
        return;                     // was laready in history
    }

    // not yet in history
    history_t *new_history_entry = malloc( sizeof( history_t ) );
    if ( NULL == new_history_entry ) {
        no_memory( line, "history" );
    }
    char *data = strdup( line->buffer );
    if ( NULL == data ) {
        no_memory( line, "text data" );
    }
    new_history_entry->data = data;
    new_history_entry->saved = save;

    line_push_history( line, new_history_entry );
}

#define SOURCE_BUFFER_LEN  64
typedef struct {
    char   buffer[ SOURCE_BUFFER_LEN ];
    fd_set fds;
    int    offset, length;
} char_source_t;

typedef char (*get_char_fct)( char_source_t *source );

typedef enum {
    NOP, UP_ARROW, DOWN_ARROW, RIGHT_ARROW, LEFT_ARROW, HOME, END,
    CTL_RIGHT_ARROW, CTL_LEFT_ARROW, INSERT, DELETE, PAGE_UP, PAGE_DOWN,
    SHT_RIGHT_ARROW, SHT_LEFT_ARROW
} edit_operation_t;

static edit_operation_t get_edit_operation( get_char_fct get_char,
                                            char_source_t *source )
{
    char c = get_char( source );
    if ( '[' == c ) {            // CSI
        c = get_char( source );
        switch( c ) {
        case 'A': return UP_ARROW;
        case 'B': return DOWN_ARROW;
        case 'C': return RIGHT_ARROW;
        case 'D': return LEFT_ARROW;
        case 'H': return HOME;
        case 'F': return END;
        case '2': // for insert
            if ( '~' == get_char( source ) ) return INSERT;
            break;
        case '3': // for delete
            if ( '~' == get_char( source ) ) return DELETE;
            break;
        case '5':
            if ( '~' == get_char( source ) ) return PAGE_UP;
            break;
        case '6':
            if ( '~' == get_char( source ) ) return PAGE_DOWN;
            break;
        case '1': //CTL <- or CTL ->
            if ( ';' != get_char(source) ) break;
            c = get_char( source );
            if ( '2' == c ) {
                c = get_char( source );
                if ( 'D' == c ) return SHT_LEFT_ARROW;
                if ( 'C' == c ) return SHT_RIGHT_ARROW;
                break;
            }
            if ( '5' != c ) break;
            c = get_char( source );
            if ( 'D' == c ) return CTL_LEFT_ARROW;
            if ( 'C' == c ) return CTL_RIGHT_ARROW;
            break;
        }
    } else if ( 'O' == c ) {
        c = get_char( source );
        if ( 'H' == c ) return HOME;
        if ( 'F' == c ) return END;
    }
/* note that other possible escape sequences are not processed and will
   appear as regular text as if the user had typed them on the keyboard. */
    return NOP;
}

extern int line_get_cursor( line_t *line )
{
    return line->cursor;
}

extern bool line_set_cursor( line_t *line, int position )
{
    if ( 0 <= position && position <= line->end_col ) {
        line->cursor = position;
        fprintf( stdout, CSI"%dG", line->cursor + 1 + line->prompt_sz );
        return true;
    }
    return false;
}

extern bool line_update_cursor( line_t *line, int delta )
{
    int position = line->cursor + delta;
    return line_set_cursor( line, position );
}

static bool hunt_backard_for_default_separators( line_t *line )
{
    int cursor;
    for ( cursor = line->cursor; cursor > 0;  ) {
        switch ( line->buffer[ --cursor ] ) {
            default: break;
            case '\"': case ':': case ',': case ';': case'.':
            case '[': case ']': case '{': case '}': case '/':
                fprintf( stdout, CSI"%dD",
                         line->cursor - cursor );
                line->cursor = cursor;
                return true;    // cursor has been moved
        }
    }
    return false;               // cursor has not moved
}

static bool hunt_forward_for_default_separators( line_t *line )
{
    int cursor;
    for ( cursor = line->cursor; cursor < line->end_col;  ) {
        switch ( line->buffer[ ++cursor ] ) {
            default: break;
            case '\"': case ':': case ',': case ';': case '.':
            case '[': case ']': case '{': case '}': case '/':
                fprintf( stdout, CSI"%dC",
                            cursor - line->cursor );
                line->cursor = cursor;
                return true;    // cursor has been moved
        }
    }
    return false;               // cursor has not moved
}

extern void line_insert_at_cursor( line_t *line, char *str, size_t len )
{
    if ( line->cursor + len >= MAX_LINE_SIZE ) {
        // no room for the whole length in buffer: reduce len
        len = MAX_LINE_SIZE - 1 - line->cursor;
        line->end_col = line->cursor;
    } else if ( line->end_col + len >= MAX_LINE_SIZE ) {
        // room for the whole length but not for the following segment
        // insert str and truncate at end of line
        line->end_col = MAX_LINE_SIZE - 1 - len;
        line->buffer[line->end_col] = '\0';
    }
    assert( line->cursor <= line->end_col );

    if ( line->cursor < line->end_col ) {
        memmove( &(line->buffer[ line->cursor + len ]),
                 &(line->buffer[ line->cursor ]),
                 line->end_col - line->cursor );
    }
    memcpy( &line->buffer[ line->cursor ], str, len );
    line->buffer[ line->end_col + len ] = '\0';
    if ( line->end_col != line->cursor ) {
        fprintf( stdout, "%s"CSI"%dD",
                 &(line->buffer[ line->cursor ]),
                 line->end_col - line->cursor );
    } else {
        fprintf( stdout, "%s", &(line->buffer[ line->cursor ]) );
    }
    line->cursor += len;
    line->end_col += len;
}

static void unselect( line_t *line )
{
    if ( -1 != line->sel_beg ) {
        int prev_cursor = line->cursor;
        line_set_cursor( line, line->sel_beg );
        for ( int i = line->sel_beg; i < line->sel_end; ++ i ) {
            fprintf( stdout, CSI"27m%c", (line->buffer[ i ]) );
        }
        line_set_cursor( line, prev_cursor );
        line->sel_beg = line->sel_end = -1;
    }
}

static void select_right( line_t *line )
{
    if ( line->cursor < line->end_col ) {
        if ( -1 == line->sel_beg ) {
            line->sel_beg = line->cursor;
            line->sel_end = line->cursor + 1;
        } else if ( line->sel_end == line->cursor ) {
            line->sel_end = line->cursor + 1;
        }
        fprintf( stdout, CSI"7m%c", (line->buffer[ line->cursor ]) );
        ++line->cursor;
    }
}

static void select_left( line_t *line )
{
    if ( line->cursor > 0 ) {
        if ( line->sel_end == line->cursor ) {
            --line->sel_end;
            if ( line->sel_end == line->sel_beg ) {
                line->sel_beg = line->sel_end = - 1;
            }
            fprintf( stdout, CSI"1D"CSI"27m%c", (line->buffer[ line->cursor-1 ]) );
            fputs( CSI"1D", stdout );
            --line->cursor;
            return;
        }
        if ( -1 == line->sel_beg ) {
            line->sel_beg = line->cursor - 1;
            line->sel_end = line->cursor;
        } else if ( line->sel_beg == line->cursor ) {
            line->sel_beg = line->cursor - 1;
        }
        fprintf( stdout, CSI"1D"CSI"7m%c", (line->buffer[ line->cursor-1 ]) );
        fputs( CSI"1D", stdout );
        --line->cursor;
    }
}

static void move_end( line_t *line )
{
    unselect( line );
    if ( line->cursor < line->end_col ) {
        fprintf( stdout, CSI"%dC", line->end_col - line->cursor );
        line->cursor = line->end_col;
    }
}

static void move_right( line_t *line )
{
    unselect( line );
    if ( line->cursor < line->end_col ) {
        fputs( CSI"1C", stdout );
        ++line->cursor;
    }
}

static void move_home( line_t *line )
{
    unselect( line );
    if ( line->cursor > 0 ) {
        fprintf( stdout, CSI"%dD", line->cursor );
        line->cursor = 0;
    }
}

static void move_left( line_t *line )
{
    unselect( line );
    if ( line->cursor > 0 ) {
        fputs( CSI"1D", stdout );
        --line->cursor;
    }
}

typedef enum {
    ONE_STEP,
    ONE_PAGE
} move_t;

static void move_up( line_t *line, move_t move )
{
    unselect( line );
    if ( NULL == line->tail ) {     // history is empty
        return;                     // no change is possible
    }

    history_t *cur = line->source;  // source of current text
    if ( NULL == cur ) {            // first time moving up in history
        line->source = line->tail;  // start from bottom (cannot be NULL here)
        if ( is_line_modified( line ) ) {   // if line is not empty,
            line_store( line, false );      // store text temporarily
        }
    } else {                        // or move (at least) ONE_STEP in history
        if ( NULL == cur->previous ) {
            return;                 // if not possible, stay at current
        }
        if ( is_line_modified( line ) ) {   // if text is modified
            line->source = line->tail;      // back to tail before storing
            line_store( line, false );      // temporarily modified text
        } else {                    // was not modified from source, keep going
            line->source = cur->previous;
        }
    }

    if ( ONE_PAGE == move ) {       // move up to the oldest entry in history
        line->source = line->head;
//        while ( line->source->previous ) {
//            line->source = line->source->previous;
//        }
    }

    strcpy( line->buffer, line->source->data );
    line->end_col = strlen( line->buffer );
    line->cursor = line->end_col;
    fprintf( stdout, "\r%s"CSI"K%s", line->prompt, line->buffer );
}

static void move_down( line_t *line, move_t move )
{
    unselect( line );
    if ( is_line_modified( line ) ) {
        line_store( line, false );
        line->source = line->tail;  // back to bottom
    }

    if ( NULL == line->source ) {   // no source, cannot go down
        return;
    }
    line->source = line->source->next;// move (at least) ONE_STEP in history
    if ( ONE_PAGE == move ) {       // move down to new empty line.
        line->source = NULL;
    }

    if ( line->source ) {
        strcpy( line->buffer, line->source->data );
        line->end_col = strlen( line->buffer );
        line->cursor = line->end_col;
     } else {
        line->buffer[0] = '\0';
        line->end_col = line->cursor = 0;
    }
    fprintf( stdout, "\r%s"CSI"K%s", line->prompt, line->buffer );
}

static void delete_at_cursor( line_t *line )
{
    unselect( line );
    if ( line->cursor == line->end_col ) {
        return;
    }
    memmove( &(line->buffer[ line->cursor ]),
         &(line->buffer[ line->cursor + 1 ]), line->end_col - line->cursor );
    fprintf( stdout, "%s"CSI"K", &(line->buffer[ line->cursor ] ) );
    --line->end_col;
    if ( line->end_col != line->cursor )
        fprintf( stdout, CSI"%dD", line->end_col - line->cursor );
}

static void delete_before_cursor( line_t *line )
{
    if ( 0 == line->cursor ) {
        return;
    }
    memmove( &(line->buffer[ line->cursor - 1 ]),
             &(line->buffer[ line->cursor ]),
             1 + line->end_col - line->cursor );
    fprintf( stdout, CSI"1D%s"CSI"K", &(line->buffer[ line->cursor - 1 ] ) );
    --line->cursor;
    --line->end_col;
    if ( line->end_col != line->cursor )
        fprintf( stdout, CSI"%dD", line->end_col - line->cursor );
}

static void copy_to_clipboard( line_t *line )
{
    if ( -1 == line->sel_beg ) {
        return;
    }
    if ( NULL != line->clipboard ) {
        free( line->clipboard );
    }
    size_t len = line->sel_end - line->sel_beg;
    line->clipboard = malloc( 1 + len );
    
    if ( NULL == line->clipboard ) {
        unselect( line );
        return;     // no room;
    }
    memcpy( line->clipboard, &line->buffer[ line->sel_beg ], len );
    line->clipboard[len] = '\0';
}

static void paste_to_clipboard( line_t *line )
{
    if ( NULL != line->clipboard ) {
        line_insert_at_cursor( line, line->clipboard, strlen(line->clipboard) );
    }
}

static void erase_to_end_of_line( line_t *line )
{
    assert( line->cursor >= 0 && line->cursor <= line->end_col );
    fputs( CSI"0K", stdout );
    line->end_col = line->cursor;
    line->buffer[line->cursor] = '\0';
}

/* enter a new line or edit a previously entered line.
   Editing stops when the key Enter is pressed or when CTL-C is pressed.
   The return value is 0 if Enter was pressed and the entered line is the tail
   line in the history, or -1 if CTL-C was pressed. */
static int edit( line_t *line, get_char_fct get_char, char_source_t *source )
{
    fprintf( stdout, "\r%s"CSI"5 q", line->prompt );
    bool insert = true;         // default to insert mode blinking bar |
    fflush(stdout);

/* ANSI cursor/line control sequences
    Esc[nC move cursor right n positions
    Esc[nD move cursor left n positions
    Esc[K  clear line from cursor right (also Esc[0K)
    Esc[1K clear line from cursor left
    Esc[2K clear the whole line
*/
    
    while ( true ) {
        char c = get_char( source );
        switch ( c ) {
        case QUIT:
            unselect( line );
            return -1; // CTL-C
        case CR:
            unselect( line );
            line_store( line, true );   // commit to history
            fputs( "\r\n", stdout );
            fflush(stdout);
            return 0;
        case COPY:
            copy_to_clipboard( line );
            break;
        case ERASE_TO_END:
            erase_to_end_of_line( line );
            break;
        case PASTE:
            paste_to_clipboard( line );
            break;
#ifdef LEDIT_DEBUG
        case DEBUG:
            line_debug( line );
            break;
#endif
        case TAB:                       // auto complete request
            unselect( line );
            if ( line->cmplt( line ) ) {
                fprintf( stdout, "\r%s"CSI"K%s"CSI"%dG",
                         line->prompt, line->buffer,
                         1 + line->cursor + line->prompt_sz );
                fflush(stdout);
            }
            break;
        case ESC:
            switch( get_edit_operation( get_char, source ) ) {
            default: break;
            case UP_ARROW: // goes to the previous entry in history
                move_up( line, ONE_STEP );
                break;
            case PAGE_UP:  // goes to the first entry in history (UP_ARROW)
                move_up( line, ONE_PAGE );
                break;
            case DOWN_ARROW: // goes to the next entry in history
                move_down( line, ONE_STEP );
                break;
            case PAGE_DOWN:  // goes to the last entry in history (DOWN_ARROW)
                move_down( line, ONE_PAGE );
                break;
            case LEFT_ARROW:  // moves cursor to previous character
                move_left( line );
                break;
            case SHT_RIGHT_ARROW:
                select_right( line );
                break;
            case SHT_LEFT_ARROW:
                select_left( line );
                break;
            case RIGHT_ARROW: // moves cursor to next character
                move_right( line );
                break;
            case CTL_LEFT_ARROW: // moves cursor to previous separator
                if ( NULL != line->hunt ) {
                    if ( line->hunt( line, false ) ) {
                        break;
                    }
                } else if ( hunt_backard_for_default_separators( line ) ) {
                    break;
                } // no sep found (next line is neccessary to silence gcc)
                // fall through
            case HOME:    // moves cursor to the first character (LEFT_ARROW)
                move_home( line );
                break;
            case CTL_RIGHT_ARROW: // moves cursor to next separator:
                if ( NULL != line->hunt ) {
                    if ( line->hunt( line, true ) ) {
                        break;
                    }
                } else if ( hunt_forward_for_default_separators( line ) ) {
                    break;
                } // else falls though (next line is necessary to silence gcc)
                // fall through
            case END:  // moves cursor to the right of the last character (RIGHT_ARROW)
                move_end( line );
                break;
            case INSERT:
                insert = ! insert;
                if ( insert ) {
                    fprintf( stdout, CSI"5 q" );    // insert mode blinking |
                } else {
                    fprintf( stdout, CSI"3 q" );    // replace mode blinking _
                }
                break;
            case DELETE: // delete the character under the cursor
                delete_at_cursor( line );
                break;
            }
            break;

        case DEL: // delete left char
            delete_before_cursor( line );
            break;

        default:
            unselect( line );
            if ( ! insert ) {
                if ( line->cursor < line->end_col ) {
                    line->buffer[ line->cursor ++ ] = c;
                    fprintf( stdout, "%c", line->buffer[ line->cursor - 1 ] );
                    break;
                }
            }
            if ( line->end_col >= MAX_LINE_SIZE-1 ) break;

            assert( line->cursor <= line->end_col );
            memmove( &(line->buffer[ line->cursor + 1 ]),
                     &(line->buffer[ line->cursor ]),
                     1 + line->end_col - line->cursor );
            line->buffer[ line->cursor ++ ] = c;
            ++ line->end_col;

            if ( line->end_col != line->cursor ) {
                fprintf( stdout, "%s"CSI"%dD",
                         &(line->buffer[ line->cursor - 1 ]),
                         line->end_col - line->cursor );
            } else {
                fprintf( stdout, "%s", &(line->buffer[ line->cursor - 1 ]) );
            }
            break;
        }
        fflush(stdout);
    }
}

static char get_char( char_source_t *source )
{
    if ( source->offset == source->length ) {
        if ( 1 != select(STDIN_FILENO + 1, &source->fds, NULL, NULL, NULL) ) {
            return 0;
        }

        source->length = read( STDIN_FILENO, source->buffer, SOURCE_BUFFER_LEN );
        source->offset = 0;
    }
    return source->buffer[ source->offset++ ];
}

static inline void empty_line( line_t *line )
{
    line->buffer[0] = 0;
    line->cursor = 0;
    line->end_col = 0;
    line->sel_beg = -1;
    line->sel_end = -1;
}

extern const char *line_edit( line_t *line )
{
    fd_set in_fds;
    FD_ZERO(&in_fds);
    FD_SET(STDIN_FILENO, &in_fds);

    empty_line( line );
    char_source_t source;
    source.fds = in_fds;
    source.offset = source.length = 0;

    set_term_raw_mode( &line->original );
    int res = edit( line, get_char, &source );
    reset_term( &line->original );

    if ( -1 == res )
        return NULL;
#if 0
    printf( "\r\nHistory:\r\n");
    history_t *sp = line->tail;
    while ( sp ) {
        printf("[%s]\r\n", sp->data );
        sp = sp->previous;
    }
    printf("---------------------------\r\n");
#endif
    if ( line->tail ) {
        return (const char *)line->tail->data;
    }
    return "";
}
