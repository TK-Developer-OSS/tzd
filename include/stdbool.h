/* TzD stdbool.  In TzD (.d) bool/true/false are keywords already; the
   macros give Managed C (.c) the same spelling.  */
#ifndef _TZD_STDBOOL_H
#define _TZD_STDBOOL_H
#define bool _Bool
#define true 1
#define false 0
#define __bool_true_false_are_defined 1
#endif
