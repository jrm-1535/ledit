
#ifndef __LEDIT_H__
#define __LEDIT_H__

// ledit is a small line editor keeping history of previously entered lines and
// providing hooks for custom word completion and custom word separators.

#include <stdbool.h>

// If insufficient the following limits can be overridden, on the make command
// line or by changing the value below and recompiling.

// Limit the longest text input
#ifndef MAX_LINE_SIZE
#define MAX_LINE_SIZE 1024
#endif

// limit the maximum number of entries in history list (infinite if set to 0).
#ifndef MAX_HISTORY_SIZE
#define MAX_HISTORY_SIZE 128
#endif

// opaque structure
typedef struct _line line_t;

// Custom word completion function. The implementaion can call line_get_segment
// to get the current line text, and when word completion is available it should
// call insert_at_cursor_in_current_line to insert the missing text..
typedef bool (*complete_f)( line_t *line );

// implementation should update cursor position in lines, depending on argument
// forward (if true, move right, else move left), to the mrext/prvious separator
// that is relevant for the line content. The return value must be true if the
// cursor was modified (with line_set_cursor or line_update_cursor) after
// finding a separator or false if no separator was founf and the cursor was
// left unchanged.
typedef bool (*hunt_sep_f)( line_t *line, bool forward );

// return a new line object that can be used for entering and editing a single
// line of text. 
extern line_t *new_line( char *prompt, complete_f cmplt, hunt_sep_f hunt );

// free an exising line object after use. History and internal buffers are 
// discarded.
extern void line_free( line_t *line );

// enter a new line or edit an existing line of text. It returns with the text
// when the 'Enter' key is pressed or with a NULL pointer if CTL-Q is entered.
// If the line of text returned by line_edit must persist it must be duplicated
// before calling again new_line or before freeing the line object, as it is
// stored inside the object. A non-empty line is stored in history where it can
// be retrived py pressing up or down arrows.
extern const char *line_edit( line_t *lines );

// helper functions, may be used to implement complete_f or mode_to_sep_f

// set and get external context necessary for the implementations of complete_f
// or hunt_sep_f functions. That context is never accessed, used or freed by
// the library and can be cast to any type that fits in a void *
extern void line_set_context( line_t *line, void *context );
extern void *line_get_context( line_t *line );

typedef enum {
    START_TO_CURSOR = 1,
    CURSOR_TO_END,
    START_TO_END
} segment_t;

// return the text segment left of cursor, right of cursor or the complete text
extern const char *line_get_segment( line_t *lines, segment_t which,
                                     size_t *length );

// insert completion text at the cursor position and move cursor after.
extern void line_insert_at_cursor( line_t *line, char *str, size_t len );

// get current cursor position, between 0 and text_length
extern int  line_get_cursor( line_t *line );

// set cursor at a given position if possible. The argment position must be in
// range [0, text length]. It returns true if the cursor has been modified, or
// false if the position is out of range.
extern bool line_set_cursor( line_t *line, int position );

// update current cursor position by adding the delta to the current position.
// delta can be positive to more cursor right or negative to move it left.
extern bool line_update_cursor( line_t *line, int delta );

// remobe all previously stored lines from the history and reset the line wo an
// empty history. This might be useful when switching to different commands
// where previous history is irrelevant.
extern void line_forget_history( line_t *line );

#endif /* __LEDIT_H__ */
