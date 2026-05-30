#ifndef PIPELINE_MINIZ_EXPORT_H
#define PIPELINE_MINIZ_EXPORT_H

/* The submodule build normally generates this header via CMake. The local
 * Makefile links miniz statically, so no import/export decoration is needed.
 */
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif

#ifndef MINIZ_NO_EXPORT
#define MINIZ_NO_EXPORT
#endif

#endif
