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
static _u32 free_SLC_blk_no[2];
static _u32 free_MLC_blk_no[2];
static _u16 free_SLC_page_no[2];
static _u16 free_MLC_page_no[2];

int MLC_merge_full_num = 0;
int MLC_merge_switch_num = 0;
int MLC_merge_partial_num = 0;

int SLC_merge_full_num = 0;
int SLC_merge_switch_num = 0;
int SLC_merge_partial_num = 0;


//ע����callfsim�ϲ㺯���ĵ�ַת����lpn  (0-Mix_4K_mapdir_num *8)�ǹ����ڷ���ҳ�ı��
_u32 system_4K_opagemap_num; // (map lpn to 4k) + (user 4k page)



int Mopm_init(blk_t SLC_blk_num,blk_t MLC_blk_num);
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


/******************************************
* Name : Mopm_init
* Date : 2018-11-22 
* author: zhoujie 
* param: SLC_blk_num(user SLC block num)
*		 MLC_blk_num(user MLC block num)
*        extra_num(system content size)
* return value: 0 is normal finish
* Attention : all Mix_opage_map based on 4K page size
*			  MLC and SLC map entry all in SLC 
***********************************************/
int Mopm_init(blk_t SLC_blk_num,blk_t MLC_blk_num)
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
  
//  extra_blk_num = extra_num;
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

  return 0;
}

/********************************
* Name : Mopm_end
* Date : 2018-11-22 
* author: zhoujie 
* param: void
* return value:void
* Function :to free init malloc memory
* Attention:
*********************************/
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
* Attention: ���������dftl.c�����غ�
************************************/
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
* param: lsn (�߼�������)
*		 size(�߼������С,����������callFsim ҳ(4K)��С�·�,����ҳ��С��(2K)�����·�)
*		 mapdir_flag(2:map read ;!2:data read)
* return value:
* Function :
* Attention : �ú����ĵ���һ�㶼�ǰ�ҳ��ȡ�����ϲ�send request�����е���
*			  ����Ӧ�����ֶ�Ӧ��SLC��MLC�����ַת��
*			  ����ҳ�Ķ�ȡ���ǰ�4K����
*			  ����ҳ�Ķ�ȡ���ǰ�2K����
*****************************************/
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
//	����ҳ�Ķ�ȡ��SLC�ж�ȡ,��2k����
	map_page_read ++ ;
  	sect_num = (size < S_SECT_NUM_PER_PAGE) ? size : S_SECT_NUM_PER_PAGE;
	s_psn = Mix_4K_mapdir[ulpn].ppn * S_SECT_NUM_PER_PAGE;
	for(i=0; i < S_SECT_NUM_PER_PAGE; i++){
		map_lsns[i] = s_lsn + i;
	}
	
	size = SLC_nand_page_read(s_psn, map_lsns, 0);
	ASSERT( size == S_SECT_NUM_PER_PAGE );

  }else{
//  ����ҳ�ĵ�ַ����ͳһ��ַת��  
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
* param: lsn (�߼����������ַ)
*		 size(�����С����������С)
*		 mapdir_flag(2: ����ҳд���£�1�������ݣ�0:������)
* return value:
* Function :������������ �����µ������ַ�����ɵ�����ҳ���Ϊ��Ч��д���µĵ�ַ
* Attention: ����ҳֱ�Ӹ��µ�SLC��ȥ������ҳ��Ҫ��������������ӵ���Ӧ��SLC����MLC��ȥ
*     
***************************************/
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
* param: lsn (�߼����������ַ)
*		 size(�����С����������С)
*		 Mapdir_flag(2: ����ҳд���£�1:����ҳд����)
* return value: sect_num ����Чд���������Ŀ��һ��Ϊ4��
* Function :
* Attention: �ú�����opm_write �������ã���Ҫʵ�ֽ�����д�뵽SLC��
*			 ��Ҫ�����Ƿ�Ϊ����ҳ���»�������ҳ���£���ͬ�����͵ĸ�������ҳ��
*			 ���뷽ʽ��һ��
*************************************/

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
	
//  ȷ���пհ�SLC�����д�룬���հ�SLC�鲻���������SLC������������
	if(free_SLC_page_no[small] >= S_SECT_NUM_PER_BLK){
		if ((free_SLC_blk_no[small] = SLC_nand_get_free_blk(0)) == -1) {
			int j = 0;
			while (free_SLC_blk_num < SLC_min_fb_num){
				j += SLC_gc_run(small, mapdir_flag);
			}
			SLC_gc_get_free_blk(small, mapdir_flag);
		}else{
			free_SLC_page_no[small] = 0;
		}
	}
		
	s_psn = S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]);

	
	if(s_psn % S_SECT_NUM_PER_PAGE!= 0){
	  printf("s_psn: %d\n", s_psn);
	}	
	
