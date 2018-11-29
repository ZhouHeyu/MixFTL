/* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *   
 * This source code provides page-level FTL scheme. 
 * 
 * Acknowledgement: We thank Jeong Uk Kang by sharing the initial version 
 * of sector-level FTL source code. 
 * 
 */

#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "flash.h"
#include "ssd_interface.h"

#define random(x) ( rand() % (x))
_u32 nand_blk_num;

//zhoujie
static _u32 last_blk_pc;
static int Min_N_Prime,Liner_S,Liner_L;
static int my_all_nand_ecn_counts;
double my_global_nand_blk_wear_ave;
double my_global_nand_blk_wear_std;
double my_global_nand_blk_wear_var;

//add zhoujie 11-16
//SW 算法调整的两个参数
int SW_level_K=3;
int SW_level_T=5;


#ifdef DEBUG
//zhoujie 11-13
int debug_cycle1=1000;
static int debug_count1;
#endif

double my_global_no_free_nand_blk_wear_ave;

static int my_min_nand_wear_ave=1;
_u8  pb_size;
struct nand_blk_info *nand_blk;

extern int SLC_to_SLC_num;
extern int SLC_to_MLC_num;
_u32 nand_SLC_blk_num,nand_MLC_blk_num;//用在opm_gc_cost_benefit()
struct SLC_nand_blk_info *SLC_nand_blk;
struct MLC_nand_blk_info *MLC_nand_blk;
struct SLC_nand_blk_info *head;
struct SLC_nand_blk_info *tail;

int MLC_MIN_ERASE;
int SLC_MIN_ERASE;
int MIN_ERASE;

/**************** NAND STAT **********************/
void nand_stat(int option)
{ 
    switch(option){

      case PAGE_READ:
        stat_read_num++;
        flash_read_num++;
        break;

      case PAGE_WRITE :
        stat_write_num++;
        flash_write_num++;
        break;

      case OOB_READ:
        stat_oob_read_num++;
        flash_oob_read_num++;
        break;

      case OOB_WRITE:
        stat_oob_write_num++;
        flash_oob_write_num++;
        break;

      case BLOCK_ERASE:
        stat_erase_num++;
        flash_erase_num++;
        break;

      case GC_PAGE_READ:
        stat_gc_read_num++;
        flash_gc_read_num++;
        break;
    
      case GC_PAGE_WRITE:
        stat_gc_write_num++;
        flash_gc_write_num++;
        break;
		
	  case SLC_PAGE_READ:
	  	SLC_stat_read_num++;
		SLC_flash_read_num++;
		break;
		
	  case SLC_PAGE_WRITE:
		SLC_stat_write_num++;
		SLC_flash_write_num++;
		break;
		
	  case SLC_OOB_READ:
		SLC_stat_oob_read_num++;
		SLC_flash_oob_read_num++;
		break;
		
	  case SLC_OOB_WRITE:
		SLC_stat_oob_write_num++;
		SLC_flash_oob_write_num++;
		break;
		
	  case SLC_BLOCK_ERASE:
		SLC_stat_erase_num++;
		SLC_flash_erase_num++;
		break;
		
	  case SLC_GC_PAGE_READ:
		SLC_stat_gc_read_num++;
		SLC_flash_gc_read_num++;
		break;
			
	  case SLC_GC_PAGE_WRITE:
		SLC_stat_gc_write_num++;
		SLC_flash_gc_write_num++;
		break;
		
	  case MLC_PAGE_READ:
		MLC_stat_read_num++;
		MLC_flash_read_num++;
		break;
		
	  case MLC_PAGE_WRITE :
		MLC_stat_write_num++;
		MLC_flash_write_num++;
		break;
		
	  case MLC_OOB_READ:
		MLC_stat_oob_read_num++;
		MLC_flash_oob_read_num++;
		break;

	  case MLC_OOB_WRITE:
		MLC_stat_oob_write_num++;
		MLC_flash_oob_write_num++;
		break;

	  case MLC_BLOCK_ERASE:
		MLC_stat_erase_num++;
		MLC_flash_erase_num++;
		break;

	  case MLC_GC_PAGE_READ:
		MLC_stat_gc_read_num++;
		MLC_flash_gc_read_num++;
		break;
	
	  case MLC_GC_PAGE_WRITE:
		MLC_stat_gc_write_num++;
		MLC_flash_gc_write_num++;
		break;


      default: 
        ASSERT(0);
        break;
    }
}

void nand_stat_reset()
{
  stat_read_num = stat_write_num = stat_erase_num = 0;
  stat_gc_read_num = stat_gc_write_num = 0;
  stat_oob_read_num = stat_oob_write_num = 0;
}

void nand_stat_print(FILE *outFP)
{
  fprintf(outFP, "\n");
  fprintf(outFP, "FLASH STATISTICS\n");
  fprintf(outFP, "------------------------------------------------------------\n");
  fprintf(outFP, " Page read (#): %8u   ", stat_read_num);
  fprintf(outFP, " Page write (#): %8u   ", stat_write_num);
  fprintf(outFP, " Block erase (#): %8u\n", stat_erase_num);
//  fprintf(outFP, " OOREAD  %8u   ", stat_oob_read_num);
//  fprintf(outFP, " OOWRITE %8u\n", stat_oob_write_num);
  fprintf(outFP, " GC page read (#): %8u   ", stat_gc_read_num);
  fprintf(outFP, " GC page write (#): %8u\n", stat_gc_write_num);
  fprintf(outFP, "Wear Level GC called Num (#): %d\n", called_wear_num);
  fprintf(outFP, "------------------------------------------------------------\n");
}

/*
* add zhoujie 11-8
* 素数判断公式
*/
int isPrime(int n){
	int aqr=0,i;
	if(n <= 1) return 0;
	aqr = (int)sqrt(1.0*n);
	for(i = 2; i <= aqr; i++)
	{
		if (n % i == 0) return 0;
	}
	return 1;
}
/*
* add zhoujie 11-9
* 找到大于N的最小素数
*/
int FindMinPrime(int n){
	int res=n;
	while(1){
		if(isPrime(res))
			break;
		res++;
	}
	return res;
}

/*
*add zhoujie 11-7
*print nand_stat_ec to analysis
*/
void nand_ecn_print(FILE * outFP)
{
	int i;
	fprintf(outFP,"\n");
	fprintf(outFP,"NAND ECN STATISTICS\n");
	fprintf(outFP, "------------------------------------------------------------\n");
	for(i=0;i<nand_blk_num;i++) {
		fprintf(outFP,"NANDBLK %d ECN:\t %u\n",i,nand_blk[i].state.ec);
	}
	fprintf(outFP, "------------------------------------------------------------\n");
	
}

