   /* 
 * Contributors: Youngjae Kim (youkim@cse.psu.edu)
 *               Aayush Gupta (axg354@cse.psu.edu)
 * 
 * In case if you have any doubts or questions, kindly write to: youkim@cse.psu.edu 
 *
 * This source file implements the DFTL FTL scheme.
 * The detail algorithm for the DFTL can be obtainable from 
 * "DFTL: A Flash Translation Layer Employing Demand-based * Selective Caching of Page-level Address Mapping", ASPLOS, 2009".
 * 
 */

#include <stdlib.h>
#include <string.h>

#include "flash.h"
#include "dftl.h"
#include "ssd_interface.h"
#include "disksim_global.h"

//磨损均衡阈值的几种选择方案
#define STATIC_THRESHOLD 0
#define DYNAMIC_THRESHOLD 1
#define AVE_ADD_N_VAR 2
#define static_wear_threshold 20
//阈值相关系数
#define N_wear_var 4
//动态阈值更新的周期
#define Session_Cycle 1000
double wear_beta=0.1;
//开启SW_level算法的标志位
int SW_level_flag = 1;
int Wear_Threshold_Type=AVE_ADD_N_VAR;

#ifdef DEBUG
int debug_cycle2=1000;
int debug_count2;
#endif

struct omap_dir *mapdir;

blk_t extra_blk_num;
_u32 free_blk_no[2];
_u16 free_page_no[2];

// add zhoujie 11-9
static _u32 wear_src_blk_no,wear_target_blk_no;
static _u16 wear_src_page_no,wear_target_page_no;
int stat_wear_gc_called_num;
double total_wear_gc_overhead_time;

extern int merge_switch_num;
extern int merge_partial_num;
extern int merge_full_num;
extern int page_num_for_2nd_map_table;
int stat_gc_called_num;
double total_gc_overhead_time;

int map_pg_read=0;

_u32 SW_Level_Find_GC_blk_no()
{
	int blk_s, blk_e, i;
	int blk_ecn, min_ecn = 9999999999;
	int min_blk = -1;
	int mod_num;
	int loop_count=0;
	mod_num = 1 << SW_level_K ;
	
	if(SW_level_Fcnt >= SW_level_BET_Size ){
		SW_Level_BET_Value_Reset();
	}
	while(1){
		while(1){
			if( SW_level_Findex >= SW_level_BET_Size){
				SW_level_Findex = 0;
			}
			SW_level_Findex ++ ;
			if( SW_level_BET_arr[SW_level_Findex] == 0){
				break;
			}
		}//第一层循环找到对应的bit位K个块集合
		//遍历K个块级中的ECN最小块选取
		blk_s = SW_level_Findex * mod_num;
		blk_e = (SW_level_Findex + 1) * mod_num;
		for( i = blk_s; i < blk_e && i < nand_blk_num ; i++){
			if( i == free_blk_no[0] || i == free_blk_no[1]){
				continue;
			}
			blk_ecn = nand_blk[i].state.ec;
			if (blk_ecn >= 9999999999 ){
				printf("nand blk [i] ecn is over limit\n",i);
				assert(0);
			}
			if ( blk_ecn < min_ecn){
				min_ecn = blk_ecn;
				min_blk = i;
			}
		}//end for
		if ( min_blk != -1){
//			ASSERT(nand_blk[min_blk].ipc > 0);
			break;
		}
#ifdef DEBUG
		loop_count++;
		if(loop_count > 3){
		  printf("loop count is %d\t reset\n",loop_count);
		  SW_Level_BET_Value_Reset();
		}
#endif 
	}
	return min_blk;
}



_u32 opm_gc_cost_benefit()
{
  int max_cb = 0;
  int blk_cb;

  _u32 max_blk = -1, i;

  for (i = 0; i < nand_blk_num; i++) {
    if(i == free_blk_no[0] || i == free_blk_no[1]){
      continue;
    }

    blk_cb = nand_blk[i].ipc;

    
    if (blk_cb > max_cb) {
      max_cb = blk_cb;
      max_blk = i;
    }
  }

  ASSERT(max_blk != -1);
  ASSERT(nand_blk[max_blk].ipc > 0);
  return max_blk;
}

