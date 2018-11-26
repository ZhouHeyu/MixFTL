  /* 
* Contributors: Zhoujie (1395529361@qq.com)
* 
* In case if you have any doubts or questions, kindly write to: 1395529361@qq.com
*
* This source file implements the MixSSD MDFTL scheme.
* 
* 
*/
#include <stdlib.h>
#include <string.h>
	
#include "flash.h"
#include "MixFTL.h"
#include "ssd_interface.h"
#include "disksim_global.h"

struct Momap_dir * Mix_4K_mapdir;
int map_page_read=0;

blk_t extra_blk_num;

// 1:data blk 0:translate blk
_u32 free_SLC_blk_no[2];
_u32 free_MLC_blk_no[2];
_u16 free_SLC_page_no[2];
_u16 free_MLC_page_no[2];

int MLC_merge_full_num = 0;
int MLC_merge_switch_num = 0;
int MLC_merge_partial_num = 0;

int SLC_merge_full_num = 0;
int SLC_merge_switch_num = 0;
int SLC_merge_partial_num = 0;


//注意在callfsim上层函数的地址转化，lpn  (0-Mix_4K_mapdir_num *8)是归属于翻译页的编号
_u32 system_4K_opagemap_num; // (map lpn to 4k) + (user 4k page)



int Mopm_init(blk_t SLC_blk_num,blk_t MLC_blk_num, blk_t extra_num );
void Mopm_end();
void opagemap_reset();
size_t Mopm_write(sect_t lsn,sect_t size,int mapdir_flag);
size_t SLC_opm_write(sect_t lsn,sect_t size,int mapdir_flag);
size_t MLC_opm_write(sect_t lsn,sect_t size,int mapdir_flag);
size_t Mopm_read(sect_t lsn,sect_t size, int mapdir_flag);


int  SLC_gc_run(int small, int mapdir_flag);
int SLC_data_gc_run(int victim_blk_no,int mapdir_flag);
int SLC_map_gc_run(int victim_blk_no,int mapdir_flag);

int  MLC_gc_run(int small, int mapdir_flag);
_u32 SLC_opm_gc_cost_benefit();
_u32 MLC_opm_gc_cost_benefit();
int SLC_gc_get_free_blk(int small, int mapdir_flag);
int MLC_gc_get_free_blk(int small, int mapdir_flag);


/*
* Name : Mopm_init
* Date : 2018-11-22 
* author: zhoujie 
* param: SLC_blk_num(user SLC block num)
*		 MLC_blk_num(user MLC block num)
*        extra_num(system content size)
* return value: 0 is normal finish
* Attention : all Mix_opage_map based on 4K page size
*			  MLC and SLC map entry all in SLC 
*/
int Mopm_init(blk_t SLC_blk_num,blk_t MLC_blk_num, blk_t extra_num )
{
  int i;
  
  SLC_4K_opagemap_num =  SLC_blk_num * S_PAGE_NUM_PER_BLK/ 2;
  MLC_4K_opagemap_num = MLC_blk_num * M_PAGE_NUM_PER_BLK;
  Mix_4K_opagemap_num = SLC_4K_opagemap_num + MLC_4K_opagemap_num;
   
  //create primary mapping table
  Mix_4K_opagemap = (struct Mopm_entry *) malloc(sizeof (struct Mopm_entry) * Mix_4K_opagemap_num);
  // all second tranlate page num and map
  Mix_4K_mapdir_num = Mix_4K_opagemap_num /MIX_MAP_ENTRIES_PER_PAGE;
  
  if( (Mix_4K_opagemap_num % MIX_MAP_ENTRIES_PER_PAGE) !=0 ){
	printf("Mix_4K_opagemap_num % MIX_MAP_ENTRIES_PER_PAGE is not zeros\n");
	Mix_4K_mapdir_num ++;
  }  
  system_4K_opagemap_num = Mix_4K_mapdir_num + Mix_4K_opagemap_num;
  
  Mix_4K_mapdir = (struct Momap_dir *)malloc(sizeof(struct Momap_dir) * Mix_4K_mapdir_num);

  if((Mix_4K_opagemap == NULL)|| (Mix_4K_mapdir == NULL)){
	return -1;
  }

  memset(Mix_4K_opagemap, 0xFF, sizeof (struct Mopm_entry) * Mix_4K_opagemap_num);
  memset(Mix_4K_mapdir, 0xFF, sizeof(struct  Momap_dir) * Mix_4K_mapdir_num);

  //zhoujie: 1st map table 
  TOTAL_MAP_ENTRIES = Mix_4K_opagemap_num;
  
  for(i =0 ; i<TOTAL_MAP_ENTRIES;i++){
	Mix_4K_opagemap[i].cache_status = 0;
	Mix_4K_opagemap[i].cache_age = 0;
	Mix_4K_opagemap[i].map_status = 0;
	Mix_4K_opagemap[i].map_age = 0;
	Mix_4K_opagemap[i].update = 0;
	Mix_4K_opagemap[i].IsSLC = -1; 
  }
  
  extra_blk_num = extra_num;
//sure free data blk  
  free_SLC_blk_no[1] = SLC_nand_get_free_blk(0);
  free_SLC_page_no[1] = 0;
// sure free map blk
  free_SLC_blk_no[0] = SLC_nand_get_free_blk(0);
  free_SLC_page_no[0] = 0;
// sure free MLC data blk
  free_MLC_blk_no[1] = MLC_nand_get_free_blk(0);
  free_MLC_page_no[1] = 0;

//initialize CMT values
  MAP_REAL_NUM_ENTRIES = 0;
  MAP_GHOST_NUM_ENTRIES = 0;
  CACHE_NUM_ENTRIES = 0;
  SYNC_NUM = 0;

  cache_hit = 0;
  flash_hit = 0;
  disk_hit = 0;
  evict = 0;
  read_cache_hit = 0;
  write_cache_hit = 0;
  write_count =0;
  read_count = 0;
  save_count = 0;

  //update 2nd mapping table to SLC map blk
  for(i = 0; i < Mix_4K_mapdir_num; i++){
	ASSERT(MIX_MAP_ENTRIES_PER_PAGE == 512);
	Mopm_write(i*S_SECT_NUM_PER_PAGE,S_SECT_NUM_PER_PAGE,2);
  }

// 将全部的物理地址都转化到对应的mix_upn编号(based 4k)，SLC在前，MLC在后
//  mix_upn_SLC_start = 0;
//  mix_upn_SLC_end = nand_SLC_blk_num * S_PAGE_NUM_PER_BLK / 2 -1;
//  mix_upn_MLC_start = nand_SLC_blk_num * S_PAGE_NUM_PER_BLK / 2;
//  mix_upn_MLC_end = nand_SLC_blk_num * S_PAGE_NUM_PER_BLK / 2 + nand_MLC_blk_num * M_PAGE_NUM_PER_BLK;  
  return 0;
}