//	���ɵ����ݱ��Ϊ��Ч,���������ִ���(����ҳ�ͷ���ҳ)
	if(mapdir_flag == 2 ){
		// ����ҳ��2k����д�룬map_lpn ��Ӧ�߼���ַҲ��2k��ַ
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
		//д������
		Mix_4K_mapdir[map_lpn].ppn = S_PAGE_NO_SECT(s_psn);
		free_SLC_page_no[small] += S_SECT_NUM_PER_PAGE;
		
		sect_num = SLC_nand_page_write(s_psn, map_lsns, 0,  mapdir_flag);
		ASSERT( sect_num == S_SECT_NUM_PER_PAGE );
	}else{
//		����ҳ��4k����д��
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
//				4k����ҳ�൱��Ϊ����		
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
		//д������
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
* param: lsn (�߼����������ַ)
*		 size(�����С����������С)
*		 Mapdir_flag(0: Ŀǰֻ֧��MLC ����ҳд����)
* return value: sect_num (��Чд���������һ��Ϊ8)
* Function :
* Attention: �ú�����opm_write �������ã���Ҫʵ�ֽ�����д�뵽MLC��
*			 MLCֻ����������ʹ�ã����洢����ҳ
**************************************/
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

	//  ȷ���пհ�SLC�����д�룬���հ�SLC�鲻���������SLC������������
	if(free_MLC_page_no[small] >= M_SECT_NUM_PER_BLK){
		if ((free_MLC_blk_no[small] = MLC_nand_get_free_blk(0)) == -1) {
			int j = 0;
			while (free_MLC_blk_num < MLC_min_fb_num){
				j += MLC_gc_run(small, mapdir_flag);
			}
			MLC_gc_get_free_blk(small, mapdir_flag);
		}else{
			free_MLC_page_no[small] = 0;
		} 
	}
	
	s_psn = S_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]);
	
	if(s_psn % M_SECT_NUM_PER_PAGE!= 0){
	  printf("s_psn: %d\n", s_psn);
	}	
	

	s_lsn = data_lpn * UPN_SECT_NUM_PER_PAGE;
	for(i=0 ; i < UPN_SECT_NUM_PER_PAGE ; i++){
		data_lsns[i] = s_lsn + i;
	}
//	���ɵ����ݱ��Ϊ��Ч
	if(Mix_4K_opagemap[data_lpn].free == 0){
		
		if(Mix_4K_opagemap[data_lpn].IsSLC == 1){ //SLC
			s_psn1 = Mix_4K_opagemap[data_lpn].ppn * S_SECT_NUM_PER_PAGE;
			for(i=0; i < S_SECT_NUM_PER_PAGE*2 ; i++){
				SLC_nand_invalidate(s_psn1+i, s_lsn+i);
			}
//			4k����ҳ�൱��Ϊ����		
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
* return value: ��-1������open��һ���µ�SLC�飬0:�ɿ���Լ���д�� 
* Function :
* Attention:
**************************************/
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
* return value: (-1������open��һ���µ�MLC�飬0:�ɿ���Լ���д�� 
* Function :
* Attention: ĿǰMLC֧������ҳ��д�룬����small��ǰ�ĵ���Ϊ1
**************************************/
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
* Function : ��SLC��free �����С����ֵ���ú��������������ݰ���
* Attention: ע������SLC�еķ��������ݿ�ĸ���
**************************************/
int  SLC_gc_run(int small,int mapdir_flag)
{
	int benefit = 0;
	blk_t victim_blk_no;
//	����̰��ԭ���ȡSLC�Ĳ�����
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
* param: victim_blk_no(ָ�����յķ����)
*		 mapdir_flag (Ϊ�˷��ϵ��ú����Ĵ�ֵ���)
* return value:
* Function : ��SLC gc run���յ�ʱ�򣬽��е��ã���Ҫ��ɷ���������ҳ����
* Attention: ����ҳ�Ĵ�СΪ2k,�͵ײ��SLCҲ��Сһ��
**************************************/
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
	
//  ȷ��������е���������ҳ��״̬���Ƿ���״̬
	for( q = 0; q  < S_PAGE_NUM_PER_BLK; q++) {
	  if(SLC_nand_blk[victim_blk_no].page_status[q] == 0 ){
	  	// map page status is 1
		printf("something corrupted1=%d",victim_blk_no);
		assert(0);
	  }
	}
	small = 0;
//	 ȷ��д����ƫ��
	s = k = S_OFF_F_SECT(free_SLC_page_no[small]);
	if(!((s == 0) && (k == 0))){
		printf("s && k should be 0\n");
		exit(0);
	}
// ����ҳ����
	for( i =0 ;i < S_SECT_NUM_PER_PAGE ; i++){

		valid_flag = SLC_nand_oob_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE));
//      ����ҳ��Ч
		if(valid_flag == 1){
			merge_count++ ;
			valid_sect_num = SLC_nand_page_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE), copy, 1);
			ASSERT( valid_sect_num == S_SECT_NUM_PER_PAGE);
			ASSERT( SLC_nand_blk[victim_blk_no].page_status[i] == 1);
        	for (j = 0 , k = 0; j < valid_sect_num; j++, k++) {
          		copy_lsn[k] = copy[j];
          		k++;
        	}
//		�ҵ����з���ҳ		
			benefit += SLC_gc_get_free_blk(small, mapdir_flag);
			Mix_4K_mapdir[(copy_lsn[s]/S_SECT_NUM_PER_PAGE)].ppn  = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]));
			SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[small],free_SLC_page_no[small]) & (~S_OFF_MASK_SECT), copy_lsn, 1, 2);
			free_SLC_page_no[small] += S_SECT_NUM_PER_PAGE;
			
        }
		
	}//end-for
	
	//	ͳ�ƺϲ�����
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
	//	�����ݿ����
	SLC_nand_erase(victim_blk_no);
	//	�˴�����ĥ��������
	
	return (benefit + 1);
}


