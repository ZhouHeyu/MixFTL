/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 *
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 * 
 * Description: This is a header file for dftl.c.  
 * 
 */
#ifndef _DFTL_H_
#include "type.h"

#define MAP_INVALID 1 
#define MAP_REAL 2
#define MAP_GHOST 3

#define CACHE_INVALID 0
#define CACHE_VALID 1

int flash_hit;
int disk_hit;
int read_cache_hit;
int write_cache_hit;
int evict;
int update_reqd;
int delay_flash_update;
int save_count;
struct ftl_operation * opm_setup();

struct opm_entry {
  _u32 free  : 1;
  _u32 ppn   : 31;
  int  cache_status;
  int  cache_age;
  int  map_status;
  int  map_age;
  int  update;
};

struct omap_dir{
  unsigned int ppn;
};

#define MAP_ENTRIES_PER_PAGE 512

int TOTAL_MAP_ENTRIES; 
int MAP_REAL_NUM_ENTRIES;
int MAP_GHOST_NUM_ENTRIES;

int CACHE_NUM_ENTRIES;




static int SYNC_NUM;

sect_t opagemap_num;
struct opm_entry *opagemap;
//add zhoujie 11-16 找到对应的块进行GC
_u32 opm_gc_cost_benefit();
_u32 SW_Level_Find_GC_blk_no();
void Select_Wear_Level_Threshold(int Type);
extern int Wear_Threshold_Type;

#endif