/*
* Name : Mopm_end
* Date : 2018-11-22 
* author: zhoujie 
* param: void
* return value:void
* Function :to free init malloc memory
* Attention:
*/
void Mopm_end()
{
  if(Mix_4K_opagemap != NULL){
	free(Mix_4K_opagemap);
	Mix_4K_opagemap = NULL;
  }
  if(Mix_4K_mapdir != NULL){
	free(Mix_4K_mapdir);
	Mix_4K_mapdir = NULL;
  }
 
}

/************************************
* Name : Mix_opagemap_reset()
* Date : 2018-11-23 
* author: zhoujie 
* param: 
* return value:
* Function :
* Attention: 函数定义和dftl.c存在重合
*/
void Mix_opagemap_reset()
{
  cache_hit = 0;
  flash_hit = 0;
  disk_hit = 0;
  evict = 0;
  delay_flash_update = 0; 
  read_count =0;
  write_count=0;
  map_page_read=0;
  save_count = 0;
}


/************************************
* Name : Mopm_read 
* Date : 2018-11-22 
* author: zhoujie 
* param: lsn (逻辑扇区号)
*		 size(逻辑请求大小,数据请求以callFsim 页(4K)大小下发,翻译页大小以(2K)对齐下发)
*		 mapdir_flag(2:map read ;!2:data read)
* return value:
* Function :
* Attention : 该函数的调用一般都是按页读取，在上层send request函数中调用
*			  下面应该区分对应的SLC和MLC物理地址转化
*			  数据页的读取都是按4K对齐
*			  翻译页的读取都是按2K对齐
*/
size_t Mopm_read(sect_t lsn,sect_t size, int mapdir_flag)
{
  int i, sect_num, flag;
  int ulpn = lsn / UPN_SECT_NUM_PER_PAGE; //logical page number based 4K;
  int size_ulpn = size / UPN_SECT_NUM_PER_PAGE; //size in page based 4k
  _u32  ppn;
  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 
  
  sect_t map_lsns[S_SECT_NUM_PER_PAGE];//map page size based SLC page  
  sect_t data_lsns[UPN_SECT_NUM_PER_PAGE]; //data page size based 4K 
  
  memset(map_lsns, 0xFF, sizeof(map_lsns));
  memset(data_lsns, 0xFF, sizeof(data_lsns));
  
  ASSERT(ulpn < system_4K_opagemap_num);
  ASSERT((ulpn + size_ulpn) < system_4K_opagemap_num);
  
  s_lsn = ulpn * UPN_SECT_NUM_PER_PAGE;
  
  if (mapdir_flag == 2){
//	翻译页的读取从SLC中读取,按2k对齐
	map_page_read ++ ;
  	sect_num = (size < S_SECT_NUM_PER_PAGE) ? size : S_SECT_NUM_PER_PAGE;
	s_psn = Mix_4K_mapdir[ulpn].ppn * S_SECT_NUM_PER_PAGE;
	for(i=0; i < S_SECT_NUM_PER_PAGE; i++){
		map_lsns[i] = s_lsn + i;
	}
	size = SLC_nand_page_read(s_psn, map_lsns, 0);
	ASSERT( size == S_SECT_NUM_PER_PAGE );

  }else{
//  数据页的地址伴随统一地址转化  
  	sect_num = (size < UPN_SECT_NUM_PER_PAGE) ? size : UPN_SECT_NUM_PER_PAGE;
	ppn = Mix_4K_opagemap[ulpn].ppn ;
	for(i=0; i < UPN_SECT_NUM_PER_PAGE; i++){
		data_lsns[i] = s_lsn + i;
	}
	if( Mix_4K_opagemap[ulpn].IsSLC == 1 ){
//  	data read from SLC
		s_psn = ppn * S_SECT_NUM_PER_PAGE;
		size = SLC_nand_4K_data_page_read( s_psn, data_lsns, 0);
	}else{
//		data  read from MLC
		s_psn = ppn * M_SECT_NUM_PER_PAGE;
		size = MLC_nand_page_read(s_psn, data_lsns, 0);
	}
	ASSERT(size == UPN_SECT_NUM_PER_PAGE);
  }
  return sect_num;

}

