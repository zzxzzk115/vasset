// Single implementation TU for the header-only miniaudio package. Both the vasset
// import pipeline (decode) and the libvultra runtime (ma_engine playback) link
// against this static library, so the implementation must live here and compile on
// every platform (including wasm, where miniaudio targets Web Audio).
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>
