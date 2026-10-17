#pragma once
/* M5OS error codes: functions return >= 0 on success, a negative ERR_* on failure. */
#define ERR_OK      0
#define ERR_NODEV  (-1)    /* no such device / name */
#define ERR_BADF   (-2)    /* bad or closed handle */
#define ERR_INVAL  (-3)    /* bad argument */
#define ERR_NOSYS  (-4)    /* operation not supported by this device */
#define ERR_BUSY   (-5)    /* resource busy / table full */
#define ERR_IO     (-6)    /* hardware error */
#define ERR_NOMEM  (-7)
#define ERR_EXIST     (-8)    /* already exists */
#define ERR_NOTDIR    (-9)
#define ERR_ISDIR     (-10)
#define ERR_NOSPC     (-11)   /* volume (or file extent table) full */
#define ERR_NAMETOOLONG (-12)
#define ERR_NOENT     (-13)   /* no such file or directory */
#define ERR_NOTEMPTY  (-14)
#define ERR_CORRUPT   (-15)   /* on-disk structures are inconsistent */
#define ERR_PERM      (-16)   /* the calling application lacks the permission */
#define ERR_NOMSG     (-17)   /* no message waiting (msg_try_receive) */
#define ERR_TIMEOUT   (-18)   /* waited, nothing happened in time */