/************************************
* Name : Mopm_write
* Date : 2018-11-24 
* author: zhoujie 
* param: lsn (逻辑扇区请求地址)
*		 size(请求大小，按扇区大小)
*		 mapdir_flag(2: 翻译页写更新，1：热数据，0:冷数据)
* return value:
* Function :函数处理流程 分配新的物理地址，将旧的数据页标记为无效，写入新的地址
* Attention: 翻译页直接更新到SLC中去，数据页需要根据数据冷热添加到对应的SLC还是MLC中去
*     
*/
size_t Mopm_write(sect_t lsn,sect_t size,int mapdir_flag)
{
	int i;
	int sect_num;
	int ulpn = lsn/UPN_SECT_NUM_PER_PAGE; // logical page number based 4k
	int size_u_page = size/UPN_SECT_NUM_PER_PAGE; // size in page based 4k
	int SLC_flag;
	
	ASSERT(ulpn < system_4K_opagemap_num);
  	ASSERT(ulpn + size_u_page <= system_4K_opagemap_num);

	if(mapdir_flag <= 2 && mapdir_flag >= 1){ //write to SLC
	  SLC_flag = 1;
	}else if ( mapdir_flag == 1){//write to MLC
	  SLC_flag = 0;
	}else{
	  printf("something corrupted");
	  exit(0);
	}
	if( SLC_flag !=0 ){
//  write to SLC
		sect_num = SLC_opm_write(lsn , size, mapdir_flag);
	}else{
//  write to MLC
		sect_num = SLC_opm_write(lsn, size ,mapdir_flag);
	}

	return sect_num;

}

/************************************
* Name : SLC_opm_write
* Date : 2018-11-23 
* author: zhoujie 
* param: lsn (逻辑扇区请求地址)
*		 size(请求大小，按扇区大小)
*		 Mapdir_flag(2: 翻译页写更新，1:数据页写更新)
* return value: sect_num （有效写入的扇区数目，一般为4）
* Function :
* Attention: 该函数由opm_write 函数调用，主要实现将数据写入到SLC中
*			 需要区分是否为翻译页更新还是数据页更新，不同的类型的更新数据页的
*			 对齐方式不一样
*/

size_t SLC_opm_write(sect_t lsn,sect_t size,int mapdir_flag)
{
	int i;
	int sect_num;
	int small; 
	sect_t s_lsn; // starting logical sector number
	sect_t s_psn; // starting physical sector number 
	sect_t s_psn1;
	sect_t data_lsns[UPN_SECT_NUM_PER_PAGE];
	sect_t map_lsns[S_SECT_NUM_PER_PAGE];

	
	memset(data_lsns, 0xFF, sizeof(data_lsns));
	memset(map_lsns, 0xFF, sizeof(map_lsns));

	if(mapdir_flag == 2)
		small = 0;
	else if ( mapdir_flag == 1)
		small = 1;
	else{
		printf("SLC IsMap must select 2:data,1:map");
		assert(0);
	}
	
//  确保有空白SLC块可以写入，若空白SLC块不够，则进行SLC被动垃圾回收
	if(free_SLC_page_no[small] >= S_SECT_NUM_PER_PAGE){
		if ((free_SLC_blk_no[small] = SLC_nand_get_free_blk(0)) == -1) {
			int j = 0;
		while (free_SLC_blk_num < SLC_min_fb_num){
			j += SLC_gc_run(small, mapdir_flag);
		}
			SLC_gc_get_free_blk(small, mapdir_flag);
		}
	}else{
		free_SLC_page_no[small] = 0;
	}    		
	s_psn = S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]);
	
	if(s_psn % S_SECT_NUM_PER_PAGE!= 0){
	  printf("s_psn: %d\n", s_psn);
	}	
	
