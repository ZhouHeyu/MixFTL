/* 
 * Contributors: Zhoujie(1395529361@qq.com)
 *               
 *
 * In case if you have any doubts or questions, kindly write to: 1395529361@qq.com 
 * 
 * Description: This is a header file for MixFTL.c. and related to dftl.h/dftl.c  
 * 
 */
#ifndef _MIXFTL_H_
#define _MIXFTL_H_
#include "dftl.h"
#include "type.h"

struct ftl_operation * Mopm_setup();

struct Mopm_entry {
  _u32 free  : 1;
  _u32 ppn   : 31;
  int  cache_status;
  int  cache_age;
  int  map_status;
  int  map_age;
  int  update;
  int  IsSLC;
};
//ppn 直接为SLC的物理块编号地址
struct Momap_dir{
  unsigned int ppn;
};

sect_t Mix_4K_opagemap_num;
sect_t SLC_4K_opagemap_num;
sect_t MLC_4K_opagemap_num;
sect_t Mix_4K_mapdir_num;
struct Mopm_entry * Mix_4K_opagemap;

int SLC_TOTAL_MAP_ENTRIES;
int MLC_TOTAL_MAP_ENTRIES; 


#define MIX_MAP_ENTRIES_PER_PAGE 512
#define UPN_SECT_NUM_PER_PAGE 8
#define UPN_SECT_BITS 3
_u32 mix_upn_SLC_start;
_u32 mix_upn_SLC_end;
_u32 mix_upn_MLC_start;
_u32 mix_upn_MLC_end;


#endif

