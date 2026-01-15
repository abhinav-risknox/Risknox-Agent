#include "Event.h"

// Event is header-only, this file exists for build system consistency
namespace ResolutePulse {
    // Static assertions for Event structure
    static_assert(sizeof(Event::eventId) == 4, "eventId should be 32-bit");
}
