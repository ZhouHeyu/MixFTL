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

_u32 nand_blk_num;

//zhoujie
static _u32 last_blk_pc;
static int Min_N_Prime,Liner_S,Liner_L;
static int my_all_nand_ecn_counts;
double my_gloabl_nand_blk_wear_ave;
double my_global_no_free_nand_blk_wear_ave;

static int my_min_nand_wear_ave=5;
_u8  pb_size;
struct nand_blk_info *nand_blk;

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
  fprintf(outFP, "------------------------------------------------------------\n");
}

/*
* add zhoujie 11-8
* �����жϹ�ʽ
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
* �ҵ�����N����С����
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
* ͳ��ȫ�ֿ��ƽ�����������
*
*/
void nand_blk_ecn_ave_static()
{
	int i;
	_u32 all_ecn=0;
	for(i=0;i<nand_blk_num;i++) {
		all_ecn+=nand_blk[i].state.ec;
		//���ֵԽ�籨����(����u32������ַ����4,294,967,296)
		if(all_ecn >= 4294967296){
			printf("all ecn sum is over limit 4294967296\n");
			assert(0);
		}
	}
	my_gloabl_nand_blk_wear_ave=all_ecn*1.0/nand_blk_num;
}

/*
*add zhoujie 11-8
* ͳ��ȫ�ֿ��ƽ�����������
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
			//���ֵԽ�籨����(����u32������ַ����4,294,967,296)
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
* ѡ��һ�����������ݽ���������1��
* ����һ����ʼ������������ô���ȡ�෽ʽ���б���
*/

_u32 find_switch_cold_blk_method1(int victim_blk_no)
{
	int i,min_bitmap_value = PAGE_NUM_PER_BLK;
	
// add zhoujie 11-12
	Static_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_blk_num; i++){
		if(nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = nand_blk_bit_map[i];
		//һ������£�min_bitmap_value=0
	}
	if(min_bitmap_value > 0){
		printf("nand_blk_bit_map value all larger than 0!\n");
	}
	while(1){
		Liner_L=(Liner_S+Liner_L) % Min_N_Prime;
		if(Liner_L < nand_blk_num ) {
			// init time my_global_nand_blk_wear_ave is 0!
			if (my_gloabl_nand_blk_wear_ave < my_min_nand_wear_ave && nand_blk[Liner_L].fpc ==0 
				&& nand_blk[Liner_L].state.free == 0 ){
				break;
			}
			if(nand_blk[Liner_L].state.ec < my_gloabl_nand_blk_wear_ave+my_min_nand_wear_ave
				&& nand_blk_bit_map[Liner_L] == min_bitmap_value 
				&& nand_blk[Liner_L].state.free ==0 && nand_blk[Liner_L].fpc ==0 ){
					break;
			}
		}

	}
	return (_u32)Liner_L;
}


/*
*add zhoujie 11-8
* ѡ��һ�����������ݽ���(����2)
* ��һ����ʷֵ��¼ֵ���б�ǣ���ѭ������
*/
_u32 find_switch_cold_blk_method2(int victim_blk_no)
{
	int i,min_bitmap_value = PAGE_NUM_PER_BLK;
	Static_pbn_map_entry_in_CMT();
	
	for(i = 0 ;i < nand_blk_num; i++){
		if(nand_blk_bit_map[i] < min_bitmap_value)
			min_bitmap_value = nand_blk_bit_map[i];
		//һ������£�min_bitmap_value=0
	}
	if(min_bitmap_value > 0){
		printf("nand_blk_bit_map value all larger than 0!\n");
	}
// ѭ������֮ǰ�Ŀ�ECNС�� ƽ��ֵ����飨ӳ�����CMT��)
	while(1){
		if( last_blk_pc >= nand_blk_num )
			last_blk_pc = 0;

		if (my_gloabl_nand_blk_wear_ave < my_min_nand_wear_ave && nand_blk[last_blk_pc].fpc ==0 
				&& nand_blk[last_blk_pc].state.free == 0 ){
				break;
		}
		
		if( nand_blk[last_blk_pc].state.ec < (my_gloabl_nand_blk_wear_ave+my_min_nand_wear_ave)
			&& nand_blk_bit_map[last_blk_pc] == min_bitmap_value+2 
			&& nand_blk[last_blk_pc].state.free ==0 && nand_blk[last_blk_pc].fpc ==0) {
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


// ��ʼ��ĥ��Ĳ�����ֵ
  my_wear_level_threshold=20;
  Min_N_Prime=FindMinPrime(blk_num);
  Liner_S=(int)blk_num*0.5;
  Liner_L=Liner_S;
  my_all_nand_ecn_counts=0;
  
//test print
  printf("blk_num is %d\tMinPrime is %d\n",blk_num,Min_N_Prime);
  
  last_blk_pc=0;
  
  nand_blk_num = blk_num;

  pb_size = 1;
  min_fb_num = min_free_blk_num;
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
    for( i =0 ; i < 8448 ; i++){
      for(j =0; j < 256;j++){
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

  free_blk_num++;
  // add zhoujie
  my_all_nand_ecn_counts++;
  my_gloabl_nand_blk_wear_ave=my_all_nand_ecn_counts*1.0/nand_blk_num;

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

















