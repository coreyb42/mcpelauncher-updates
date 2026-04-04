#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef NDEBUG
#define IF_DEBUG(x)
#else
#define IF_DEBUG(x) x
#endif