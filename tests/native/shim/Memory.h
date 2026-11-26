// Host-only stand-in. RakNet's BitStream.cpp and StringCompressor.cpp say
// #include <Memory.h>; in the Android build that resolves to the project's
// GlossHook wrapper, which does not exist on a PC. They only need memcpy/memset.
#include <string.h>