/*
*add zhoujie 11-8
* 统计全局块的平均块擦除次数
*
*/
void nand_blk_ecn_ave_static()
{
	int i;
	_u32 all_ecn=0;
	for(i=0;i<nand_blk_num;i++) {
		all_ecn+=nand_blk[i].state.ec;
		//最大值越界报错处理(超出u32的最大地址区间4,294,967,296)
		if(all_ecn >= 4294967296){
			printf("all ecn sum is over limit 4294967296\n");
			assert(0);
		}
	}
	my_global_nand_blk_wear_ave=all_ecn*1.0/nand_blk_num;
}

/*
* add zhoujie 11-13
* 统计全局块的磨损方差值
*/
void nand_blk_ecn_std_var_static()
{
  int i;
  double temp = 0.0;
  nand_blk_ecn_ave_static();
  for(i = 0 ; i < nand_blk_num ; i++){
	temp += (nand_blk[i].state.ec - my_global_nand_blk_wear_ave) * \
		(nand_blk[i].state.ec - my_global_nand_blk_wear_ave);
  }
  my_global_nand_blk_wear_std = temp / nand_blk_num;
  my_global_nand_blk_wear_var = sqrt(my_global_nand_blk_wear_std);
  
}



/*
*add zhoujie 11-12
* 统计全局块的平均块擦除次数
*
*/
void nand_no_free_blk_ecn_ave_static()
{
	int i,j=0;
	_u32 all_ecn=0;
	for(i=0;i<nand_blk_num;i++) {
		if(nand_blk[i].state.free == 0 ){
			all_ecn+=nand_blk[i].state.ec;
			j++;
			//最大值越界报错处理(超出u32的最大地址区间4,294,967,296)
			if(all_ecn >= 4294967296){
				printf("all ecn sum is over limit 4294967296\n");
				assert(0);
			}
		}
	}
	my_global_no_free_nand_blk_wear_ave=all_ecn*1.0/j;
}


/*
* add zhoujie 11-10
* static pbn ppn to lpn entry in CMT count
*/
void Static_pbn_map_entry_in_CMT()
{
	int i,j,k;
	int blk_s,blk_e;
	for( i=0 ; i < nand_blk_num; i++){
		blk_s=PAGE_NUM_PER_BLK * i;
		blk_e=PAGE_NUM_PER_BLK * (i+1);
		k=0;
		for( j = blk_s ; j < blk_e ; j++){
			if ( nand_ppn_2_lpn_in_CMT_arr[j] == 1 ){
				k++;	
			}
		}
		nand_pbn_2_lpn_in_CMT_arr[i] = k;
		nand_blk_bit_map[i] = k;
	}
}



/*
* add zhoujie 11-8
* 选择一个冷块进行数据交换（方法1）
* 生成一个初始的随机数，利用代数取余方式进行遍历
*/

_u32 find_switch_cold_blk_method1(int victim_blk_no)
{
	int i,min_bitmap_value = PAGE_NUM_PER_BLK;
	
// add zhoujie 11-12
	Static_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_blk_num; i++){
		if(nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = nand_blk_bit_map[i];
		//一般情况下，min_bitmap_value=0
	}
	if(min_bitmap_value > 0){
		printf("nand_blk_bit_map value all larger than 0!\n");
	}
	while(1){
		Liner_L=(Liner_S+Liner_L) % Min_N_Prime;
		if(Liner_L < nand_blk_num ) {
			// init time my_global_nand_blk_wear_ave is 0!
			if (my_global_nand_blk_wear_ave < my_min_nand_wear_ave && nand_blk[Liner_L].fpc ==0 
				&& nand_blk[Liner_L].state.free == 0 && nand_blk[Liner_L].ipc == 0 ){
				break;
			}
			if(nand_blk[Liner_L].state.ec < my_global_nand_blk_wear_ave+my_min_nand_wear_ave
				&& nand_blk_bit_map[Liner_L] == min_bitmap_value 
				&& nand_blk[Liner_L].state.free ==0 && nand_blk[Liner_L].fpc == 0 && nand_blk[Liner_L].ipc == 0){
					break;
			}
		}

	}
	return (_u32)Liner_L;
}


/*
*add zhoujie 11-8
* 选择一个冷块进行数据交换(方法2)
* 按一个历史值记录值进行标记，大循环遍历
*/
_u32 find_switch_cold_blk_method2(int victim_blk_no)
{
	int i,min_bitmap_value = PAGE_NUM_PER_BLK;
	Static_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_blk_num; i++){
		if(nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = nand_blk_bit_map[i];
		//一般情况下，min_bitmap_value=0
	}
	if(min_bitmap_value > 0){
		printf("nand_blk_bit_map value all larger than 0!\n");
	}
// 循环遍历之前的块ECN小于 平均值的冷块（映射项不在CMT中)
	while(1){
		if( last_blk_pc >= nand_blk_num )
			last_blk_pc = 0;

		if (my_global_nand_blk_wear_ave < my_min_nand_wear_ave && nand_blk[last_blk_pc].fpc ==0 
				&& nand_blk[last_blk_pc].state.free == 0){
				break;
		}
		
		if( nand_blk[last_blk_pc].state.ec < (my_global_nand_blk_wear_ave+my_min_nand_wear_ave)
			&& nand_blk_bit_map[last_blk_pc] == min_bitmap_value 
			&& nand_blk[last_blk_pc].state.free ==0 && nand_blk[last_blk_pc].fpc ==0 ) {
			break;
		}
		
		last_blk_pc++;
	}
	return last_blk_pc;
}


