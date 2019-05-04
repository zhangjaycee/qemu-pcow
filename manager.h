/***
    Image file management interfaces. (snapshot, base image ...)
    Jaycee Zhang, 20181122      <jczhang@nbjl.nankai.edu.cn>
***/

#ifdef QEMU
#include "qemu/pcow-config.h"
#include "qemu/pcow-meta.h"
#else
#include "config.h"
#include "meta.h"
#endif

/*  
    create a new image file.
    input:  
            image file name, block unit size (in KB), max size (in GB), 
            base image name (a string less than 48 Bytes).
    return value:
            0  on success
            -1 when file exists or no based image found
*/
int new_image(char *new_image_name, int block_size, unsigned long max_size, char *base_image_name);

/*  
    create a new image file.
    input:  
            image file name,
            new base image name (a string less than 48 Bytes).
    return value:
            0  on success
            -1 when file exists or no based image found
*/
int update_base_image(char *image_name, char *base_image_name);


/* 
    create a snapshot on a image file
    input:
            image name
    return value:
            0  on success
            -1 on failed
*/

int snapshot(char *image_name);



/* 
    list 
    input:
            image name
    return value:
            0  on success
            -1 on failed
*/
int list_snapshots(char *image_name);



/* 
    list 
    input:
            image name, snapshot number
    return value:
            0  on success
            -1 on failed
*/
int rollback_snapshot(char *image_name, int snapshot_no);
