#pragma once
#include <cstddef>

using PocketSamples = int (*)(const float*, std::size_t, void*);
using PocketCreate = void* (*)(const char*, char*, std::size_t);
using PocketGenerate = int (*)(void*, const char*, PocketSamples, void*, char*, std::size_t);
using PocketDestroy = void (*)(void*);