//	将旧的数据标记为无效,这里做区分处理(数据页和翻译页)
	if(mapdir_flag == 2 ){
		// 翻译页按2k对齐写入，map_lpn 相应逻辑地址也按2k地址
		int map_lpn = lsn / S_SECT_NUM_PER_PAGE;
		s_lsn =  map_lpn * S_SECT_NUM_PER_PAGE ;
		
		for(i=0 ; i < S_SECT_NUM_PER_PAGE ; i++){
			map_lsns[i] =  s_lsn + i;
		}
		if(Mix_4K_mapdir[map_lpn].ppn != -1 ){
			s_psn1 = Mix_4K_mapdir[map_lpn].ppn * S_SECT_NUM_PER_PAGE;
			for(i=0 ; i< S_SECT_NUM_PER_PAGE; i++){
				SLC_nand_invalidate( s_psn+i, s_lsn+i);
			}
			nand_stat(SLC_OOB_WRITE);
		}
		//写入数据
		Mix_4K_mapdir[map_lpn].ppn = S_PAGE_NO_SECT(s_psn);
		free_SLC_page_no[small] += S_SECT_NUM_PER_PAGE;
		sect_num = SLC_nand_page_write(s_psn, map_lsns, 0,  mapdir_flag);
		ASSERT( sect_num == S_SECT_NUM_PER_PAGE );
	}else{
//		数据页按4k对齐写入
		int data_lpn = lsn / UPN_SECT_NUM_PER_PAGE;
		s_lsn = data_lpn * UPN_SECT_NUM_PER_PAGE;
		for(i=0 ; i < UPN_SECT_NUM_PER_PAGE ; i++){
			data_lsns[i] = s_lsn + i;
		}
		if(Mix_4K_opagemap[data_lpn].free == 0){
			if(Mix_4K_opagemap[data_lpn].IsSLC == 1){ //SLC
				s_psn1 = Mix_4K_opagemap[data_lpn].ppn * S_SECT_NUM_PER_PAGE;
				for(i=0; i < S_SECT_NUM_PER_PAGE*2 ; i++){
					SLC_nand_invalidate(s_psn1+i, s_lsn+i);
				}
//				4k数据页相当置为两次		
				nand_stat(SLC_OOB_WRITE);
				nand_stat(SLC_OOB_WRITE);
			}else{ // MLC
				s_psn1 = Mix_4K_opagemap[data_lpn].ppn * M_SECT_NUM_PER_PAGE;
				for(i=0; i < M_SECT_NUM_PER_PAGE; i++){
					MLC_nand_invalidate(s_psn1+i, s_lsn+i);
				}
				nand_stat(MLC_OOB_WRITE);
			}
			
		}else{
			Mix_4K_opagemap[data_lpn].free = 0;
		}
		//写入数据
		Mix_4K_opagemap[data_lpn].ppn = S_PAGE_NO_SECT(s_psn);
		Mix_4K_opagemap[data_lpn].IsSLC = 1;
		free_SLC_page_no[small] += UPN_SECT_NUM_PER_PAGE;
		sect_num = SLC_nand_4K_data_page_write(s_psn, data_lsns, 0, mapdir_flag);
		ASSERT( sect_num == UPN_SECT_NUM_PER_PAGE );
	}
	
	return sect_num;
}

/************************************
* Name : MLC_opm_write
* Date : 2018-11-24 
* author: zhoujie 
* param: lsn (逻辑扇区请求地址)
*		 size(请求大小，按扇区大小)
*		 Mapdir_flag(0: 目前只支持MLC 数据页写更新)
* return value: sect_num (有效写入的扇区，一般为8)
* Function :
* Attention: 该函数由opm_write 函数调用，主要实现将数据写入到MLC中
*			 MLC只当作数据盘使用，不存储翻译页
*/
size_t MLC_opm_write(sect_t lsn,sect_t size,int mapdir_flag)
{
	int i;
	int sect_num;
	int small; 
	sect_t s_lsn; // starting logical sector number
	sect_t s_psn; // starting physical sector number 
	sect_t s_psn1;
	int data_lpn = lsn / UPN_SECT_NUM_PER_PAGE;
	sect_t data_lsns[UPN_SECT_NUM_PER_PAGE];

	memset(data_lsns, 0xFF, sizeof(data_lsns));
	if( mapdir_flag ==  0){
		small = 1;
	}else{
		printf("curr MLC write only support 4k data page write\n
				mapdir_flag must select 0\n");
		assert(0);
	}

	//  确保有空白SLC块可以写入，若空白SLC块不够，则进行SLC被动垃圾回收
	if(free_MLC_page_no[small] >= M_SECT_NUM_PER_PAGE){
		if ((free_MLC_blk_no[small] = MLC_nand_get_free_blk(0)) == -1) {
			int j = 0;
		while (free_MLC_blk_num < MLC_min_fb_num){
			j += MLC_gc_run(small, mapdir_flag);
		}
			MLC_gc_get_free_blk(small, mapdir_flag);
		}
	}else{
		free_MLC_page_no[small] = 0;
	}    		
	s_psn = S_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]);
	
	if(s_psn % M_SECT_NUM_PER_PAGE!= 0){
	  printf("s_psn: %d\n", s_psn);
	}	
	

	s_lsn = data_lpn * UPN_SECT_NUM_PER_PAGE;
	for(i=0 ; i < UPN_SECT_NUM_PER_PAGE ; i++){
		data_lsns[i] = s_lsn + i;
	}