/*
* add zhoujie 11-9
* 触发磨损均衡
*
*/
void opm_wear_level(int target_blk_no)
{

	int merge_count;
	int i,z, j,m,map_flag,q;
	int k,old_flag,temp_arr[PAGE_NUM_PER_BLK],temp_arr1[PAGE_NUM_PER_BLK],map_arr[PAGE_NUM_PER_BLK]; 
	int valid_flag,pos;
	_u32 copy_lsn[SECT_NUM_PER_PAGE], copy[SECT_NUM_PER_PAGE];
	_u16 valid_sect_num,  l, s;
	_u32 old_ppn,new_ppn;

	sect_t s_lsn;	// starting logical sector number
    sect_t s_psn; // starting physical sector number 
	
	//确定选择交换的冷块数据
//	wear_src_blk_no=find_switch_cold_blk_method1(target_blk_no);
	wear_src_blk_no=find_switch_cold_blk_method2(target_blk_no);
	wear_target_blk_no=target_blk_no;
	
	// target-blk-no nand_blk state free must to be 0
	nand_blk[target_blk_no].state.free=0;
	free_blk_num--;
	
	wear_src_page_no=0;
	wear_target_page_no=0;

	memset(copy_lsn, 0xFF, sizeof (copy_lsn));
	
	// 确认搬移的块其中的数据页状态是一致的
	map_flag= -1;
	for( q = 0; q < PAGE_NUM_PER_BLK; q++){
		if(nand_blk[wear_src_blk_no].page_status[q] == 1){ //map block
#ifdef DEBUG
     	   // test debug print zhoujie
	    	printf("wear level block gcc select Map blk no: %d\n",wear_src_blk_no);
#endif
			for( q = 0; q  < PAGE_NUM_PER_BLK; q++) {
				if(nand_blk[wear_src_blk_no].page_status[q] == 0 ){
					printf("something corrupted1=%d",wear_src_blk_no);
        		}
      		}
      		map_flag = 0;
      		break;
    	} 
    	else if(nand_blk[wear_src_blk_no].page_status[q] == 0){ //data block
#ifdef DEBUG
		//test debug print zhoujie
			printf("wear level block gcc select Data blk no: %d\n",wear_src_blk_no);
#endif
			for( q = 0; q  < PAGE_NUM_PER_BLK; q++) {
        		if(nand_blk[wear_src_blk_no].page_status[q] == 1 ){
          			printf("something corrupted2=%d",wear_src_blk_no);
        		}
      		}
      		map_flag = 1;
      		break;
    	}
  	}

	ASSERT ( map_flag== 0 || map_flag == 1);
	
 	pos = 0;
 	merge_count = 0;
//	将冷块的数据更新到对应的磨损块 
 	for (i = 0; i < PAGE_NUM_PER_BLK; i++) 
 	{
 		//首先模拟块读 
		//读取整个物理块，按页一个个读取
		valid_flag = nand_oob_read( SECTOR(wear_src_blk_no, i * SECT_NUM_PER_PAGE));
//      有效数据填入		
   		if(valid_flag == 1)
   		{
	   		valid_sect_num = nand_page_read( SECTOR(wear_src_blk_no, i * SECT_NUM_PER_PAGE), copy, 1);
	   		merge_count++;
	   		ASSERT(valid_sect_num == 4);
	   		k=0;
	   		for (j = 0; j < valid_sect_num; j++) {
		 		copy_lsn[k] = copy[j];
		 		k++;
	   		}
//			 这里要区分更新的是数据块还是翻译页冷块
		 	if(nand_blk[wear_src_blk_no].page_status[i] == 1)
		 	{	
//		 		翻译页修改对应的GTD --> mapdir
		   		mapdir[(copy_lsn[0]/SECT_NUM_PER_PAGE)].ppn	= BLK_PAGE_NO_SECT(SECTOR(wear_target_blk_no, wear_target_page_no));
		   		opagemap[copy_lsn[0]/SECT_NUM_PER_PAGE].ppn = BLK_PAGE_NO_SECT(SECTOR(wear_target_blk_no, wear_target_page_no));
//				TODO 确认后续的nand page write 没有问题		
		   		nand_page_write(SECTOR(wear_target_blk_no,wear_target_page_no) & (~OFF_MASK_SECT), copy_lsn, 1, 2);
		   		wear_target_page_no+= SECT_NUM_PER_PAGE;
		 	}
		 	else{
//             选择的是数据块冷块
//				保留旧的ppn，便于后续的标志位修改
				old_ppn=opagemap[BLK_PAGE_NO_SECT(copy_lsn[0])].ppn;
//				debug printf
				if(old_ppn != BLK_PAGE_NO_SECT(SECTOR(wear_src_blk_no, i * SECT_NUM_PER_PAGE))){
					printf("debug :old ppn:%d\t BLK_PAGE_NO_SECT:%d\n",old_ppn,BLK_PAGE_NO_SECT(SECTOR(wear_src_blk_no, i * SECT_NUM_PER_PAGE)));
				}
				
		   		opagemap[BLK_PAGE_NO_SECT(copy_lsn[0])].ppn = BLK_PAGE_NO_SECT(SECTOR(wear_target_blk_no, wear_target_page_no));
				new_ppn=BLK_PAGE_NO_SECT(SECTOR(wear_target_blk_no, wear_target_page_no));
				
		   		nand_page_write(SECTOR(wear_target_blk_no, wear_target_page_no) & (~OFF_MASK_SECT), copy_lsn, 1, 1);
		   		wear_target_page_no+= SECT_NUM_PER_PAGE;
//            一般根据选冷块的原则是不会出现数据页映射项在CMT中的情况，因此延迟更新delay_flash_update 一般不累加
		   		if((opagemap[BLK_PAGE_NO_SECT(copy_lsn[0])].map_status == MAP_REAL) || (opagemap[BLK_PAGE_NO_SECT(copy_lsn[0])].map_status == MAP_GHOST)) {
			 		delay_flash_update++;
					nand_ppn_2_lpn_in_CMT_arr[old_ppn]=0;
					nand_ppn_2_lpn_in_CMT_arr[new_ppn]=1;
		   		}
		   		else {
//				后面更新对应的翻译页		
			 		map_arr[pos] = copy_lsn[0];
			 		pos++;
		   		} 
			}
   		}else{
   			//无效数据也填入，避免出现fpc不为0的块无法回收
   			s_lsn = nand_blk[wear_src_blk_no].sect[i*SECT_NUM_PER_PAGE].lsn;
			for(k = 0 ; k < SECT_NUM_PER_PAGE ; k++){
				copy_lsn[k] = s_lsn+k ;
			}
//			便于后面的页面无效化		
			s_psn = SECTOR(wear_target_blk_no, wear_target_page_no) & (~OFF_MASK_SECT);
//			先写入无效数据		
			if(nand_blk[wear_src_blk_no].page_status[i] == 1){
				nand_page_write(SECTOR(wear_target_blk_no, wear_target_page_no) & (~OFF_MASK_SECT), copy_lsn, 1, 2);
			}
			else{
				nand_page_write(SECTOR(wear_target_blk_no, wear_target_page_no) & (~OFF_MASK_SECT), copy_lsn, 1, 1);
			}
			wear_target_page_no+= SECT_NUM_PER_PAGE;
//			之后把页面无效化
			for(k = 0; k<SECT_NUM_PER_PAGE; k++){
      			nand_invalidate(s_psn + k, s_lsn + k);
    		} 
   		}
 	}//end-for
	if(nand_blk[wear_target_blk_no].fpc !=0 ){
#ifdef DEBUG
		printf("nand blk %d is not write full\n",wear_target_blk_no);
#endif 
	}

	
//	处理后续的数据页翻译项的更新
	for(i=0;i < PAGE_NUM_PER_BLK;i++) {
		temp_arr[i]=-1;
	}
	k=0;
//	这段就是将属于同一翻译页的不同翻译项整合在一起
	for(i =0 ; i < pos; i++) {
		old_flag = 0;
		for( j = 0 ; j < k; j++) {
//			根据翻译项---> 对应的翻译页---> 翻译页对应的物理页ppn		
			if(temp_arr[j] == mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn) {
				if(temp_arr[j] == -1){
					printf("something wrong");
					ASSERT(0);
				}
				old_flag = 1;
				break;
		 	}
		}
		if( old_flag == 0 ) {
			//temp_arr村的是物理翻译页号
		 	temp_arr[k] = mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn;
			// temp_arr存的是更新的数据页逻辑扇区地址
		 	temp_arr1[k] = map_arr[i];
		 	k++;
		}
		else
	  		save_count++;
	}//end-for
//	确定好要更新的翻译页下发更新	
	for ( i=0; i < k; i++) {
		if (free_page_no[0] >= SECT_NUM_PER_BLK) {
			if((free_blk_no[0] = nand_get_free_blk(1)) == -1){
				printf("we are in big trouble shudnt happen");
			}
			free_page_no[0] = 0;
		}
		nand_page_read(temp_arr[i]*SECT_NUM_PER_PAGE,copy,1);

		for(m = 0; m<SECT_NUM_PER_PAGE; m++){
			nand_invalidate(mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn*SECT_NUM_PER_PAGE+m, copy[m]);
		} 
		nand_stat(OOB_WRITE);
		mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn  = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));
		opagemap[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));
		nand_page_write(SECTOR(free_blk_no[0],free_page_no[0]) & (~OFF_MASK_SECT), copy, 1, 2);
		free_page_no[0] += SECT_NUM_PER_PAGE;
	}
	
