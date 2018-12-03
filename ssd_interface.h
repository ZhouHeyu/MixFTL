/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 *
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * Description: This is a header file for ssd_interface.c.
 *
 */
#ifndef _SSD_INTERFACE_H_
#define _SSD_INTERFACE_H_

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "fast.h"
#include "pagemap.h"
#include "flash.h"
#include "type.h"


#define READ_DELAY        (0.1309/4)
#define WRITE_DELAY       (0.4059/4)
#define ERASE_DELAY       1.5 
#define GC_READ_DELAY  READ_DELAY    // gc read_delay = read delay    
#define GC_WRITE_DELAY WRITE_DELAY  // gc write_delay = write delay 
#define OOB_READ_DELAY    0.0
#define OOB_WRITE_DELAY   0.0

/***Mix SSD Time Delay Value**/
#define SLC_READ_DELAY        (0.0250)
#define SLC_WRITE_DELAY       (0.200)
#define SLC_ERASE_DELAY       1.5 
#define SLC_GC_READ_DELAY  READ_DELAY    // gc read_delay = read delay    
#define SLC_GC_WRITE_DELAY WRITE_DELAY  // gc write_delay = write delay 

#define MLC_READ_DELAY        (0.060)
#define MLC_WRITE_DELAY       (0.800)
#define MLC_ERASE_DELAY       1.5 
#define MLC_GC_READ_DELAY  READ_DELAY    // gc read_delay = read delay    
#define MLC_GC_WRITE_DELAY WRITE_DELAY  // gc write_delay = write delay

#define SLC_OOB_READ_DELAY    0.0
#define SLC_OOB_WRITE_DELAY   0.0
#define MLC_OOB_READ_DELAY    0.0
#define MLC_OOB_WRITE_DELAY   0.0
/****************************/

struct ftl_operation * ftl_op;

#define PAGE_READ     0
#define PAGE_WRITE    1
#define OOB_READ      2
#define OOB_WRITE     3
#define BLOCK_ERASE   4
#define GC_PAGE_READ  5
#define GC_PAGE_WRITE 6
/*******Mix SSD Stat Value*******/
#define SLC_PAGE_READ     7
#define SLC_PAGE_WRITE    8
#define SLC_OOB_READ      9
#define SLC_OOB_WRITE     10
#define SLC_BLOCK_ERASE   11
#define SLC_GC_PAGE_READ  12
#define SLC_GC_PAGE_WRITE 13
#define MLC_PAGE_READ     14
#define MLC_PAGE_WRITE    15
#define MLC_OOB_READ      16
#define MLC_OOB_WRITE     17
#define MLC_BLOCK_ERASE   18
#define MLC_GC_PAGE_READ  19
#define MLC_GC_PAGE_WRITE 20
/****************************/


void reset_flash_stat();
double calculate_delay_flash();
void initFlash();
void endFlash();
//add zhoujie 11-27
void Mix_initFlash();
void Mix_endFlash();

void printWearout();
void send_flash_request(int start_blk_no, int block_cnt, int operation, int mapdir_flag);
void find_real_max();
void find_real_min();
int find_min_ghost_entry();
void synchronize_disk_flash();
void find_min_cache();
int search_table(int *arr, int size, int val) ;
int find_free_pos( int *arr, int size);
double callFsim(unsigned int secno, int scount, int operation);
/********Mix SSD function******/
void reset_SLC_flash_stat();
void reset_MLC_flash_stat();
double calculate_delay_SLC_flash();
double calculate_delay_MLC_flash();

/****************************/

int write_count;
int read_count;

int flash_read_num;
int flash_write_num;
int flash_gc_read_num;
int flash_gc_write_num;
int flash_erase_num;
int flash_oob_read_num;
int flash_oob_write_num;

/*******Mix SSD Stat Value*****/
int SLC_flash_read_num;
int SLC_flash_write_num;
int SLC_flash_gc_read_num;
int SLC_flash_gc_write_num;
int SLC_flash_erase_num;
int SLC_flash_oob_read_num;
int SLC_flash_oob_write_num;

int MLC_flash_read_num;
int MLC_flash_write_num;
int MLC_flash_gc_read_num;
int MLC_flash_gc_write_num;
int MLC_flash_erase_num;
int MLC_flash_oob_read_num;
int MLC_flash_oob_write_num;
/****************************/


int map_flash_read_num;
int map_flash_write_num;
int map_flash_gc_read_num;
int map_flash_gc_write_num;
int map_flash_erase_num;
int map_flash_oob_read_num;
int map_flash_oob_write_num;

int ftl_type;
int Mix_SSD_flag;
extern int total_util_sect_num; 
extern int total_extra_sect_num;

int global_total_blk_num;

int warm_done; 

int total_er_cnt;
int flag_er_cnt;
int block_er_flag[20000];
int block_dead_flag[20000];
int wear_level_flag[20000];
int unique_blk_num; 
int unique_log_blk_num;
int last_unique_log_blk;

int total_extr_blk_num;
int total_init_blk_num;

//�ж���Ӧ�Ŀ��ж���ҳ��ӳ������CMT��
int * nand_blk_bit_map;

#endif