//	将旧的数据标记为无效
	if(Mix_4K_opagemap[data_lpn].free == 0){
		
		if(Mix_4K_opagemap[data_lpn].IsSLC == 1){ //SLC
			s_psn1 = Mix_4K_opagemap[data_lpn].ppn * S_SECT_NUM_PER_PAGE;
			for(i=0; i < S_SECT_NUM_PER_PAGE*2 ; i++){
				SLC_nand_invalidate(s_psn1+i, s_lsn+i);
			}
//			4k数据页相当置为两次		
			nand_stat(SLC_OOB_WRITE);
			nand_stat(SLC_OOB_WRITE);
		}else{ // MLC
			s_psn1 = Mix_4K_opagemap[data_lpn].ppn * M_SECT_NUM_PER_PAGE;
			for(i=0; i < M_SECT_NUM_PER_PAGE; i++){
				MLC_nand_invalidate(s_psn1+i, s_lsn+i);
			}
			nand_stat(MLC_OOB_WRITE);
		}

	}else{
		Mix_4K_opagemap[data_lpn].free = 0;
	}
	
	Mix_4K_opagemap[data_lpn].ppn = s_psn/M_SECT_NUM_PER_PAGE;
	Mix_4K_opagemap[data_lpn].IsSLC = 0;
	free_MLC_page_no[small] += UPN_SECT_NUM_PER_PAGE;
	sect_num = MLC_nand_page_write(s_psn, data_lsns,0, mapdir_flag);

	ASSERT( sect_num == UPN_SECT_NUM_PER_PAGE );
			
	return sect_num;
}


/************************************
* Name : SLC_gc_get_free_blk
* Date : 2018-11-23 
* author: zhoujie 
* param: small(1:data ,0:map)
* return value: （-1：重新open了一个新的SLC块，0:旧块可以继续写） 
* Function :
* Attention:
*/
int SLC_gc_get_free_blk(int small, int mapdir_flag)
{
  if (free_SLC_page_no[small] >= S_SECT_NUM_PER_BLK) {
	free_SLC_blk_no[small] =  SLC_nand_get_free_blk(1);
    free_SLC_page_no[small] = 0;
    return -1;
  }
  return 0;
}

/************************************
* Name : MLC_gc_get_free_blk
* Date : 2018-11-23 
* author: zhoujie 
* param: small(1:data ,0:map)
* return value: (-1：重新open了一个新的MLC块，0:旧块可以继续写） 
* Function :
* Attention: 目前MLC支持数据页的写入，所以small当前的调入为1
*/
int MLC_gc_get_free_blk(int small, int mapdir_flag)
{
  if (free_MLC_page_no[small] >= M_SECT_NUM_PER_BLK) {
	free_MLC_blk_no[small] =  MLC_nand_get_free_blk(1);
    free_MLC_page_no[small] = 0;
    return -1;
  }
  return 0;
}



/************************************
* Name : SLC_gc_run
* Date : 2018-11-26 
* author: zhoujie 
* param: 
* return value:
* Function : 当SLC的free 块个数小于阈值，该函数触发进行数据搬移
* Attention: 注意区分SLC中的翻译块和数据块的更新
*/
int  SLC_gc_run(int small,int mapdir_flag)
{
	int benefit = 0;
	blk_t victim_blk_no;
//	基于贪婪原则获取SLC的擦除块
	victim_blk_no = SLC_opm_gc_cost_benefit();
	if( mapdir_flag == 2 ){
		benefit = SLC_map_gc_run(victim_blk_no, mapdir_flag);
	}else if (mapdir_flag == 1){
		benefit = SLC_data_gc_run(victim_blk_no, mapdir_flag);
	}else {
		printf("SLC gc run mapdir_flag must(2: map, 1: data)\n");
		exit(0);
	}
	
	return benefit;
}

/************************************
* Name : SLC_map_gc_run
* Date : 2018-11-26 
* author: zhoujie 
* param: victim_blk_no(指定回收的翻译块)
*		 mapdir_flag (为了符合调用函数的传值添加)
* return value:
* Function : 当SLC gc run回收的时候，进行调用，主要完成翻译块的数据页拷贝
* Attention: 翻译页的大小为2k,和底层的SLC也大小一致
*/
int SLC_map_gc_run(int victim_blk_no,int mapdir_flag)
{
	int small;
	int benefit = 0;
	int merge_count = 0, pos = 0;
	int s,k,q,i,j;
	int valid_flag,valid_sect_num;
	_u32 copy[S_SECT_NUM_PER_PAGE],copy_lsn[S_SECT_NUM_PER_PAGE];
	
	memset(copy, 0xFF, sizeof(copy));
	memset(copy_lsn, 0xFF, sizeof(copy_lsn));
	
//  确保翻译块中的所有数据页的状态都是翻译状态
	for( q = 0; q  < S_PAGE_NUM_PER_BLK; q++) {
	  if(SLC_nand_blk[victim_blk_no].page_status[q] == 0 ){
	  	// map page status is 1
		printf("something corrupted1=%d",victim_blk_no);
		assert(0);
	  }
	}
	small = 0;
//	 确认写入块的偏移
	s = k = S_OFF_F_SECT(free_SLC_page_no[small]);
	if(!((s == 0) && (k == 0))){
		printf("s && k should be 0\n");
		exit(0);
	}
// 翻译页拷贝
	for( i =0 ;i < S_SECT_NUM_PER_PAGE ; i++){

		valid_flag = SLC_nand_oob_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE));
//      翻译页有效
		if(valid_flag == 1){
			merge_count++ ;
			valid_sect_num = SLC_nand_page_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE), copy, 1);
			ASSERT( valid_sect_num == S_SECT_NUM_PER_PAGE);
			ASSERT( SLC_nand_blk[victim_blk_no].page_status[i] == 1);
        	for (j = 0 , k = 0; j < valid_sect_num; j++, k++) {
          		copy_lsn[k] = copy[j];
          		k++;
        	}