/************************************
* Name : SLC_data_gc_run
* Date : 2018-11-26 
* author: zhoujie 
* param: victim_blk(ָ�����յ� ���ݿ�)
* 		 mapdir_flag(�͵��ú����Ľӿڶ�������Ĳ���)
* return value:
* Function : ��SLC gc run���յ�ʱ�򣬽��е��ã���Ҫ������ݿ������ҳ����
* Attention: ����ҳ�Ĵ�СΪ4k,Ϊ�ײ�SLC��ҳ��С��2��
*****************************************/
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

	//  ȷ�����ݿ��е���������ҳ��״̬��������״̬
	for( q = 0; q  < S_PAGE_NUM_PER_BLK; q++) {
	  if(SLC_nand_blk[victim_blk_no].page_status[q] == 1 ){
	  	// data page status is 0
		printf("something corrupted1=%d",victim_blk_no);
		assert(0);
	  }
	}
	
	small = 1;
	//	 ȷ��д����ƫ��
	s = k = S_OFF_F_SECT(free_SLC_page_no[small]);
	if(!((s == 0) && (k == 0))){
		printf("s && k should be 0\n");
		exit(0);
	}
	//����ҳ����,ע������ҳ�ǰ�4k����
	for(i = 0; i < S_SECT_NUM_PER_PAGE; i+=2){
		valid_flag1 = SLC_nand_oob_read( S_SECTOR(victim_blk_no, i * S_SECT_NUM_PER_PAGE));
		valid_flag2 = SLC_nand_oob_read( S_SECTOR(victim_blk_no, (i+1) * S_SECT_NUM_PER_PAGE));
//		����2��SLC����ҳ��Ϊ��Ч����4k������Ч		
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

//			ȷ������д��λ��
			benefit += SLC_gc_get_free_blk(small, mapdir_flag);
			ASSERT(Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].IsSLC == 1); //4k data
			Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].ppn = S_BLK_PAGE_NO_SECT(S_SECTOR(free_SLC_blk_no[small], free_SLC_page_no[small]));
			Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].IsSLC = 1;
			
            SLC_nand_4K_data_page_write(S_SECTOR(free_SLC_blk_no[small],free_SLC_page_no[small]) & (~S_OFF_MASK_SECT), data_copy_lsn, 1, 1);
            free_SLC_page_no[small] += UPN_SECT_NUM_PER_PAGE;
						
//			�жϷ������Ƿ����CMT,�ӳٸ��·�����		
			if((Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].map_status == MAP_REAL) 
				|| (Mix_4K_opagemap[UPN_BLK_PAGE_NO_SECT(data_copy_lsn[0])].map_status == MAP_GHOST)) {
			  delay_flash_update++;
			}
			else {
			  map_arr[pos] = data_copy_lsn[0];
			  pos++;
			} 
	
		}//��Ч����ҳ����
	}//end--for


//	����ҳ���£�ȷ����Щӳ����ͬ��һ������ҳһ�����
	for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1; //֮���Ķ�����Ҫ���µķ���ҳ��SLC��
	}
		
	k=0;
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {
			 if(temp_arr[j] == Mix_4K_mapdir[(map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE].ppn){
//				�����ж�Ӧ�ķ���ҳ ,����֮ǰ�ķ�����ͬ��һ������ҳ����Ҫ����µķ���ҳ	
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
//	����SLC�еķ���ҳ
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
//		����ҳ���µ�SLC
		SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[0],free_SLC_page_no[0]) & (~S_OFF_MASK_SECT), map_copy, 1, 2);
		free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	}

	//	ͳ�ƺϲ�����
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
	//	�����ݿ����
	SLC_nand_erase(victim_blk_no);
	//	�˴�����ĥ��������
	
	return 0;
}


