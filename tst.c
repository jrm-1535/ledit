

#include <stdio.h>
#include <string.h>

#include "ledit.h"

extern int main( int argv, char **argc )
{
    printf("Ledit libray minimal test\n");
    printf("Type\n");

    printf("  CTL-C to copy\n" );
    printf("  CTL-D to debug (if debug was enabled)\n" );
    printf("  CTL-E to erase from the curosr to the end of line\n");
    printf("  CTL-Q to exit\n" );
    printf("  CTL-V to paste\n");
    printf("  Right or Left arrow to navigate in the line\n");
    printf("  CTL Right or Left arrow to move cursor to next/previous seprator\n");
    printf("  Home or End to move cursor to the begining or the end of the line\n");
    printf("  SHIFT Right or Left arrow to select an area to copy\n");
    printf("  Up or Down arrow to navigate in history\n");
    printf("  Page Up or Page Down to go to the last/first entry in history\n");
    printf("  Delete ot Backspace to remove the character under/before the cursor\n");
    printf("  Insert to toggle from insert to replace mode\n");
    printf("  Tab for auto completion (not implemented in this test)\n");

    line_t *line = new_line( "> ", NULL, NULL );
    while( true ) {
        const char *text = line_edit( line );
        if ( NULL == text ) {
            break;
        }
        if ( 0 == strcmp( text, "reset") ) {
            line_forget_history( line );
        }
    }
    printf( "\nExiting\n" );
}