//		找到空闲翻译页		
			benefit += SLC_gc_get_free_blk(small, mapdir_flag);
			Mix_4K_mapdir[(copy_lsn[s]/S_SECT_NUM_PER_PAGE)].ppn  = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]));
			SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[small],free_SLC_page_no[small]) & (~S_OFF_MASK_SECT), copy_lsn, 1, 2);
			free_SLC_page_no[small] += S_SECT_NUM_PER_PAGE;
			
        }
		
	}//end-for
	
	//	统计合并次数
	if(merge_count == 0 ) 
	  SLC_merge_switch_num++;
	else if(merge_count > 0 && merge_count < S_PAGE_NUM_PER_BLK)
	  SLC_merge_partial_num++;
	else if(merge_count == S_PAGE_NUM_PER_BLK)
	  SLC_merge_full_num++;
	else if(merge_count > S_PAGE_NUM_PER_BLK){
	  printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,S_PAGE_NUM_PER_BLK);
	  ASSERT(0);
	}
	//	将数据块擦除
	SLC_nand_erase(victim_blk_no);
	//	此处插入磨损均衡代码
	
	return (benefit + 1);
}


/************************************
* Name : SLC_data_gc_run
* Date : 2018-11-26 
* author: zhoujie 
* param: victim_blk(指定回收的 数据块)
* 		 mapdir_flag(和调用函数的接口对齐引入的参数)
* return value:
* Function : 当SLC gc run回收的时候，进行调用，主要完成数据块的数据页拷贝
* Attention: 翻译页的大小为4k,为底层SLC的页大小的2倍
*/
int SLC_data_gc_run(int victim_blk_no,int mapdir_flag)
{
	int small;
	int benefit = 0;
	int merge_count = 0, pos = 0;
	int s,k,q,i,j;
	int valid_flag1,valid_flag2,valid_sect_num;
	_u32 data_copy[UPN_SECT_NUM_PER_PAGE],data_copy_lsn[UPN_SECT_NUM_PER_PAGE];
	_u32 map_copy[S_SECT_NUM_PER_PAGE];

	int old_flag;
	_u32 m,map_lpn;
	int temp_arr[S_PAGE_NUM_PER_BLK/2],temp_arr1[S_PAGE_NUM_PER_BLK/2],map_arr[S_PAGE_NUM_PER_BLK/2]; 

	
	
	memset(data_copy, 0xFF, sizeof(data_copy));
	memset(data_copy_lsn, 0xFF, sizeof(data_copy_lsn));
	memset(map_copy,0xFF,sizeof(map_copy));

	//  确保数据块中的所有数据页的状态都是数据状态
	for( q = 0; q  < S_PAGE_NUM_PER_BLK; q++) {
	  if(SLC_nand_blk[victim_blk_no].page_status[q] == 1 ){
	  	// data page status is 0
		printf("something corrupted1=%d",victim_blk_no);
		assert(0);
	  }
	}
	
	small = 1;
	//	 确认写入块的偏移
	s = k = S_OFF_F_SECT(free_SLC_page_no[small]);
	if(!((s == 0) && (k == 0))){
		printf("s && k should be 0\n");
		exit(0);
	}
	//数据页拷贝,注意数据页是按4k对齐
	for(i = 0; i < S_SECT_NUM_PER_PAGE; i+=2){
		valid_flag1 = SLC_nand_oob_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE));
		valid_flag2 = SLC_nand_oob_read( S_SECTOR(victim_blk_no, (i+1) * S_SECT_NUM_PER_PAGE));
//		连续2个SLC数据页都为有效，即4k数据有效		
		if( (valid_flag1 == 1) &&(valid_flag2 == 2)){
			merge_count++ ;
			valid_sect_num = SLC_nand_4K_data_page_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE), data_copy, 1);
			ASSERT( valid_sect_num == UPN_SECT_NUM_PER_PAGE);
			ASSERT( SLC_nand_blk[victim_blk_no].page_status[i] == 1);
			ASSERT( SLC_nand_blk[victim_blk_no].page_status[i+1] == 1);

        	for (j = 0 , k = 0; j < valid_sect_num; j++, k++) {
          		data_copy_lsn[k] = data_copy[j];
          		k++;
        	}

//			确定空闲写入位置
			benefit += SLC_gc_get_free_blk(small, mapdir_flag);
			ASSERT(Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].IsSLC == 1); //4k data
			Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]));
			Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].IsSLC = 1;
			
            SLC_nand_4K_data_page_write(S_SECTOR(free_SLC_blk_no[small],free_SLC_page_no[small]) & (~S_OFF_MASK_SECT), data_copy_lsn, 1, 1);
            free_SLC_page_no[small] += UPN_SECT_NUM_PER_PAGE;
						
//			判断翻译项是否存在CMT,延迟更新翻译项		
			if((Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].map_status == MAP_REAL) 
				|| (Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].map_status == MAP_GHOST)) {
			  delay_flash_update++;
			}
			else {
			  map_arr[pos] = data_copy_lsn[0];
			  pos++;
			} 
	
		}//有效数据页拷贝
	}//end--for


