#pragma once
#include <cstddef>

// C ABI between the application and the optional speech backend DLL.
// The sample callback receives mono float PCM in slices of at most 4096 samples. A call with
// count 0 is a cancellation poll between model runs. Returning 0 stops generation.
using SpeechSamples = int (*)(const float*, std::size_t, void*);
using SpeechCreate = void* (*)(const char* root, int threads, char* error, std::size_t capacity);
using SpeechSampleRate = int (*)(void*);
using SpeechGenerate = int (*)(void*, const char* text, const char* voice, SpeechSamples, void*, char*, std::size_t);
using SpeechDestroy = void (*)(void*);
