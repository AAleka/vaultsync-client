#include <vaultsync/vaultsync.h>

const char* vs_version(void)
{
    return "0.1.0";
}

const char* vs_strerr(int err)
{
    switch (err)
    {
        case VS_OK:
            return "Success";
        case VS_ERR_NOMEM:
            return "malloc failed";
        case VS_ERR_INVALID:
            return "bad argument";
        case VS_ERR_IO:
            return "reading or writing failed";
        case VS_ERR_NOT_FOUND:
            return "file, object or snapshot doesn't exist";
        case VS_ERR_CORRUPT:
            return "data on disk is damaged or malformed";
        case VS_ERR_CANCELLED:
            return "user pressed cancel";
        default:
            return "unknown error code";
    }
}