//	翻译页更新，确定哪些映射项同属一个翻译页一起更新
	for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1; //之后存的都是需要更新的翻译页（SLC）
	}
		
	k=0;
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {
			 if(temp_arr[j] == Mix_4K_mapdir[(map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE].ppn){
//				必须有对应的翻译页 ,若何之前的翻译项同属一个翻译页则不需要添加新的翻译页	
				  if(temp_arr[j] == -1){
						printf("something wrong");
						ASSERT(0);
				  }
				  old_flag = 1;
				  break;
			 }
		}
		if( old_flag == 0 ) {
			 temp_arr[k] = Mix_4K_mapdir[((map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE)].ppn;
			 temp_arr1[k] = map_arr[i];
			 k++;
		}
		else
		  save_count++;
	}
//	更新SLC中的翻译页
	for ( i=0; i < k; i++) {
		if(free_SLC_blk_no[0] >= S_SECT_NUM_PER_PAGE) {
			if((free_SLC_blk_no[0] = SLC_nand_get_free_blk(1)) == -1){
				printf("we are in big trouble shudnt happen \n
					In SLC data GC update--> SLC map free block is full\n");
			}
				  free_SLC_page_no[0] = 0;
		}
		
		SLC_nand_page_read(temp_arr[i]*S_SECT_NUM_PER_PAGE, map_copy, 1);
		map_lpn = (temp_arr1[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE;
		
		//invalid old map page
		for(m = 0; m < S_SECT_NUM_PER_PAGE; m++){
			SLC_nand_invalidate( Mix_4K_mapdir[map_lpn].ppn*S_SECT_NUM_PER_PAGE+m, map_copy[m]);
		}
		nand_stat(SLC_OOB_WRITE);

		Mix_4K_mapdir[map_lpn].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[0], free_SLC_page_no[0]));
//		翻译页更新到SLC
		SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[0],free_SLC_page_no[0]) & (~S_OFF_MASK_SECT), map_copy, 1, 2);
		free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	}

	//	统计合并次数
	if(merge_count == 0 ) 
	  SLC_merge_switch_num++;
	else if(merge_count > 0 && merge_count < S_PAGE_NUM_PER_BLK)
	  SLC_merge_partial_num++;
	else if(merge_count == S_PAGE_NUM_PER_BLK)
	  SLC_merge_full_num++;
	else if(merge_count > S_PAGE_NUM_PER_BLK){
	  printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,S_PAGE_NUM_PER_BLK);
	  ASSERT(0);
	}
	//	将数据块擦除
	SLC_nand_erase(victim_blk_no);
	//	此处插入磨损均衡代码
	
	return 0;
}


/************************************
* Name : MLC_gc_run(small, mapdir_flag)
* Date : 2018-11-24 
* author: zhoujie 
* param: 
* return value:
* Function : 当MLC的free 块个数小于阈值，该函数触发进行数据搬移
* Attention: MLC 只做数据块区不存在翻译页更新
*/
int  MLC_gc_run(int small, int mapdir_flag)
{
	int benefit = 0;
	blk_t victim_blk_no;
	_u32 copy_lsn[UPN_SECT_NUM_PER_PAGE], copy[UPN_SECT_NUM_PER_PAGE];
	_u32 map_copy[S_SECT_NUM_PER_PAGE],map_lpn;

	int merge_count;
  	int i,z, j,m,q;
  	int k,old_flag,temp_arr[M_PAGE_NUM_PER_BLK],temp_arr1[M_PAGE_NUM_PER_BLK],map_arr[M_PAGE_NUM_PER_BLK]; 
  	int valid_flag,pos;
  	_u16 valid_sect_num,  l, s;

//  基于贪婪原则获取MLC的擦除块
  	victim_blk_no = MLC_opm_gc_cost_benefit();
  	memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  	s = k = M_OFF_F_SECT(free_MLC_page_no[small]);
  	if(!((s == 0) && (k == 0))){
    	printf("s && k should be 0\n");
    	exit(0);
  	}
//   确保数据区的所有数据页都为数据页状态
	for( q = 0; q < M_PAGE_NUM_PER_BLK; q++){
		if(MLC_nand_blk[victim_blk_no].page_status[q] != 0){
			printf("MLC page status must be data page\n
					something corrupted MLC blkno=%d\n",victim_blk_no);
			assert(0);
		}
	}
//  依次读取有效数据页 拷贝到当前的写入块
	pos = 0;
  	merge_count = 0;
  	for (i = 0; i < M_PAGE_NUM_PER_BLK; i++) 
  	{
		valid_flag = MLC_nand_oob_read( M_SECTOR(victim_blk_no, i * M_SECT_NUM_PER_PAGE));
//		如果数据有效
    	if(valid_flag == 1)
    	{
        	valid_sect_num = MLC_nand_page_read( M_SECTOR(victim_blk_no, i * M_SECT_NUM_PER_PAGE), copy, 1);
			merge_count++;
			ASSERT(valid_sect_num == M_SECT_NUM_PER_PAGE );

			k=0;
			for (j = 0; j < valid_sect_num; j++) {
          		copy_lsn[k] = copy[j];
          		k++;
        	}
//			确定空闲写入位置
			benefit += MLC_gc_get_free_blk(small, mapdir_flag);
//          MLC 只有数据页拷贝
			ASSERT(MLC_nand_blk[victim_blk_no].page_status[i] == 0);
			ASSERT(Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].IsSLC == 0);
			Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]));
			Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].IsSLC = 0;

            MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[small],free_MLC_page_no[small]) & (~M_OFF_MASK_SECT), copy_lsn, 1, 1);
            free_MLC_page_no[small] += M_SECT_NUM_PER_PAGE;
			
