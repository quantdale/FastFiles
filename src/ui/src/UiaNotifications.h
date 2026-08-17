#pragma once
// semantic-uia-core-surfaces 1.1/2.6: UIA event notifications.
//
// The notification types and the sink/router moved to the pure header
// ffui/UiaEvents.h (semantic-treemap-uia 30.x) so the treemap semantic seam
// in include/ffui can push events without a src dependency. This header
// remains for existing includers.

#include "ffui/UiaEvents.h"
