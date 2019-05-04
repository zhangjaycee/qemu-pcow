#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "manager.h"


int main(int argc, char *argv[])
{
    if (argc < 2) {
        printf("usage: %s [option] [parasmeters...]\n\t\t use '%s help' to chechout the avaliable optaions.\n", argv[0], argv[0]);
        return 0;
    }
    if (0 == strcmp(argv[1], "help")) {
        printf( "Help:\n"
                "   Optaions:\n"
                "       create\tcreate a new image.\n"
                "       snapshot\tcreate a snapshot on an existing image.\n"
                "       rollback\trollback the image to a specific snapshot.\n"
                "       rebase\tchange the base image of an existing image.\n"
                "       info\tcheck the information of an existing image.\n");
        return 0;
    }

    if (0 == strcmp(argv[1], "create")) {
        if (argc < 5 || argc > 6) {
            printf("usage: %s create [block_size(in KB)] [max_image_size(in GB)] [new_image_name] [base_image_name]\n", argv[0]);
            return 0;
        }
        int block_size = atoi(argv[2]);
        int max_image_size = atoi(argv[3]);
        if (argc == 5) {
            new_image(argv[4], block_size, max_image_size, NULL);
            return 0; 
        } else if (argc == 6) {
            new_image(argv[4], block_size, max_image_size, argv[5]);
            return 0; 
        }
    }

    if (0 == strcmp(argv[1], "info")) {
        if (argc != 3) {
            printf("usage: %s info [target_image_name]\n", argv[0]);
            return 0;
        }
        // argc == 3:
        list_snapshots(argv[2]);
        return 0;
         
    }

    if (0 == strcmp(argv[1], "rebase")) {
        if (argc != 4) {
            printf("usage: %s rebase [target_image_name] [base_image_name]\n", argv[0]);
            return 0;
        }
        // argc == 4
        update_base_image(argv[2], argv[3]);
        return 0;
    }

    if (0 == strcmp(argv[1], "snapshot")) {
        if (argc != 3) {
            printf("usage: %s snapshot [target_image_name]\n", argv[0]);
            return 0;
        }
        // argc == 3:
        snapshot(argv[2]);
        return 0;
    }

    if (0 == strcmp(argv[1], "rollback")) {
        if (argc != 4) {
            printf("usage: %s rollback [target_image_name] [snapshot_no]\n", argv[0]);
            return 0;
        }
        // argc == 3:
        list_snapshots(argv[2]);
        rollback_snapshot(argv[2], atoi(argv[3]));
        list_snapshots(argv[2]);
        return 0;
    }


}