//	统计合并开销
   	if(merge_count == 0 ) 
    	merge_switch_num++;
  	else if(merge_count > 0 && merge_count < PAGE_NUM_PER_BLK)
    	merge_partial_num++;
  	else if(merge_count == PAGE_NUM_PER_BLK)
    	merge_full_num++;
  	else if(merge_count > PAGE_NUM_PER_BLK){
    	printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,PAGE_NUM_PER_BLK);
    	ASSERT(0);
  	}
//	擦除旧的冷数据块
	nand_erase(wear_src_blk_no);
	
 
}//end-func



size_t opm_read(sect_t lsn, sect_t size, int mapdir_flag)
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int sect_num;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 

  sect_t lsns[SECT_NUM_PER_PAGE];

  ASSERT(lpn < opagemap_num);
  ASSERT(lpn + size_page <= opagemap_num);

  memset (lsns, 0xFF, sizeof (lsns));

  sect_num = (size < SECT_NUM_PER_PAGE) ? size : SECT_NUM_PER_PAGE;

  if(mapdir_flag == 2){
    s_psn = mapdir[lpn].ppn * SECT_NUM_PER_PAGE;
  }
  else s_psn = opagemap[lpn].ppn * SECT_NUM_PER_PAGE;

  s_lsn = lpn * SECT_NUM_PER_PAGE;

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) {
    lsns[i] = s_lsn + i;
  }

  if(mapdir_flag == 2){
    map_pg_read++;
  }
  size = nand_page_read(s_psn, lsns, 0);

  ASSERT(size == SECT_NUM_PER_PAGE);

  return sect_num;
}

