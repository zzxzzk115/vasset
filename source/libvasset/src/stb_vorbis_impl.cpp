// Single implementation TU for stb_vorbis (ogg decode in the audio importer).
// Kept out of vasset_importers.cpp so its file-scope statics/typedefs do not
// pollute that already-large TU.
#include <stb/stb_vorbis.c>
