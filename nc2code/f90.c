#include <sds.h>

const char *f90_type_str(SDSType type)
{
    switch (type) {
    case SDS_I8:
    case SDS_U8:
    case SDS_I16:
    case SDS_U16:
    case SDS_I32:
        return "integer";
    case SDS_FLOAT:
        return "real*4";
    case SDS_DOUBLE:
        return "real*8";
    case SDS_STRING:
        return "character(%u)";
    default:
        abort();
    }
    return NULL;
}

