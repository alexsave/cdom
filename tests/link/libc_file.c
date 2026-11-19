/* stdio.h's FILE is an incomplete type: no FILE can be made (run.sh expects this file not to compile, and
 * the error to be clang's "never completed"). */
#include <stdio.h>
FILE one;