/**************** NAND INIT **********************/
int nand_init (_u32 blk_num, _u8 min_free_blk_num)
{
  _u32 blk_no;
  int i;
  int mod_num=1;

  nand_end();

  nand_blk = (struct nand_blk_info *)malloc(sizeof (struct nand_blk_info) * blk_num);

  if (nand_blk == NULL) 
  {
    return -1;
  }
  memset(nand_blk, 0xFF, sizeof (struct nand_blk_info) * blk_num);
// add zhoujie
  nand_blk_bit_map=(int *)malloc(sizeof(int)*blk_num);
  if (nand_blk == NULL)
  {
  	return -1;
  }
  memset(nand_blk_bit_map,0,sizeof(int) * blk_num);
// add zhoujie 11-10
  nand_pbn_2_lpn_in_CMT_arr=(int *)malloc( sizeof(int) * blk_num);
  nand_ppn_2_lpn_in_CMT_arr=(int *)malloc( sizeof(int) * blk_num * PAGE_NUM_PER_BLK);
  if (nand_pbn_2_lpn_in_CMT_arr == NULL || nand_ppn_2_lpn_in_CMT_arr == NULL )
  {
  	return -1;
  }
  memset(nand_ppn_2_lpn_in_CMT_arr,0,sizeof(int) * blk_num * PAGE_NUM_PER_BLK);
  memset(nand_pbn_2_lpn_in_CMT_arr,0,sizeof(int) * blk_num );

// 初始化磨损的差异阈值
  Min_N_Prime=FindMinPrime(blk_num);
  Liner_S=(int)blk_num*0.5;
  Liner_L=Liner_S;
  my_all_nand_ecn_counts=0;
  
#ifdef DEBUG
  debug_count1=0;
  printf("blk_num is %d\tMinPrime is %d\n",blk_num,Min_N_Prime);
#endif

  last_blk_pc=0;

  nand_blk_num = blk_num;
// SW level磨损均衡相关变量值初始化
  mod_num = mod_num << SW_level_K;
  printf("mod num is %d\n",mod_num);
  SW_level_BET_Size=nand_blk_num / mod_num;
  if( nand_blk_num % mod_num != 0 ){
		SW_level_BET_Size += 1;
  }

  SW_level_BET_arr = (int *) malloc( sizeof(int) * SW_level_BET_Size );
  if (SW_level_BET_arr == NULL ){
	return -1;
  }
  
  SW_Level_BET_Value_Reset();
  SW_level_reset_num = 0;
  SW_level_GC_called_num = 0;

  pb_size = 1;
  min_fb_num = min_free_blk_num;
  MLC_min_fb_num = min_free_blk_num;
  SLC_min_fb_num = min_free_blk_num;
  for (blk_no = 0; blk_no < blk_num; blk_no++) {
    nand_blk[blk_no].state.free = 1;
    nand_blk[blk_no].state.ec = 0;
    nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
    nand_blk[blk_no].ipc = 0;
    nand_blk[blk_no].lwn = -1;


    for(i = 0; i<SECT_NUM_PER_BLK; i++){
      nand_blk[blk_no].sect[i].free = 1;
      nand_blk[blk_no].sect[i].valid = 0;
      nand_blk[blk_no].sect[i].lsn = -1;
    }

    for(i = 0; i < PAGE_NUM_PER_BLK; i++){
      nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    }
  }
  free_blk_num = nand_blk_num;

  free_blk_idx =0;

  nand_stat_reset();
  
  return 0;
}

/**************** NAND END **********************/
void nand_end ()
{
  nand_blk_num = 0;
  if (nand_blk != NULL) {
    nand_blk = NULL;
  }
  if (nand_blk_bit_map != NULL){
	nand_blk_bit_map=NULL;
  }
  if(nand_pbn_2_lpn_in_CMT_arr != NULL ){
	nand_pbn_2_lpn_in_CMT_arr = NULL;
  }
  if(nand_ppn_2_lpn_in_CMT_arr != NULL ){
	nand_ppn_2_lpn_in_CMT_arr = NULL;
  }
}

/**************** NAND OOB READ **********************/
int nand_oob_read(_u32 psn)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    if(nand_blk[pbn].sect[pin + i].free == 0){

      if(nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;
        break;
      }
      else{
        valid_flag = -1;
        break;
      }
    }
    else{
      valid_flag = 0;
      break;
    }
  }

  nand_stat(OOB_READ);
  
  return valid_flag;
}

void break_point()
{
  printf("break point\n");
}

