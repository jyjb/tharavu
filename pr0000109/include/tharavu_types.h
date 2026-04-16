#ifndef THARAVU_TYPES_H
#define THARAVU_TYPES_H

#include <stdint.h>
#include <stddef.h>

/* Magic Bytes */
#define MAGIC_ODAT "ODAT"
#define MAGIC_OVOC "OVOC"
#define MAGIC_OVEC "OVEC"
#define VERSION_MAJOR 1
#define VERSION_MINOR 0

/* Cell Types */
#define DE_TYPE_NULL    0
#define DE_TYPE_INT32   1
#define DE_TYPE_INT64   2
#define DE_TYPE_FLOAT   3
#define DE_TYPE_STRING  4
#define DE_TYPE_BLOB    5

/* Error Codes */
#define DE_OK           0
#define DE_ERR_IO       -1
#define DE_ERR_CORRUPT  -2
#define DE_ERR_MEM      -3
#define DE_ERR_LOCK     -4
#define DE_ERR_NOTFOUND -5
#define DE_ERR_INVAL    -6

/* Limits */
#define MAX_PATH_LEN    4096
#define HEADER_SIZE     128
#define MAX_COL_NAME    64

#endif