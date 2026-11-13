/* Public native-module declarations keep default ELF visibility; everything
 * else is hidden by build-native.sh.  The sources include these headers again,
 * but their guards preserve the visibility attached to these declarations. */
#pragma GCC visibility push(default)
#include "gfopenlayout.h"
#include "gfopenlayout_css.h"
#include "gfopenlayout_ui.h"
#pragma GCC visibility pop