/************************************
* Name : MLC_gc_run(small, mapdir_flag)
* Date : 2018-11-24 
* author: zhoujie 
* param: 
* return value:
* Function : ��MLC��free �����С����ֵ���ú��������������ݰ���
* Attention: MLC ֻ�����ݿ��������ڷ���ҳ����
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

//  ����̰��ԭ���ȡMLC�Ĳ�����
  	victim_blk_no = MLC_opm_gc_cost_benefit();
  	memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  	s = k = M_OFF_F_SECT(free_MLC_page_no[small]);
  	if(!((s == 0) && (k == 0))){
    	printf("s && k should be 0\n");
    	exit(0);
  	}
//   ȷ������������������ҳ��Ϊ����ҳ״̬
	for( q = 0; q < M_PAGE_NUM_PER_BLK; q++){
		if(MLC_nand_blk[victim_blk_no].page_status[q] != 0){
			printf("MLC page status must be data page\n
					something corrupted MLC blkno=%d\n",victim_blk_no);
			assert(0);
		}
	}
//  ���ζ�ȡ��Ч����ҳ ��������ǰ��д���
	pos = 0;
  	merge_count = 0;
  	for (i = 0; i < M_PAGE_NUM_PER_BLK; i++) 
  	{
		valid_flag = MLC_nand_oob_read( M_SECTOR(victim_blk_no, i * M_SECT_NUM_PER_PAGE));
//		���������Ч
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
//			ȷ������д��λ��
			benefit += MLC_gc_get_free_blk(small, mapdir_flag);
//          MLC ֻ������ҳ����
			ASSERT(MLC_nand_blk[victim_blk_no].page_status[i] == 0);
			ASSERT(Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].IsSLC == 0);
			Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = M_BLK_PAGE_NO_SECT(M_SECTOR(free_MLC_blk_no[small], free_MLC_page_no[small]));
			Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[0])].IsSLC = 0;

            MLC_nand_page_write(M_SECTOR(free_MLC_blk_no[small],free_MLC_page_no[small]) & (~M_OFF_MASK_SECT), copy_lsn, 1, 1);
            free_MLC_page_no[small] += M_SECT_NUM_PER_PAGE;
			
//			�жϷ������Ƿ����CMT,�ӳٸ��·�����		
            if((Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_REAL) 
				|| (Mix_4K_opagemap[M_BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_GHOST)) {
              delay_flash_update++;
            }
            else {
              map_arr[pos] = copy_lsn[s];
              pos++;
            } 
			
        }//��Ч���ݿ���
    }//end-for

//  ����ҳ���£�ȷ����Щӳ����ͬ��һ������ҳһ�����
	for(i=0;i < M_PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1; //֮���Ķ�����Ҫ���µķ���ҳ��SLC��
	}
	
	k=0;
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {
			 if(temp_arr[j] == Mix_4K_mapdir[(map_arr[i]/UPN_SECT_NUM_PER_PAGE)/MIX_MAP_ENTRIES_PER_PAGE].ppn){
//				�����ж�Ӧ�ķ���ҳ ,����֮ǰ�ķ�����ͬ��һ������ҳ����Ҫ����µķ���ҳ	
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
//	����SLC�еķ���ҳ
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
//		����ҳ���µ�SLC
		SLC_nand_page_write(S_SECTOR(free_SLC_blk_no[0],free_SLC_page_no[0]) & (~S_OFF_MASK_SECT), map_copy, 1, 2);
		free_MLC_page_no[0] += M_SECT_NUM_PER_PAGE;
	}
//	ͳ�ƺϲ�����
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
//	�����ݿ����
	MLC_nand_erase(victim_blk_no);
//	�˴�����ĥ��������

	return (benefit+1);
}


/************************************
* Name : MLC_opm_gc_cost_benefit
* Date : 2018-11-24
* author: zhoujie 
* param: 
* return value: MLC GC ѡ��Ŀ��
* Function : ����̰��ԭ��ѡ�����Чҳ����MLC���п����
* Attention: û�д�����ĥ�����ʱ��ѡ��
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
* return value: SLC GC ѡ��Ŀ��
* Function : ����̰��ԭ��ѡ�����Чҳ����SLC���п����
* Attention: û�д�����ĥ�����ʱ��ѡ��
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