/**************** NAND PAGE READ **********************/
_u8 nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number	
  _u16  pin = IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_blk_num){
    printf("psn: %d, pbn: %d, nand_blk_num: %d\n", psn, pbn, nand_blk_num);
  }

  ASSERT(OFF_F_SECT(psn) == 0);
  if(nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < nand_blk_num; i++){
      for(j =0; j < SECT_NUM_PER_BLK ;j++){
        if(nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {

      if((nand_blk[pbn].sect[pin + i].free == 0) &&
         (nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num == 3){
      for(i = 0; i<SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, nand_blk[pbn].sect[pin+i].free, nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (nand_blk[pbn].sect[pin + i].free == 0 &&
            nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(GC_PAGE_READ);
    }
  } else {
    nand_stat(PAGE_READ);
  }
  
  return valid_sect_num;
}

/**************** NAND PAGE WRITE **********************/
_u8 nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;


  if(pbn >= nand_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_blk_num);
  ASSERT(OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    nand_blk[pbn].page_status[pin/SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(nand_blk[pbn].sect[pin + i].free == 1);
      
      nand_blk[pbn].sect[pin + i].free = 0;			
      nand_blk[pbn].sect[pin + i].valid = 1;			
      nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      nand_blk[pbn].fpc--;  
      nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(GC_PAGE_WRITE);
  } else {
    nand_stat(PAGE_WRITE);
  }

  return valid_sect_num;
}


/**************** NAND BLOCK ERASE **********************/
void nand_erase (_u32 blk_no)
{
  int i;
  int mod_num;
  mod_num = 1 << SW_level_K ;
  ASSERT(blk_no < nand_blk_num);

  ASSERT(nand_blk[blk_no].fpc <= SECT_NUM_PER_BLK);

  if(nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(nand_blk[blk_no].state.free == 0);

  nand_blk[blk_no].state.free = 1;
  nand_blk[blk_no].state.ec++;
  nand_blk[blk_no].fpc = SECT_NUM_PER_BLK;
  nand_blk[blk_no].ipc = 0;
  nand_blk[blk_no].lwn = -1;


  for(i = 0; i<SECT_NUM_PER_BLK; i++){
    nand_blk[blk_no].sect[i].free = 1;
    nand_blk[blk_no].sect[i].valid = 0;
    nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < PAGE_NUM_PER_BLK; i++){
    nand_blk[blk_no].page_status[i] = -1;
  }

  free_blk_num ++;
  // add zhoujie
  my_all_nand_ecn_counts ++;
  my_global_nand_blk_wear_ave = my_all_nand_ecn_counts*1.0/nand_blk_num;

//add zhoujie 11-16 SW_level
 if ( SW_level_BET_arr [ blk_no/mod_num] == 0 ){
	SW_level_BET_arr [ blk_no/mod_num] = 1;
	SW_level_Fcnt += 1;
 }
 SW_level_Ecnt++;

#ifdef DEBUG
	// add zhoujie 11-13
  debug_count1++;
  if(debug_count1 % debug_cycle1 == 0){
  	nand_blk_ecn_std_var_static();
  	printf("nand blk ecn ave:%lf\t nand blk ecn std:%lf\t nand blk var:%lf\n",
  												my_global_nand_blk_wear_ave,
  												my_global_nand_blk_wear_std,
  												my_global_nand_blk_wear_var );
  }
#endif

  nand_stat(BLOCK_ERASE);
}

/**************** NAND INVALIDATE **********************/
void nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = BLK_F_SECT(psn);
  _u16 pin = IND_F_SECT(psn);
  if(pbn > nand_blk_num ) return;

  ASSERT(pbn < nand_blk_num);
  ASSERT(nand_blk[pbn].sect[pin].free == 0);
  if(nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(nand_blk[pbn].sect[pin].valid == 1);

  if(nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(nand_blk[pbn].sect[pin].lsn == lsn);
  
  nand_blk[pbn].sect[pin].valid = 0;
  nand_blk[pbn].ipc++;

  ASSERT(nand_blk[pbn].ipc <= SECT_NUM_PER_BLK);

}



_u32 nand_get_free_blk (int isGC) 
{
  _u32 blk_no = -1, i;
  int flag = 0,flag1=0;

  MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && (min_fb_num >= free_blk_num)) {
    //printf("min_fb_num: %d\n", min_fb_num);
    return -1;
  }

  for(i = 0; i < nand_blk_num; i++) 
  {
    if (nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( nand_blk[i].state.ec < MIN_ERASE ) {
            blk_no = i;
            MIN_ERASE = nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  if(flag1 != 1){
    printf("no free block left=%d",free_blk_num);
    
  ASSERT(0);
  }
  if ( flag == 1) {
        flag = 0;
        ASSERT(nand_blk[blk_no].fpc == SECT_NUM_PER_BLK);
        ASSERT(nand_blk[blk_no].ipc == 0);
        ASSERT(nand_blk[blk_no].lwn == -1);
        nand_blk[blk_no].state.free = 0;

        free_blk_idx = blk_no;
        free_blk_num--;

        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}

void SW_Level_BET_Value_Reset()
{
	memset(SW_level_BET_arr,0,sizeof(int) * SW_level_BET_Size );
	SW_level_Ecnt = 0;
	SW_level_Fcnt = 0;
//  产生随机随机数	
	SW_level_Findex = random(SW_level_BET_Size-1);
	SW_level_reset_num ++;
}



/********************混合SSD相关函数代码******************/
/*
* Name : Mix_nand_init
* Date : 2018-11-22 
* author: zhoujie 
* param: SLC_blk_num(all SLC physical blk num:user + system op)
*		 MLC_blk_num(all MLC physical blk num:user + system op)
*        min_free_blk_num(trigger to GC threshold)
* return value:
* Function :
* Attention:
*/
int Mix_nand_init (_u32 SLC_blk_num,_u32 MLC_blk_num, _u8 min_free_blk_num)
{
	_u32 blk_no;
	int i;
	Mix_nand_end();
	SLC_nand_blk = (struct SLC_nand_blk_info *)malloc(sizeof (struct SLC_nand_blk_info) * SLC_blk_num);
	MLC_nand_blk = (struct MLC_nand_blk_info *)malloc(sizeof (struct MLC_nand_blk_info) * MLC_blk_num);
	if ((SLC_nand_blk == NULL)||(MLC_nand_blk == NULL)) 
	{
		return -1;
	}
	
	head=tail=&SLC_nand_blk[0];
	memset(SLC_nand_blk, 0xFF, sizeof (struct SLC_nand_blk_info) * SLC_blk_num);
	memset(MLC_nand_blk, 0xFF, sizeof (struct MLC_nand_blk_info) * MLC_blk_num);
	
	nand_SLC_blk_num = SLC_blk_num;
	nand_MLC_blk_num = MLC_blk_num;

	pb_size = 1;
	SLC_min_fb_num = min_free_blk_num;
	MLC_min_fb_num = min_free_blk_num;

	for (blk_no = 0; blk_no < nand_SLC_blk_num; blk_no++) {
    	SLC_nand_blk[blk_no].state.free = 1;
    	SLC_nand_blk[blk_no].state.ec = 0;
    	SLC_nand_blk[blk_no].fpc = S_SECT_NUM_PER_BLK;
    	SLC_nand_blk[blk_no].ipc = 0;
    	SLC_nand_blk[blk_no].lwn = -1;

		for(i = 0; i<S_SECT_NUM_PER_BLK; i++){
      		SLC_nand_blk[blk_no].sect[i].free = 1;
      		SLC_nand_blk[blk_no].sect[i].valid = 0;
      		SLC_nand_blk[blk_no].sect[i].lsn = -1;
    	}

    	for(i = 0; i < S_PAGE_NUM_PER_BLK; i++){
      		SLC_nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    	}
  	}
	
  	for (blk_no =0 ; blk_no < nand_MLC_blk_num; blk_no++) {
    	MLC_nand_blk[blk_no].state.free = 1;
    	MLC_nand_blk[blk_no].state.ec = 0;
    	MLC_nand_blk[blk_no].fpc = M_SECT_NUM_PER_BLK;
    	MLC_nand_blk[blk_no].ipc = 0;
    	MLC_nand_blk[blk_no].lwn = -1;
		
    	for(i = 0; i<M_SECT_NUM_PER_BLK; i++){
      		MLC_nand_blk[blk_no].sect[i].free = 1;
      		MLC_nand_blk[blk_no].sect[i].valid = 0;
      		MLC_nand_blk[blk_no].sect[i].lsn = -1;
    	}

    	for(i = 0; i < M_PAGE_NUM_PER_BLK; i++){
      		MLC_nand_blk[blk_no].page_status[i] = -1; // 0: data, 1: map table
    	}
  	}

	free_SLC_blk_num = nand_SLC_blk_num;
	free_MLC_blk_num = nand_MLC_blk_num;	
	free_SLC_blk_idx =0 ;
	free_MLC_blk_idx =0 ;
	
	Mix_nand_stat_reset();
  	return 0;
}


/*
* Name : Mix_nand_end
* Date : 2018-11-22 
* author: zhoujie 
* param: 
* return value:
* Function : to free Mix_nand_init malloc memory
* Attention:
*/
void Mix_nand_end ()
{
  if (SLC_nand_blk != NULL) {
    SLC_nand_blk = NULL;
  }
  if (MLC_nand_blk != NULL) {
    MLC_nand_blk = NULL;
  }
}

/*
* Name : Mix_nand_stat_reset
* Date : 2018-11-22 
* author: zhoujie 
* param: void
* return value: void
* Function : to reset  SLC and MLC final print static value
* Attention:
*/
void Mix_nand_stat_reset()
{
  SLC_stat_read_num = SLC_stat_write_num = SLC_stat_erase_num = 0;
  SLC_stat_gc_read_num = SLC_stat_gc_write_num = 0;
  SLC_stat_oob_read_num = SLC_stat_oob_write_num = 0;
  MLC_stat_read_num = MLC_stat_write_num = MLC_stat_erase_num = 0;
  MLC_stat_gc_read_num = MLC_stat_gc_write_num = 0;
  MLC_stat_oob_read_num = MLC_stat_oob_write_num = 0;
}

/*
* Name : Mix_nand_stat_reset
* Date : 2018-11-22 
* author: zhoujie 
* param: FILE outfile point
* return value: void
* Function : print SLC and MLC final static value to outfile(XXX.outv)
* Attention:
*/
void Mix_nand_stat_print(FILE *outFP)
{
	int i;
	fprintf(outFP, "\n");
	fprintf(outFP, "MIX FLASH STATISTICS\n");
	fprintf(outFP,"-----------------------------------------------------------\n");
	fprintf(outFP, " SLC_Page read (#): %8u	", SLC_stat_read_num);
	fprintf(outFP, " SLC_Page write (#): %8u	 ", SLC_stat_write_num);
	fprintf(outFP,"-----------------------------------------------------------\n");
	fprintf(outFP, " SLC_Block erase (#): %8u\n", SLC_stat_erase_num);
	fprintf(outFP, " SLC_GC page read (#): %8u   ", SLC_stat_gc_read_num);
	fprintf(outFP, " SLC_GC page write (#): %8u\n", SLC_stat_gc_write_num);
	fprintf(outFP, "------------------------------------------------------------\n");
	fprintf(outFP, "------------------------------------------------------------\n");
	fprintf(outFP, " MLC_Page read (#): %8u	", MLC_stat_read_num);
	fprintf(outFP, " MLC_Page write (#): %8u	 ", MLC_stat_write_num);
	fprintf(outFP,"-----------------------------------------------------------\n");
	fprintf(outFP, " MLC_Block erase (#): %8u\n", MLC_stat_erase_num);
	fprintf(outFP, " MLC__GC page read (#): %8u	", MLC_stat_gc_read_num);
	fprintf(outFP, " MLC_GC page write (#): %8u\n", MLC_stat_gc_write_num);
	fprintf(outFP, "------------------------------------------------------------\n");
	fprintf(outFP, "------------------------------------------------------------\n");
	fprintf(outFP, " SLC_to_SLC_num (#): %8u	 ", SLC_to_SLC_num);
	fprintf(outFP, " SLC_to_MLC_num (#): %8u\n", SLC_to_MLC_num);
	fprintf(outFP, "------------------------------------------------------------\n");
	
	fprintf(outFP, "-----------SLC inner Wear level static----------\n");
	for(i=0; i < nand_SLC_blk_num;i++){
		fprintf(outFP,"SLCNAND BLKNO%d ECN :%d\n",i,SLC_nand_blk[i].state.ec);
	}
	fprintf(outFP, "-----------MLC inner Wear level static----------\n");
	for(i=0; i < nand_SLC_blk_num;i++){
		fprintf(outFP,"MLCNAND BLKNO%d ECN :%d\n",i,SLC_nand_blk[i].state.ec);
	}
}


/*
* Name : SLC_nand_page_read
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
*		 lsn(逻辑扇区号)
*        isGC(系统当前是否处于GC状态，0：normal read，1:GC data read,2:?)
* return value: valid_sect_num (数据页中有效的扇区个数：完整有效(4))
*/
_u8 SLC_nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = S_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_SLC_blk_num){
    printf("psn: %d, pbn: %d, nand_SLC_blk_num: %d\n", psn, pbn, nand_SLC_blk_num);
  }

  ASSERT(S_OFF_F_SECT(psn) == 0);
  if(SLC_nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < nand_SLC_blk_num ; i++){
      for(j =0; j < S_SECT_NUM_PER_BLK;j++){
        if(SLC_nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(SLC_nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {

      if((SLC_nand_blk[pbn].sect[pin + i].free == 0) &&
         (SLC_nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = SLC_nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }
// 数据没有扇区对齐，表示相应的数据写入有问题
    if(valid_sect_num == 3){
      for(i = 0; i<S_SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, SLC_nand_blk[pbn].sect[pin+i].free, SLC_nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }
// isGC 为2 表示？
  } else if (isGC == 2) {
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (SLC_nand_blk[pbn].sect[pin + i].free == 0 &&
            SLC_nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(SLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(SLC_nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(SLC_nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(SLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(SLC_GC_PAGE_READ);
    }
  } else {
    nand_stat(SLC_PAGE_READ);
  }
  
  return valid_sect_num;
}

/*
* Name : MLC_nand_page_read
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(22)+page-bit(7)+sect-bit(3))
*		 lsn(逻辑扇区号)
*        isGC(系统当前是否处于GC状态，0：normal read，1:GC data read,2:?)
* return value: valid_sect_num (数据页中有效的扇区个数：完整有效(8))
* Function : 
*/

_u8 MLC_nand_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = M_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0;

  if(pbn >= nand_MLC_blk_num){
    printf("psn: %d, pbn: %d, nand_MLC_blk_num: %d\n", psn, pbn, nand_MLC_blk_num);
  }

  ASSERT(M_OFF_F_SECT(psn) == 0);
  if(MLC_nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < nand_MLC_blk_num ; i++){
      for(j =0; j < M_SECT_NUM_PER_BLK ;j++){
        if(MLC_nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(MLC_nand_blk[pbn].state.free == 0);	// block should be written with something

  if (isGC == 1) {
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {

      if((MLC_nand_blk[pbn].sect[pin + i].free == 0) &&
         (MLC_nand_blk[pbn].sect[pin + i].valid == 1)) {
        lsns[valid_sect_num] = MLC_nand_blk[pbn].sect[pin + i].lsn;
        valid_sect_num++;
      }
    }

    if(valid_sect_num != M_SECT_NUM_PER_PAGE){
      for(i = 0; i<M_SECT_NUM_PER_PAGE; i++){
        printf("pbn: %d, pin %d: %d, free: %d, valid: %d\n", 
            pbn, i, pin+i, MLC_nand_blk[pbn].sect[pin+i].free, MLC_nand_blk[pbn].sect[pin+i].valid);

      }
      exit(0);
    }

  } else if (isGC == 2) {
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {
        if (MLC_nand_blk[pbn].sect[pin + i].free == 0 &&
            MLC_nand_blk[pbn].sect[pin + i].valid == 1) {
          ASSERT(MLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
          valid_sect_num++;
        } else {
          lsns[i] = -1;
        }
      }
    }
  } 

  else { // every sector should be "valid", "not free"   
    for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
      if (lsns[i] != -1) {

        ASSERT(MLC_nand_blk[pbn].sect[pin + i].free == 0);
        ASSERT(MLC_nand_blk[pbn].sect[pin + i].valid == 1);
        ASSERT(MLC_nand_blk[pbn].sect[pin + i].lsn == lsns[i]);
        valid_sect_num++;
      }
      else{
        printf("lsns[%d]: %d shouldn't be -1\n", i, lsns[i]);
        exit(0);
      }
    }
  }
  
  if (isGC) {
    if (valid_sect_num > 0) {
      nand_stat(MLC_GC_PAGE_READ);
    }
  } else {
    nand_stat(MLC_PAGE_READ);
  }
  
  return valid_sect_num;
}


/*
* Name : SLC_nand_oob_read
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
* return value: valid_flag (1：valid, -1:unvalid, 0:free)
* Function : read oob data to sure SLC page(2K) state
*/

int SLC_nand_oob_read(_u32 psn)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = S_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_SLC_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < S_SECT_NUM_PER_PAGE; i++) {
    if(SLC_nand_blk[pbn].sect[pin + i].free == 0){

      if(SLC_nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;
        break;
      }
      else{
        valid_flag = -1;
        break;
      }
    }
    else{
      valid_flag = 0;
      break;
    }
  }

  nand_stat(SLC_OOB_READ);
  
  return valid_flag;
}


/*******************************************
* Name : MLC_nand_oob_read
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(22)+page-bit(7)+sect-bit(3))
* return value: valid_flag (1：valid, -1:unvalid, 0:free)
* Function : read oob data to sure MLC page(4K) state
**********************************************/
int MLC_nand_oob_read(_u32 psn)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = M_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i, valid_flag = 0;

  ASSERT(pbn < nand_MLC_blk_num);	// pbn shouldn't exceed max nand block number 

  for (i = 0; i < M_SECT_NUM_PER_PAGE; i++) {
    if(MLC_nand_blk[pbn].sect[pin + i].free == 0){

      if(MLC_nand_blk[pbn].sect[pin + i].valid == 1){
        valid_flag = 1;
        break;
      }
      else{
        valid_flag = -1;
        break;
      }
    }
    else{
      valid_flag = 0;
      break;
    }
  }

  nand_stat(MLC_OOB_READ);
  
  return valid_flag;
}

/*****************************
* Name : SLC_nand_erase
* Date : 2018-11-21 
* author: zhoujie 
* param: blk_no ( blk number-index in  nand_SLC_blk(physical-blk))
* return value: void
* Function : Reset 'SLC_nand_blk[blk_no]' all flag and add ec value
* Attention: the 'SLC_nand_blk[blk_no]' must free ;
******************************/
void SLC_nand_erase (_u32 blk_no)
{
  int i;

  ASSERT(blk_no < nand_SLC_blk_num);

  ASSERT(SLC_nand_blk[blk_no].fpc <= S_SECT_NUM_PER_BLK);

  if(SLC_nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(SLC_nand_blk[blk_no].state.free == 0);
  
  SLC_nand_blk[blk_no].state.free = 1;
  SLC_nand_blk[blk_no].state.ec++;
  SLC_nand_blk[blk_no].fpc = S_SECT_NUM_PER_BLK;
  SLC_nand_blk[blk_no].ipc = 0;
  SLC_nand_blk[blk_no].lwn = -1;

  
  for(i = 0; i<S_SECT_NUM_PER_BLK; i++){
    SLC_nand_blk[blk_no].sect[i].free = 1;
    SLC_nand_blk[blk_no].sect[i].valid = 0;
    SLC_nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < S_PAGE_NUM_PER_BLK; i++){
    SLC_nand_blk[blk_no].page_status[i] = -1;
  }
  printf("擦除的块号＝%d\n",blk_no);
  free_SLC_blk_num++;

  nand_stat(SLC_BLOCK_ERASE);
}

/*****************************
* Name : MLC_nand_erase
* Date : 2018-11-21 
* author: zhoujie 
* param: blk_no ( blk number-index in  nand_MLC_blk(physical-blk))
* Function : Reset 'MLC_nand_blk[blk_no]' all flag and add ec value
* Attention: the 'MLC_nand_blk[blk_no]' must free ;
******************************/
void MLC_nand_erase (_u32 blk_no)
{
  int i;

  ASSERT(blk_no < nand_MLC_blk_num);

  ASSERT(MLC_nand_blk[blk_no].fpc <= M_SECT_NUM_PER_BLK);

  if(MLC_nand_blk[blk_no].state.free != 0){ printf("debug\n"); }

  ASSERT(MLC_nand_blk[blk_no].state.free == 0);

  MLC_nand_blk[blk_no].state.free = 1;
  MLC_nand_blk[blk_no].state.ec++;
  MLC_nand_blk[blk_no].fpc = M_SECT_NUM_PER_BLK;
  MLC_nand_blk[blk_no].ipc = 0;
  MLC_nand_blk[blk_no].lwn = -1;


  for(i = 0; i<M_SECT_NUM_PER_BLK; i++){
    MLC_nand_blk[blk_no].sect[i].free = 1;
    MLC_nand_blk[blk_no].sect[i].valid = 0;
    MLC_nand_blk[blk_no].sect[i].lsn = -1;
  }

  //initialize/reset page status 
  for(i = 0; i < M_PAGE_NUM_PER_BLK; i++){
    MLC_nand_blk[blk_no].page_status[i] = -1;
  }

  free_MLC_blk_num++;

  nand_stat(MLC_BLOCK_ERASE);
}


/*****************************
* Name : SLC_nand_invalidate
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
*		 lsn(逻辑扇区号)
* return value: void
* Function : Reset SLC_nand_blk[pbn] state to unvalid !
* Attention: SLC_nand_blk[pbn].sect[pin] state must free != 0 and valid == 1
*			 SLC_nand_blk[pbn].sect[pin].lsn == lsn !!!
******************************/
void SLC_nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = S_BLK_F_SECT(psn);
  _u16 pin = S_IND_F_SECT(psn);
  if(pbn > nand_SLC_blk_num ) return;

  ASSERT(pbn < nand_SLC_blk_num);
  ASSERT(SLC_nand_blk[pbn].sect[pin].free == 0);
  if(SLC_nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(SLC_nand_blk[pbn].sect[pin].valid == 1);

  if(SLC_nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(SLC_nand_blk[pbn].sect[pin].lsn == lsn);
  
  SLC_nand_blk[pbn].sect[pin].valid = 0;
  SLC_nand_blk[pbn].ipc++;

  ASSERT(SLC_nand_blk[pbn].ipc <= S_SECT_NUM_PER_BLK);
}

/*****************************
* Name : MLC_nand_invalidate
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(22)+page-bit(7)+sect-bit(3))
*		 lsn(逻辑扇区号)
* return value: void
* Function : Reset MLC_nand_blk[pbn] state to unvalid !
* Attention: MLC_nand_blk[pbn].sect[pin] state must free != 0 and valid == 1
*			 MLC_nand_blk[pbn].sect[pin].lsn == lsn !!!
******************************/
void MLC_nand_invalidate (_u32 psn, _u32 lsn)
{
  _u32 pbn = M_BLK_F_SECT(psn);
  _u16 pin = M_IND_F_SECT(psn);
  if(pbn > nand_MLC_blk_num ) return;

  ASSERT(pbn < nand_MLC_blk_num);
  ASSERT(MLC_nand_blk[pbn].sect[pin].free == 0);
  if(MLC_nand_blk[pbn].sect[pin].valid != 1) { printf("debug"); }
  ASSERT(MLC_nand_blk[pbn].sect[pin].valid == 1);

  if(MLC_nand_blk[pbn].sect[pin].lsn != lsn){
    ASSERT(0);
  }

  ASSERT(MLC_nand_blk[pbn].sect[pin].lsn == lsn);
  
  MLC_nand_blk[pbn].sect[pin].valid = 0;
  MLC_nand_blk[pbn].ipc++;

  ASSERT(MLC_nand_blk[pbn].ipc <= M_SECT_NUM_PER_BLK);
}

/*****************************
* Name : SLC_nand_page_write
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
*		 lsns(写入的数据：real data is continuous lsn address size is 4)
*		 map_flag(2 : map pages write , !2 : data pages write)
*		 isGC (0 : no GC write , !0 : GC write)
* return value: success write sect numbers (4)
* Function : 
* Attention: SLC_nand_blk[pbn].sect[pin + i].free == 1
******************************/
_u8 SLC_nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = S_IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;

#ifdef DEBUG
  printf("pbn %d\n",pbn);
#endif

  if(pbn >= nand_SLC_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_SLC_blk_num);
  ASSERT(S_OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        SLC_nand_blk[pbn].page_status[pin/S_SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    SLC_nand_blk[pbn].page_status[pin/S_SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <S_SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(SLC_nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(SLC_nand_blk[pbn].sect[pin + i].free == 1);
      
      SLC_nand_blk[pbn].sect[pin + i].free = 0;			
      SLC_nand_blk[pbn].sect[pin + i].valid = 1;			
      SLC_nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      SLC_nand_blk[pbn].fpc--;  
      SLC_nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(SLC_nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(SLC_GC_PAGE_WRITE);
  } else {
    nand_stat(SLC_PAGE_WRITE);
  }

  return valid_sect_num;
}

/*****************************
* Name : MLC_nand_page_write
* Date : 2018-11-21 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(22)+page-bit(7)+sect-bit(3))
		 *lsns(写入的数据：real data is continuous lsn address size is 8)
		 map_flag(2 : map pages write , !2 : data pages write)
		 isGC (0 : no GC write , !0 : GC write)
* return value: success write sect numbers (8)
* Function : 
* Attention: MLC_nand_blk[pbn].sect[pin + i].free == 1
******************************/
_u8 MLC_nand_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = M_BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = M_IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, valid_sect_num = 0;
  
  
  if(pbn >= nand_MLC_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_MLC_blk_num);
  ASSERT(M_OFF_F_SECT(psn) == 0);

  if(map_flag == 2) {
        MLC_nand_blk[pbn].page_status[pin/M_SECT_NUM_PER_PAGE] = 1; // 1 for map table
  }
  else{
    MLC_nand_blk[pbn].page_status[pin/M_SECT_NUM_PER_PAGE] = 0; // 0 for data 
  }

  for (i = 0; i <M_SECT_NUM_PER_PAGE; i++) {

    if (lsns[i] != -1) {

      if(MLC_nand_blk[pbn].state.free == 1) {
        printf("blk num = %d",pbn);
      }

      ASSERT(MLC_nand_blk[pbn].sect[pin + i].free == 1);
      
      MLC_nand_blk[pbn].sect[pin + i].free = 0;			
      MLC_nand_blk[pbn].sect[pin + i].valid = 1;			
      MLC_nand_blk[pbn].sect[pin + i].lsn = lsns[i];	
      MLC_nand_blk[pbn].fpc--;  
      MLC_nand_blk[pbn].lwn = pin + i;	
      valid_sect_num++;
    }
    else{
      printf("lsns[%d] do not have any lsn\n", i);
    }
  }
  
  ASSERT(MLC_nand_blk[pbn].fpc >= 0);

  if (isGC) {
    nand_stat(MLC_GC_PAGE_WRITE);
  } else {
    nand_stat(MLC_PAGE_WRITE);
  }

  return valid_sect_num;
}


/*****************************
* Name : SLC_nand_4K_data_page_write
* Date : 2018-11-22
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
*		 lsns(写入的数据：real data is continuous lsn address size is 8)
*		 map_flag(2 : map pages write , !2 : data pages write)
*		 isGC (0 : no GC write , !0 : GC write)
* return value: success write sect numbers (8)
* Function : 
* Attention: 
******************************/
_u8 SLC_nand_4K_data_page_write(_u32 psn, _u32 *lsns, _u8 isGC, int map_flag)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number with psn
  _u16  pin = S_IND_F_SECT(psn);	// sector index, page index is the same as sector index 
  int i, j,valid_sect_num = 0,temp_valid_sect_num=0;
  _u32 copy_lsns[S_SECT_NUM_PER_PAGE];
  _u32 temp_psn = psn;

  if(pbn >= nand_SLC_blk_num){
    printf("break !\n");
  }

  ASSERT(pbn < nand_SLC_blk_num);
  ASSERT(S_OFF_F_SECT(psn) == 0);
  if (map_flag == 2 || nand_blk[pbn].page_status[pin/S_SECT_NUM_PER_PAGE] == 1){
	printf("curr SLC nand 4K page support data page writes\n");
	assert(0);
  }
  if( SLC_nand_blk[pbn].fpc % 8 !=0 ){
  	printf("4k page data write blk must 8sectors contents\n");
	assert(0);
  }
  
  for (j=0; j < 2; j++){
  	temp_psn += j * S_SECT_NUM_PER_PAGE;
  	for(i=0;i < S_SECT_NUM_PER_PAGE ;i ++){
		copy_lsns[i] = lsns[i+j*S_SECT_NUM_PER_PAGE];
  	}
	temp_valid_sect_num=SLC_nand_page_write( temp_psn, copy_lsns, isGC, map_flag);
	assert (temp_valid_sect_num == S_SECT_NUM_PER_PAGE);
	valid_sect_num += temp_valid_sect_num;
  }
  return valid_sect_num;
}

/*****************************
* Name : SLC_nand_4K_data_page_read
* Date : 2018-11-22 
* author: zhoujie 
* param: psn(物理扇区号：blk-bit(24)+page-bit(6)+sect-bit(2))
*		 lsn(逻辑扇区号)
*        isGC(系统当前是否处于GC状态，0：normal read，1:GC data read,2:?)
* return value: valid_sect_num (数据页中有效的扇区个数：完整有效(8))
* Function : 
* Attention: 
******************************/
_u8 SLC_nand_4K_data_page_read(_u32 psn, _u32 *lsns, _u8 isGC)
{
  blk_t pbn = S_BLK_F_SECT(psn);	// physical block number	
  _u16  pin = S_IND_F_SECT(psn);	// page index (within the block), here page index is the same as sector index
  _u16  i,j, valid_sect_num = 0,temp_valid_sect_num = 0;
  _u32  copy_lsns[S_SECT_NUM_PER_PAGE],temp_psn = psn;
  memset(copy_lsns,-1,sizeof(_u32) * S_SECT_NUM_PER_PAGE);
  
  if (pbn != S_BLK_F_SECT(psn + S_SECT_NUM_PER_PAGE)){
		printf("4k data page must in same pbn blk\n");
		assert(0);
  }
  if(pbn >= nand_SLC_blk_num){
    printf("psn: %d, pbn: %d, nand_SLC_blk_num: %d\n", psn, pbn, nand_SLC_blk_num);
  }

  ASSERT(S_OFF_F_SECT(psn) == 0);
  if(SLC_nand_blk[pbn].state.free != 0) {
    for( i =0 ; i < nand_SLC_blk_num ; i++){
      for(j =0; j < S_SECT_NUM_PER_BLK;j++){
        if(SLC_nand_blk[i].sect[j].lsn == lsns[0]){
          printf("blk = %d",i);
          break;
        }
      }
    }
  }

  ASSERT(SLC_nand_blk[pbn].state.free == 0);	// block should be written with something
  for(j=0; j < 2 ; j++){
  	temp_psn += j * S_SECT_NUM_PER_PAGE;
  	temp_valid_sect_num = SLC_nand_page_read( temp_psn, copy_lsns, isGC);
  	for(i=0; i < S_SECT_NUM_PER_PAGE; i++){
		lsns[ j * S_SECT_NUM_PER_PAGE + i ] = copy_lsns[i];
  	}
	valid_sect_num += temp_valid_sect_num;
  }

  return valid_sect_num;
}


/*****************************
* Name : MLC_nand_get_free_blk
* Date : 2018-11-22 
* author: zhoujie 
* param: isGC(系统当前是否处于GC状态，0：normal，1:GC ,2:?)
* return value: free MLC blk_no
* Function :  to find free MLC blk_no with less ecn
* Attention: 
******************************/
_u32 MLC_nand_get_free_blk (int isGC) 
{
  _u32 blk_no = -1, i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  MLC_MIN_ERASE = 9999999;
  //in case that there is no avaible free block -> GC should be called !
  if ((isGC == 0) && ( MLC_min_fb_num >= free_MLC_blk_num)) {
#ifdef DEBUG 
    printf("MLC_min_fb_num: %d\n", MLC_min_fb_num);
    printf(" free MLC blk num: %d\n", free_MLC_blk_num);
#endif
	return -1;
  }

  for(i =0 ; i < nand_MLC_blk_num; i++) 
  {
    if (MLC_nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( MLC_nand_blk[i].state.ec < MLC_MIN_ERASE ) {
            blk_no = i;
            MLC_MIN_ERASE = MLC_nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  if(flag1 != 1){
    printf("no free block left=%d",free_MLC_blk_num);
    
  ASSERT(0);
  }
  if ( flag == 1) {
        flag = 0;
        ASSERT(MLC_nand_blk[blk_no].fpc == M_SECT_NUM_PER_BLK);
        ASSERT(MLC_nand_blk[blk_no].ipc == 0);
        ASSERT(MLC_nand_blk[blk_no].lwn == -1);
        MLC_nand_blk[blk_no].state.free = 0;
        free_MLC_blk_idx = blk_no;
        free_MLC_blk_num--;
       // printf("MLC空闲块 %d\n",free_MLC_blk_num);
        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}

/*****************************
* Name : SLC_nand_get_free_blk
* Date : 2018-11-22 
* author: zhoujie 
* param: isGC(系统当前是否处于GC状态，0：normal，1:GC ,2:?)
* return value: free SLC blk_no
* Function :  based Greedy alogrithm to find free SLC blk_no with less ecn
* Attention: 
******************************/
_u32 SLC_nand_get_free_blk (int isGC) 
{
  _u32 blk_no=-1 , i;
  int flag = 0,flag1=0;
  flag = 0;
  flag1 = 0;

  SLC_MIN_ERASE = 9999999;
  if ((isGC == 0) && (SLC_min_fb_num >= free_SLC_blk_num)) {
#ifdef DEBUG 
		  printf("SLC_min_fb_num: %d\n", SLC_min_fb_num);
		  printf(" free SLC blk num: %d\n", free_SLC_blk_num);
#endif
    return -1;
  }

  for(i = 0; i < nand_SLC_blk_num; i++) 
  {
    if (SLC_nand_blk[i].state.free == 1) {
      flag1 = 1;

      if ( SLC_nand_blk[i].state.ec < SLC_MIN_ERASE ) {
            blk_no = i;
            SLC_MIN_ERASE = SLC_nand_blk[i].state.ec;
            flag = 1;
      }
    }
  }
  
   if(flag1 != 1){
     printf("no free block left=%d",free_SLC_blk_num);
     ASSERT(0);
   }
  if ( flag == 1) {
        flag = 0;
        ASSERT(SLC_nand_blk[blk_no].fpc == S_SECT_NUM_PER_BLK);
        ASSERT(SLC_nand_blk[blk_no].ipc == 0);
        ASSERT(SLC_nand_blk[blk_no].lwn == -1);
        SLC_nand_blk[blk_no].state.free = 0;

        free_SLC_blk_idx = blk_no;
        free_SLC_blk_num--;
        printf("获取成功，块号：%d\n",blk_no);
        return blk_no;
  }
  else{
    printf("shouldn't reach...\n");
  }

  return -1;
}


