/***
    Image file management interfaces. (snapshot, base image ...)
    Jaycee Zhang, 20181122      <jczhang@nbjl.nankai.edu.cn>
***/
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#ifdef QEMU
#include "qemu/pcow-config.h"
#include "qemu/pcow-meta.h"
#include "qemu/pcow-manager.h"
#else
#include "config.h"
#include "meta.h"
#include "manager.h"
#endif /*QEMU*/



int new_image(char *new_image_name, int block_size, unsigned long max_size, char *base_image_name)
{
    // check if the file is already formated 
    int fd = open(new_image_name, O_CREAT|O_RDWR, 0755);
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL != s) {
        printf("New image create failed: the image %s already exist.\n", new_image_name);
        return -1;
    }
    // check if the base image exits?
    if (NULL != base_image_name) {
        int base_fd = open(base_image_name, O_RDWR, 0755);
        if (fd == -1 || NULL == read_first_super(base_fd, 0, 0)) {
            printf("New image create failed: formated based image is not exist.\n");
        }
        close(base_fd);
    }
    
    // start creating:
    s = new_super(fd, block_size * 1024, max_size * 1024 * 1024 / block_size);
    if (base_image_name != NULL) {
        memcpy(s->pm_super->base_image_name, base_image_name, 48);
    } else {
        s->pm_super->base_image_name[0] = '\0';
    }
    pm_flush(s->pm_super->base_image_name);
    pm_fence();

    free(s);
    close(fd);

    printf("New image created!\n");
    return 0;
}


int update_base_image(char *image_name, char *base_image_name)
{
    // check if the file is already formated 
    int fd = open(image_name, O_RDWR, 0755);
    if (fd < 0) {
        printf("Image's base image update failed: the image %s is not exist.\n", image_name);
        return -1;
    }
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL == s) {
        printf("Image's base image update failed: the image %s is not formatted.\n", image_name);
        return -1;
    }

    memcpy(s->pm_super->base_image_name, base_image_name, 48);
    pm_flush(s->pm_super->base_image_name);
    pm_fence();

    printf("Base image of Image file [%s] is updated to [%s]\n", image_name, s->pm_super->base_image_name);

    return 0;
}

int snapshot(char *image_name)
{
    // check if the file is already formated 
    int fd = open(image_name, O_RDWR, 0755);
    if (fd < 0) {
        printf("Create snapshot failed: the image %s is not exist.\n", image_name);
        return -1;
    }
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL == s) {
        printf("Create snapshot failed: the image %s is not formatted.\n", image_name);
        return -1;
    }

    // create a snapshot:
    s = new_super(fd, s->block_size, s->max_block_num);
    printf("Create snapshot success on [%s]!\n", image_name);
    return 0;
}

int list_snapshots(char *image_name)
{
    // check if the file is already formated 
    int fd = open(image_name, O_RDWR, 0755);
    if (fd < 0) {
        printf("Failed: the image %s is not exist.\n", image_name);
        return -1;
    }
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL == s) {
        printf("Failed: the image %s is not formatted.\n", image_name);
        return -1;
    }

    printf("==================image file: %s=====================\n", image_name);
	// print information
    int counter = 0;
	unsigned long i;
    do {
        counter++;
        printf("--- Super Block No. %d  my_off: %lu ---\n", counter, s->my_off);
        for (i = 0; i <= s->active_meta_num; i++) {
            struct Meta *m = read_meta(s, i);
            if (i % 10 == 0 || i == s->active_meta_num)
                printf("\tmetablock: [%lu] count: [%d]\n", i, m->counter);
        }
        printf("image file magic string: \n\t%c%c%c%c\n",
                        s->pm_super->magic[0], s->pm_super->magic[1], s->pm_super->magic[2], s->pm_super->magic[3]);
        printf("Super: \n\tblock_size = %d\n\tmax_block_num = %d\n\tactive_meta_num = %d\n",
                                                    s->block_size, s->max_block_num, s->active_meta_num);
        printf("\tmax_per_seg = %d\n", s->max_per_seg);
        // active meta:
        printf("Meta : \n\tcounter = %d\n", s->active_meta->counter);
        printf("\tpm_top->counter = %d\n", s->active_meta->pm_top->counter);
        printf("Backing file:\n\t%s\n", s->pm_super->base_image_name);
    } while ((s = read_next_super(s)));

    printf("==========================================================\n");
    
	return 0;
}

int rollback_snapshot(char *image_name, int snapshot_no)
{

    // check if the file is already formated 
    int fd = open(image_name, O_RDWR, 0755);
    if (fd < 0) {
        printf("Failed: the image %s is not exist.\n", image_name);
        return -1;
    }
    struct Super *s = read_first_super(fd, 0, 0);
    if (NULL == s) {
        printf("Failed: the image %s is not formatted.\n", image_name);
        return -1;
    }

    int counter = 0;
    do {
        counter++;
        if (counter == snapshot_no) {
            unsigned long tr_length = s->active_meta_off + (unsigned long)s->block_size * \
                                                                (1L + s->active_meta->pm_top->counter);
            ftruncate(fd, tr_length);
        }
    } while ((s = read_next_super(s)));
    printf("done!\n");

	return 0;
}