//			判断翻译项是否存在CMT,延迟更新翻译项		
            if((Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_REAL) 
				|| (Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_GHOST)) {
              delay_flash_update++;
            }
            else {
              map_arr[pos] = copy_lsn[s];
              pos++;
            } 
			
        }//有效数据拷贝
    }//end-for

//  翻译页更新，确定哪些映射项同属一个翻译页一起更新
	for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1; //之后存的都是需要更新的翻译页（SLC）
	}
	
	k=0;
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {
			 if(temp_arr[j] == Mix_4K_mapdir[(map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE].ppn){
//				必须有对应的翻译页 ,若何之前的翻译项同属一个翻译页则不需要添加新的翻译页	
				  if(temp_arr[j] == -1){
						printf("something wrong");
						ASSERT(0);
				  }
				  old_flag = 1;
				  break;
			 }
		}
		if( old_flag == 0 ) {
			 temp_arr[k] = Mix_4K_mapdir[((map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE)].ppn;
			 temp_arr1[k] = map_arr[i];
			 k++;
		}
		else
		  save_count++;
	}
//	更新SLC中的翻译页
	for ( i=0; i < k; i++) {
		if(free_SLC_blk_no[0] >= S_SECT_NUM_PER_PAGE) {
			if((free_SLC_blk_no[0] = SLC_nand_get_free_blk(1)) == -1){
				printf("we are in big trouble shudnt happen \n
					In MLC data GC update--> SLC map free block is full\n");
			}
				  free_SLC_page_no[0] = 0;
		}
		
		SLC_nand_page_read(temp_arr[i]*S_SECT_NUM_PER_PAGE, map_copy, 1);
		map_lpn = (temp_arr1[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE;
		
		//invalid old map page
	    for(m = 0; m < S_SECT_NUM_PER_PAGE; m++){
			SLC_nand_invalidate( Mix_4K_mapdir[map_lpn].ppn*S_SECT_NUM_PER_PAGE+m, map_copy[m]);
	    }
		nand_stat(SLC_OOB_WRITE);

		Mix_4K_mapdir[map_lpn].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[0], free_SLC_page_no[0]));
//		翻译页更新到SLC
		SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[0],free_SLC_page_no[0]) & (~S_OFF_MASK_SECT), map_copy, 1, 2);
		free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	}
//	统计合并次数
	if(merge_count == 0 ) 
	  MLC_merge_switch_num++;
	else if(merge_count > 0 && merge_count < M_PAGE_NUM_PER_BLK)
	  MLC_merge_partial_num++;
	else if(merge_count == M_PAGE_NUM_PER_BLK)
	  MLC_merge_full_num++;
	else if(merge_count > M_PAGE_NUM_PER_BLK){
	  printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,M_PAGE_NUM_PER_BLK);
	  ASSERT(0);
	}
//	将数据块擦除
	MLC_nand_erase(victim_blk_no);
//	此处插入磨损均衡代码

	return (benefit+1);
}


/************************************
* Name : MLC_opm_gc_cost_benefit
* Date : 2018-11-24
* author: zhoujie 
* param: 
* return value: MLC GC 选块的块号
* Function : 基于贪婪原则选择的无效页最多的MLC进行块擦除
* Attention: 没有触发块磨损均衡时的选块
*/

_u32 MLC_opm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_MLC_blk_num; i++) {
    if(i == free_MLC_blk_no[1]){
      continue;
    }

    blk_cb = MLC_nand_blk[i].ipc;
    
    if (blk_cb > max_cb) {
      max_cb = blk_cb;
      max_blk = i;
    }
  }
  
  ASSERT(max_blk != -1);
  ASSERT(MLC_nand_blk[max_blk].ipc > 0);
  return max_blk;
}

/************************************
* Name : SLC_opm_gc_cost_benefit
* Date : 2018-11-24
* author: zhoujie 
* param: 
* return value: SLC GC 选块的块号
* Function : 基于贪婪原则选择的无效页最多的SLC进行块擦除
* Attention: 没有触发块磨损均衡时的选块
*/
_u32 SLC_opm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_SLC_blk_num; i++) {
    if( i == free_SLC_blk_no[1] || i == free_SLC_blk_no[0]){
      continue;
    }

    blk_cb = SLC_nand_blk[i].ipc;

    
    if (blk_cb > max_cb) {
      max_cb = blk_cb;
      max_blk = i;
    }
  }

  ASSERT(max_blk != -1);
  ASSERT(SLC_nand_blk[max_blk].ipc > 0);
  return max_blk;
}


struct ftl_operation Mopm_operation = {
	init:  Mopm_init,
	read:  Mopm_read,
	write: Mopm_write,
	end:   Mopm_end
};
  
struct ftl_operation * Mopm_setup()
{
  return &Mopm_operation;
}

/************************************
* Name : 
* Date : 2018-11-26
* author: zhoujie 
* param: 
* return value:
* Function :
* Attention:
*/

/************************************
* Name : 
* Date : 2018-11-26 
* author: zhoujie 
* param: 
* return value:
* Function :
* Attention:
*/

