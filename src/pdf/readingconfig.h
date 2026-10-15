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
    static constexpr double margin = 20;
    static constexpr double gap = 16;
};