int opm_gc_get_free_blk(int small, int mapdir_flag)
{
  if (free_page_no[small] >= SECT_NUM_PER_BLK) {

    free_blk_no[small] = nand_get_free_blk(1);

    free_page_no[small] = 0;

    return -1;
  }
  
  return 0;
}

int opm_gc_run(int small, int mapdir_flag)
{
  blk_t victim_blk_no;
  int merge_count;
  int i,z, j,m,q, benefit = 0;
  int k,old_flag,temp_arr[PAGE_NUM_PER_BLK],temp_arr1[PAGE_NUM_PER_BLK],map_arr[PAGE_NUM_PER_BLK]; 
  int valid_flag,pos;

  _u32 copy_lsn[SECT_NUM_PER_PAGE], copy[SECT_NUM_PER_PAGE];
  _u16 valid_sect_num,  l, s;
  _u32 old_ppn,new_ppn;


  if (SW_level_flag ){
#ifdef DEBUG
    if(debug_count2 % debug_cycle2 == 0){
  		printf("SW-BET-Size is %d\t SW-K:%d\t SW-T:%d\n",SW_level_BET_Size,
												 	 			SW_level_K,
												 	 			SW_level_T);
		printf("SW-reset num is %d\t SW-gc-called-num is %d\n",SW_level_reset_num,
																SW_level_GC_called_num);
		printf("SW-level-Fcnt is %d\t SW-level-Ecnt is %d\n",SW_level_Fcnt,
															 SW_level_Ecnt);
    }
#endif
	if(SW_level_Fcnt >= SW_level_BET_Size){
		SW_Level_BET_Value_Reset();
	}
  }


  if( SW_level_flag && SW_level_Fcnt !=0 &&
  	 (SW_level_Ecnt / SW_level_Fcnt) > SW_level_T ){
	victim_blk_no = SW_Level_Find_GC_blk_no();
	SW_level_GC_called_num ++;
	
  }else{
  	victim_blk_no = opm_gc_cost_benefit();
  }


  memset(copy_lsn, 0xFF, sizeof (copy_lsn));

  s = k = OFF_F_SECT(free_page_no[small]);

  if(!((s == 0) && (k == 0))){
    printf("s && k should be 0\n");
    exit(0);
  }
 

  small = -1;

  for( q = 0; q < PAGE_NUM_PER_BLK; q++){
    if(nand_blk[victim_blk_no].page_status[q] == 1){ //map block
      for( q = 0; q  < 64; q++) {
        if(nand_blk[victim_blk_no].page_status[q] == 0 ){
          printf("something corrupted1=%d",victim_blk_no);
        }
      }
      small = 0;
      break;
    } 
    else if(nand_blk[victim_blk_no].page_status[q] == 0){ //data block
      for( q = 0; q  < 64; q++) {
        if(nand_blk[victim_blk_no].page_status[q] == 1 ){
          printf("something corrupted2",victim_blk_no);
        }
      }
      small = 1;
      break;
    }
  }

  ASSERT ( small == 0 || small == 1);
  pos = 0;
  merge_count = 0;
  for (i = 0; i < PAGE_NUM_PER_BLK; i++) 
  {

    valid_flag = nand_oob_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE));

    if(valid_flag == 1)
    {
        valid_sect_num = nand_page_read( SECTOR(victim_blk_no, i * SECT_NUM_PER_PAGE), copy, 1);

        merge_count++;

        ASSERT(valid_sect_num == 4);
        k=0;
        for (j = 0; j < valid_sect_num; j++) {
          copy_lsn[k] = copy[j];
          k++;
        }

          benefit += opm_gc_get_free_blk(small, mapdir_flag);

          if(nand_blk[victim_blk_no].page_status[i] == 1)
          {                       
            mapdir[(copy_lsn[s]/SECT_NUM_PER_PAGE)].ppn  = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));
            opagemap[copy_lsn[s]/SECT_NUM_PER_PAGE].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));

            nand_page_write(SECTOR(free_blk_no[small],free_page_no[small]) & (~OFF_MASK_SECT), copy_lsn, 1, 2);
            free_page_no[small] += SECT_NUM_PER_PAGE;
          }
          else{
//          add zhoujie 11-12
			old_ppn=opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].ppn;
			new_ppn=BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));
			
            opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[small], free_page_no[small]));

            nand_page_write(SECTOR(free_blk_no[small],free_page_no[small]) & (~OFF_MASK_SECT), copy_lsn, 1, 1);
            free_page_no[small] += SECT_NUM_PER_PAGE;

            if((opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_REAL) || (opagemap[BLK_PAGE_NO_SECT(copy_lsn[s])].map_status == MAP_GHOST)) {
              delay_flash_update++;
//			add zhoujie 11-12
			  nand_ppn_2_lpn_in_CMT_arr[old_ppn]=0;
			  nand_ppn_2_lpn_in_CMT_arr[new_ppn]=1;
            }
        
            else {
  
              map_arr[pos] = copy_lsn[s];
              pos++;
            } 
          }
    }
  }
  
  for(i=0;i < PAGE_NUM_PER_BLK;i++) {
      temp_arr[i]=-1;
  }
  k=0;
  for(i =0 ; i < pos; i++) {
      old_flag = 0;
      for( j = 0 ; j < k; j++) {
           if(temp_arr[j] == mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn) {
                if(temp_arr[j] == -1){
                      printf("something wrong");
                      ASSERT(0);
                }
                old_flag = 1;
                break;
           }
      }
      if( old_flag == 0 ) {
           temp_arr[k] = mapdir[((map_arr[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn;
           temp_arr1[k] = map_arr[i];
           k++;
      }
      else
        save_count++;
  }

  for ( i=0; i < k; i++) {
            if (free_page_no[0] >= SECT_NUM_PER_BLK) {
                if((free_blk_no[0] = nand_get_free_blk(1)) == -1){
                   printf("we are in big trouble shudnt happen");
                }

                free_page_no[0] = 0;
            }
     
            nand_page_read(temp_arr[i]*SECT_NUM_PER_PAGE,copy,1);

            for(m = 0; m<SECT_NUM_PER_PAGE; m++){
               nand_invalidate(mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn*SECT_NUM_PER_PAGE+m, copy[m]);
              } 
            nand_stat(OOB_WRITE);


            mapdir[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn  = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));
            opagemap[((temp_arr1[i]/SECT_NUM_PER_PAGE)/MAP_ENTRIES_PER_PAGE)].ppn = BLK_PAGE_NO_SECT(SECTOR(free_blk_no[0], free_page_no[0]));

            nand_page_write(SECTOR(free_blk_no[0],free_page_no[0]) & (~OFF_MASK_SECT), copy, 1, 2);
      
            free_page_no[0] += SECT_NUM_PER_PAGE;


  }
  if(merge_count == 0 ) 
    merge_switch_num++;
  else if(merge_count > 0 && merge_count < PAGE_NUM_PER_BLK)
    merge_partial_num++;
  else if(merge_count == PAGE_NUM_PER_BLK)
    merge_full_num++;
  else if(merge_count > PAGE_NUM_PER_BLK){
    printf("merge_count =%d PAGE_NUM_PER_BLK=%d",merge_count,PAGE_NUM_PER_BLK);
    ASSERT(0);
  }

  nand_erase(victim_blk_no);

  if (SW_level_flag == 1 ){
  	//如果开启SW算法则无需进行后面的静态交换的磨损均衡
	return (benefit + 1);
  }
 // add zhoujie 11-10 超过给定阈值开启 磨损均衡 
  Select_Wear_Level_Threshold(Wear_Threshold_Type);

  if(nand_blk[victim_blk_no].state.ec > (int)(my_global_nand_blk_wear_ave + my_wear_level_threshold)){
  		opm_wear_level( victim_blk_no );
		called_wear_num++;
#ifdef DEBUG
		switch(Wear_Threshold_Type){
				case STATIC_THRESHOLD: 
					printf("THRESHOLD TYPE is static threshold\n");
					break;
				case DYNAMIC_THRESHOLD:
					printf("THRESHOLD TYPE is dynamic threshold\n");
					break;
				case  AVE_ADD_N_VAR:
					printf("THRESHOLD TYPE is ave add %d * var\n",N_wear_var);
					break;
				default : assert(0);break;
		}
		printf("called opm wear level %d\n",called_wear_num);
#endif

   }


  return (benefit + 1);
}

/*
* add zhoujie 11-13
* 选择磨损均衡阈值调整的方式
*/
void Select_Wear_Level_Threshold(int Type)
{
 double temp;
 switch(Type){
 	case STATIC_THRESHOLD: 
		my_wear_level_threshold = static_wear_threshold;
		break;
	case DYNAMIC_THRESHOLD:
//  注意这里的循环周期计数值选取 11-15		
		if(stat_erase_num % Session_Cycle == 0 && called_wear_num != 0 ){
			temp = (called_wear_num - last_called_wear_num) *1.0 / nand_blk_num;
			my_wear_level_threshold = sqrt(100 / wear_beta) * sqrt(temp * my_wear_level_threshold);
#ifdef DEBUG
			printf("----------------------------------\n");
			printf("curr stat_erase_num is %d\t,Session Cycle is %d\n",stat_erase_num,Session_Cycle);
			printf("Session called wear num is %d\n",called_wear_num-last_called_wear_num);
			printf("my_wear_level_threshold is %lf\n",my_wear_level_threshold);
			printf("----------------------------------\n");
			
#endif
		    last_called_wear_num = called_wear_num;
		}

		break;
	case  AVE_ADD_N_VAR:
		nand_blk_ecn_std_var_static();
		my_wear_level_threshold = N_wear_var * my_global_nand_blk_wear_var;
		break;
	default : break;
 }
}

size_t opm_write(sect_t lsn, sect_t size, int mapdir_flag)  
{
  int i;
  int lpn = lsn/SECT_NUM_PER_PAGE; // logical page number
  int size_page = size/SECT_NUM_PER_PAGE; // size in page 
  int ppn;
  int small;
  int old_ppn,new_ppn;

  sect_t lsns[SECT_NUM_PER_PAGE];
  int sect_num = SECT_NUM_PER_PAGE;

  sect_t s_lsn;	// starting logical sector number
  sect_t s_psn; // starting physical sector number 
  sect_t s_psn1;


  ASSERT(lpn < opagemap_num);
  ASSERT(lpn + size_page <= opagemap_num);

  s_lsn = lpn * SECT_NUM_PER_PAGE;


  if(mapdir_flag == 2) //map page
    small = 0;
  else if ( mapdir_flag == 1) //data page
    small = 1;
  else{
    printf("something corrupted");
    exit(0);
  }

  if (free_page_no[small] >= SECT_NUM_PER_BLK) 
  {

    if ((free_blk_no[small] = nand_get_free_blk(0)) == -1) 
    {
      int j = 0;

      while (free_blk_num < min_fb_num){
        j += opm_gc_run(small, mapdir_flag);
      }
      opm_gc_get_free_blk(small, mapdir_flag);
    } 
    else {
      free_page_no[small] = 0;
    }
  }

  memset (lsns, 0xFF, sizeof (lsns));
  
  s_psn = SECTOR(free_blk_no[small], free_page_no[small]);

  if(s_psn % 4 != 0){
    printf("s_psn: %d\n", s_psn);
  }

  ppn = s_psn / SECT_NUM_PER_PAGE;

  if (opagemap[lpn].free == 0) {
//  add zhoujie 11-12
	old_ppn=opagemap[lpn].ppn;
	nand_ppn_2_lpn_in_CMT_arr[old_ppn]=0;
//	printf("old - ppn: %d\n",old_ppn);
	
    s_psn1 = opagemap[lpn].ppn * SECT_NUM_PER_PAGE;
	
    for(i = 0; i<SECT_NUM_PER_PAGE; i++){
      nand_invalidate(s_psn1 + i, s_lsn + i);
    } 
    nand_stat(3);
  }
  else {
    opagemap[lpn].free = 0;
  }

  for (i = 0; i < SECT_NUM_PER_PAGE; i++) 
  {
    lsns[i] = s_lsn + i;
  }

  if(mapdir_flag == 2) {
    mapdir[lpn].ppn = ppn;
    opagemap[lpn].ppn = ppn;
  }
  else {
// 数据页 add zhoujie 11-12
	if(opagemap[lpn].map_status == MAP_REAL || opagemap[lpn].map_status == MAP_GHOST ){
//		printf("new ppn:%d\n",ppn);
		nand_ppn_2_lpn_in_CMT_arr[ppn]=1;
	}
    opagemap[lpn].ppn = ppn;
  }

  free_page_no[small] += SECT_NUM_PER_PAGE;

  nand_page_write(s_psn, lsns, 0, mapdir_flag);

  return sect_num;
}

void opm_end()
{
  if (opagemap != NULL) {
    free(opagemap);
    free(mapdir);
  }
  
  opagemap_num = 0;
}

void opagemap_reset()
{
  cache_hit = 0;
  flash_hit = 0;
  disk_hit = 0;
  evict = 0;
  delay_flash_update = 0; 
  read_count =0;
  write_count=0;
  map_pg_read=0;
  save_count = 0;
}

int opm_init(blk_t blk_num, blk_t extra_num)
{
  int i;
  int mapdir_num;

  opagemap_num = blk_num * PAGE_NUM_PER_BLK;

  //create primary mapping table
  opagemap = (struct opm_entry *) malloc(sizeof (struct opm_entry) * opagemap_num);

  mapdir = (struct omap_dir *)malloc(sizeof(struct omap_dir) * opagemap_num / MAP_ENTRIES_PER_PAGE); 

  if ((opagemap == NULL) || (mapdir == NULL)) {
    return -1;
  }

  mapdir_num = (opagemap_num / MAP_ENTRIES_PER_PAGE);

  if((opagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
    printf("opagemap_num % MAP_ENTRIES_PER_PAGE is not zero\n"); 
    mapdir_num++;
  }

  memset(opagemap, 0xFF, sizeof (struct opm_entry) * opagemap_num);
  memset(mapdir,  0xFF, sizeof (struct omap_dir) * mapdir_num);

  //youkim: 1st map table 
  TOTAL_MAP_ENTRIES = opagemap_num;

  for(i = 0; i<TOTAL_MAP_ENTRIES; i++){
    opagemap[i].cache_status = 0;
    opagemap[i].cache_age = 0;
    opagemap[i].map_status = 0;
    opagemap[i].map_age = 0;
    opagemap[i].update = 0;
  }

  extra_blk_num = extra_num;

  free_blk_no[0] = nand_get_free_blk(0);
  free_page_no[0] = 0;
  free_blk_no[1] = nand_get_free_blk(0);
  free_page_no[1] = 0;

  //initialize variables
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
// 相关参数的初始化
  my_wear_level_threshold=10;
  called_wear_num=0;
  last_called_wear_num=0;
  //update 2nd mapping table
  for(i = 0; i<mapdir_num; i++){
    ASSERT(MAP_ENTRIES_PER_PAGE == 512);
    opm_write(i*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 2);
  }

  /*
  for(i = mapdir_num; i<(opagemap_num - mapdir_num - (extra_num * PAGE_NUM_PER_BLK)); i++){
    opm_write(i*SECT_NUM_PER_PAGE, SECT_NUM_PER_PAGE, 1);
  }
  */

  // update dm_table
  /*
  int j;
  for(i = mapdir_num; i<(opagemap_num - mapdir_num - (extra_num * PAGE_NUM_PER_BLK)); i++){
      for(j=0; j < SECT_NUM_PER_PAGE;j++)
        dm_table[ (i*SECT_NUM_PER_PAGE) + j] = DEV_FLASH;
  }
  */
  
  return 0;
}

int opm_invalid(int secno)
{
  int lpn = secno/SECT_NUM_PER_PAGE + page_num_for_2nd_map_table;	
  int s_lsn = lpn * SECT_NUM_PER_PAGE;
  int i, s_psn1;

  s_psn1 = opagemap[lpn].ppn * SECT_NUM_PER_PAGE;
  for(i = 0; i<SECT_NUM_PER_PAGE; i++){
      nand_invalidate(s_psn1 + i, s_lsn + i);
  }
  opagemap[lpn].ppn = -1;
  opagemap[lpn].map_status = 0;
  opagemap[lpn].map_age = 0;
  opagemap[lpn].update = 0;

  return SECT_NUM_PER_PAGE;

}

struct ftl_operation opm_operation = {
  init:  opm_init,
  read:  opm_read,
  write: opm_write,
  end:   opm_end
};
  
struct ftl_operation * opm_setup()
{
  return &opm_operation;
}
