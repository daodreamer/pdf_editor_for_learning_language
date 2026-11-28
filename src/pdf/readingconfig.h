#pragma once
#include <QtGlobal>

struct ReadingConfig {
    qint64 cpuBytes = 128 * 1024 * 1024;
    qint64 gpuBytes = 64 * 1024 * 1024;
    static constexpr int maxDimension = 8192;
    static constexpr int previewWidth = 480;
    static constexpr int metadataBatch = 32;
    static constexpr int metadataSliceMs = 4;
    static constexpr int neighbors = 1;
    static constexpr int viewportDelayMs = 16;
    static constexpr qint64 textBytes = 16 * 1024 * 1024;
    static constexpr int textCharacters = 65536;
    static constexpr int selectionPages = 128;
    // Overlapping selection bands are split into disjoint pieces only up to this count per page.
    static constexpr int selectionDisjointBands = 256;
    static constexpr int textDeviceSize = 1 << 24;
    static constexpr int ocrWidth = 2400;
    static constexpr qint64 ocrImageBytes = 32 * 1024 * 1024;
    static constexpr double margin = 20;
    static constexpr double gap = 16;
};

// Whole-book background OCR (docs/OCR_SELECTION.md). One page at a time, after every foreground
// request, only once the user has left the document alone for idleMs.
struct OcrBookConfig {
    static constexpr int idleMs = 1500;
    static constexpr qint64 bookBytes = 256 * 1024 * 1024;   // one document, all profiles
    static constexpr qint64 totalBytes = 1024LL * 1024 * 1024; // every document; LRU by last use
    static constexpr int profilesPerBook = 2;                 // e.g. fra and fra+eng kept side by side
    static constexpr bool enabledByDefault = true;
};
