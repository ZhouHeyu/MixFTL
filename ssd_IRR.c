/*************************************************************
* Modle Name: IRRFTL             
* origin author:ymb        
* modified :zhoujie
* Date :02.12.2018
* email: 1395529361@qq.com
* *********************************************************/
#include "ssd_IRR.h"
#include "ssd_interface.h"

//all map entry is 4096
#define MAP_MAX_ENTRIES 4096
#define MAP_WCMT_MAX_ENTRIES 2048
#define MAP_RCMT_MAX_ENTRIES 1536
#define MAP_TPCS_ENTRIES 512
//some Mix_4K_opagemap status
#define MAP_WCMT 2
#define MAP_RCMT 3
#define MAP_TPCS 4
static int rqst = 0;
/************************************
 * 		global arr value 
 ************************************/
static int wcmt_arr[MAP_WCMT_MAX_ENTRIES] ;
static int rcmt_arr[MAP_RCMT_MAX_ENTRIES] ;
static int *cluster_arr = NULL;
static int operation_time = 0;

static int Mix_2K_page_num_for_2nd_map_table; //based on 2k map page
static int Mix_4K_page_num_for_2nd_map_table; //based on 4k map page

static int MAP_RCMT_NUM_ENTRIES;
static int MAP_WCMT_NUM_ENTRIES;
static int init_flag = 0;
int IRRFTL_Hot_Min = 0;
int IRRFTL_Cold_Min = 0;
Node *IRRFTL_WH_Head = NULL; //WCMT-hot-list
Node *IRRFTL_WC_Head = NULL;//WCMT-Cold-list

int gHotFlag = 0;
int wcmt_hot_len;
int wcmt_cold_len;

int ccw_len;
int TPCS = -1;

/**********************************
 *  		inner function
 * ********************************/
//zhoujie add function
int find_min_rcmt_entry();
int ColdWCMT_MaxCluster_WriteBack();
int Find_Clean_Entry_In_ColdWCMT();
int IRRFTL_Data_Operation(int blkno,int operation,int hot_flag);
//finish
void IRRFTL_init_arr();
void IRRFTL_Init();
int IRRFTL_Find_Victim_In_WCMT();
void IRRFTL_WCMT_Is_Full(int isrun);
void IRRFTL_RCMT_Is_Full(int isrun);
void IRRFTL_Move_RCMT2WCMT(int blkno, int operation);
void IRRFTL_Move_RCMT2MRU(int blkno, int operation);
void IRRFTL_Move_TPCS2RCMT(int blkno, int operation);
void IRRFTL_Move_TPCS2WCMT(int blkno, int operation);

void IRRFTL_Hit_WCMT(int blkno, int operation);
void IRRFTL_Hit_RCMT(int blkno, int operation);
void IRRFTL_Hit_TPCS(int blkno, int operation);

//to do
void IRRFTL_NOT_Hit_CMT(int blkno,int operation);


/****************************
 *  local function
 *  Date :03.12.2018
 *  author : zhoujie
 *  funciton: to find rcmt entry with min map age
 ****************************/
int find_min_rcmt_entry()
{
  int i,index;
  int rcmt_min = -1;
  int temp = 99999999;
  
  for(i=0; i < MAP_RCMT_MAX_ENTRIES; i++) {
	  if(rcmt_arr[i] > 0){
        if(Mix_4K_opagemap[rcmt_arr[i]].map_age <= temp) {
            rcmt_min = rcmt_arr[i];
            temp = Mix_4K_opagemap[rcmt_arr[i]].map_age;
        }
	}
  } 	
  return rcmt_min;
}

/***********************************************
 * Name: Find_Clean_Entry_In_ColdWCMT
 * Date : 03.12.2018
 * Author:zhoujie
 * param:
 * return value: Victim_index (wcmt arr index num)
 * 				 -1 : No Clean Entry in ColdWCMT
 * *********************************************/
int Find_Clean_Entry_In_ColdWCMT()
{
	int Victim_index = -1, curr_lpn;
	Node * TempNode;
	TempNode = IRRFTL_WC_Head->pre;
	while(TempNode != IRRFTL_WC_Head){
		curr_lpn = TempNode->lpn_num;
		if(Mix_4K_opagemap[curr_lpn].update == 0){
			Victim_index = search_table(wcmt_arr, MAP_WCMT_MAX_ENTRIES, curr_lpn);
			ASSERT(Victim_index != -1);
			return Victim_index;
		}  
		TempNode = TempNode->pre;
	}
	return Victim_index;
}

/***************************************
 * Name :ColdWCMT_MaxCluster_WriteBack
 * Date :03.12.2018
 * Author: zhoujie
 * param:
 * return value: 0 (sucess)
 * function:  to reset cluster_arr static value
 * 			  find map-page with max cluster-size to write back
 * 			  and reset some related mix_4K_opagemap update-flag;  
 * **************************************/
int ColdWCMT_MaxCluster_WriteBack()
{
	Node * TempNode;
	int curr_lpn, temp_index,temp_max = -1;
	int i;
	
	//将cluster_arr里的统计变量清零
	memset(cluster_arr, 0 , sizeof(int) * Mix_2K_page_num_for_2nd_map_table);
	//  reset cluster_arr static value
	TempNode = IRRFTL_WC_Head->pre;
	while(TempNode != IRRFTL_WC_Head){
		curr_lpn=TempNode->lpn_num;
		cluster_arr[(curr_lpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE] += 1;
		TempNode = TempNode->pre;
	}
	//选择最大簇的脏映射项
	for(i=0; i < Mix_2K_page_num_for_2nd_map_table; i++){
		if(temp_max < cluster_arr[i]){
			temp_max = cluster_arr[i];
			temp_index = i;
		}
	}
	//find map page write and reset mix_opagemap 
	TempNode = IRRFTL_WC_Head->pre;
	while(TempNode != IRRFTL_WC_Head){
		curr_lpn = TempNode->lpn_num;
		if((curr_lpn - Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE == temp_index){
			Mix_4K_opagemap[curr_lpn].update = 0;
		}
		TempNode = TempNode->pre;
	}
	
	// 翻译页的回写 带来的读写操作 to SLC 
	send_flash_request( (temp_index) * 4,4,1,2);
	send_flash_request( (temp_index) * 4,4,0,2);
	
	return 0;
}

/**********************
 * Name:IRRFTL_init_arr
 * Date: 02.12.2018
 * Author : zhoujie
 * param: void
 * return :void 
 * function : to reset global arr(init)
 ***********************/
void IRRFTL_init_arr()
{
    int i;
    for( i = 0; i < MAP_WCMT_MAX_ENTRIES; i++) {
        wcmt_arr[i] = -1;
    }
    for( i= 0; i < MAP_RCMT_MAX_ENTRIES; i++) {
        rcmt_arr[i] = -1;
    }
    for( i = 0; i < Mix_2K_page_num_for_2nd_map_table; i++){
        cluster_arr[i] = 0;
    }
    
    wcmt_cold_len = 0;
    wcmt_hot_len = 0;
    MAP_RCMT_NUM_ENTRIES = 0;
    MAP_WCMT_NUM_ENTRIES = 0;
    //ccw_len = 0;

}
/**********************
 * Name:IRRFTL_Init
 * Date: 02.12.2018
 * Author : zhoujie
 * param: void
 * return :void 
 * function : when init_flag = 0 to init IRRFTL
 ***********************/
void IRRFTL_Init()
{
	Mix_2K_page_num_for_2nd_map_table = (Mix_4K_opagemap_num/MIX_MAP_ENTRIES_PER_PAGE);
	if((Mix_4K_opagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
		Mix_2K_page_num_for_2nd_map_table++;//every map page 2k(all map page num)
	}
	Mix_4K_page_num_for_2nd_map_table = (int)(Mix_2K_page_num_for_2nd_map_table * 1.0 / 2 + 0.5);//based on 4K
	
	cluster_arr=(int *)malloc(sizeof(int)*Mix_2K_page_num_for_2nd_map_table);
	if(cluster_arr == NULL){
		printf("malloc for cluster is failed\n");
		assert(0);
	}
	IRRFTL_WH_Head=CreateList();
	IRRFTL_WC_Head=CreateList();
	IRRFTL_init_arr();
	
	init_flag=1;
	//需要将链表中，插入对应长度的热数据
	IRRFTL_Hot_Min = MAP_WCMT_MAX_ENTRIES * 0.7;
	IRRFTL_Cold_Min = MAP_WCMT_MAX_ENTRIES * 0.09;
}

/******************************
 * Name: IRRFTL_Data_Operation
 * Date: 03.12.2018
 * param: blkno--> (lpn ,based on 4k page)
 * 		  operation (0 :write 1:read)
 * 		  hot_flag(1:hot data,0:cold data)
 * return value:0(operation success)
 * function: to  write or read data from MixSSD
 * Attention:
 * ********************************/
int IRRFTL_Data_Operation(int blkno,int operation,int hot_flag)
{
    if(operation==0){
        write_count++;
        Mix_4K_opagemap[blkno].update = 1;
        // send request to
        if(hot_flag == 0){
			//mapdir_flag 0 -> write to MLC data
			send_flash_request(blkno*UPN_SECT_NUM_PER_PAGE,UPN_SECT_NUM_PER_PAGE,operation,0);
		}else{
			//mapdir_flag 1 -> write to SLC data
			send_flash_request(blkno*UPN_SECT_NUM_PER_PAGE,UPN_SECT_NUM_PER_PAGE,operation,1);
		}
    }
    else{
        read_count++;
        if(Mix_4K_opagemap[blkno].IsSLC == 0){
			// data storage in MLC
			send_flash_request(blkno*UPN_SECT_NUM_PER_PAGE,UPN_SECT_NUM_PER_PAGE,operation,0);
		}else{
			//data storage in SLC
			send_flash_request(blkno*UPN_SECT_NUM_PER_PAGE,UPN_SECT_NUM_PER_PAGE,operation,1);
		}
    }	
    return 0;
}


/*********************************
 * Name :IRRFTL_Find_Victim_In_WCMT
 * Date :03.12.2018
 * Author: ymb
 * param:
* return value: Victim_index (返回受害项,wcmt_arr index num)
* Attention : IRRFTL_WC_Head是写冷缓存的头指针，从尾部开始查找，是否有干净项。
*             有，则优先剔除干净项。无，则统计最大簇，整簇回写。再从头开始。
 * *******************************/
int IRRFTL_Find_Victim_In_WCMT()
{
	int Victim_index = -1;
	// first to check cold wcmt has clean entry
	Victim_index = Find_Clean_Entry_In_ColdWCMT();
	if(Victim_index == -1){
		//not has clean entry and cluster write back
		ColdWCMT_MaxCluster_WriteBack();
		Victim_index = Find_Clean_Entry_In_ColdWCMT();
		ASSERT(Victim_index != -1);
	}
	return Victim_index;
}



/*******************************
 * Name: IRRFTL_RCMT_Is_Full
 * Date: 03.12.2018
 * Author: ymb
 * param : isrun(强制执行剔除操作的标志为，1:强制执行，0:缓存满时执行)
 * return value:void
 * function: only delete map entry don't need to write map to mixssd
 * ******************************/
void IRRFTL_RCMT_Is_Full(int isrun)
{
	int rcmt_min = -1 ,pos = -1;
    if((MAP_RCMT_NUM_ENTRIES - MAP_RCMT_MAX_ENTRIES >= 0)|| (isrun == 1)){
		rcmt_min = find_min_rcmt_entry();
		ASSERT( rcmt_min != -1);
		ASSERT( Mix_4K_opagemap[rcmt_min].update != 1);
		
		//evict one entry from rcmt cache
		Mix_4K_opagemap[rcmt_min].map_status = MAP_INVALID;
		Mix_4K_opagemap[rcmt_min].map_age = 0;
		MAP_RCMT_NUM_ENTRIES -- ;
		pos = search_table(rcmt_arr, MAP_RCMT_MAX_ENTRIES, rcmt_min);
        if(pos==-1){
            printf("can not find rcmt_min:%d  in rcmt_arr\n",rcmt_min);
            assert(0);
        }
        rcmt_arr[pos]=-1;
	}
}

/*******************************
 * Name: IRRFTL_WCMT_Is_Full
 * Date: 03.12.2018
 * Author: ymb
 * param : isrun(强制执行剔除操作的标志为，1:强制执行，0:缓存满时执行)
 * return value: void
 * function:
 * ******************************/
void IRRFTL_WCMT_Is_Full(int isrun)
{
	int Victim_pos=-1, find_free_pos=-1,DelLpn;
    int temp_num = 0;
    int indexold = 0;
    Node * DelNode;
    
    if(MAP_WCMT_NUM_ENTRIES - MAP_WCMT_MAX_ENTRIES >= 0|| isrun ==1 ){
		// first delete cold list clean entry
		Victim_pos = IRRFTL_Find_Victim_In_WCMT();
		if(Victim_pos == -1){
			// cold list is empty,del hot-list
			DelNode = DeleteLRUInList(IRRFTL_WH_Head);
			wcmt_hot_len --;
			DelLpn = DelNode->lpn_num;
			Mix_4K_opagemap[DelLpn].map_status = MAP_INVALID;
			Mix_4K_opagemap[DelLpn].map_age = 0;
            Mix_4K_opagemap[DelLpn].update = 0;
            Victim_pos = search_table(wcmt_arr, MAP_WCMT_MAX_ENTRIES,DelLpn);
            ASSERT(Victim_pos != -1);
            wcmt_arr[Victim_pos] = -1;
            // map write to SLC | map flash page size is 2K
            send_flash_request((((DelLpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE))*4, 4, 1, 2);
            send_flash_request((((DelLpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE))*4, 4, 0, 2);
		}else{
			DelLpn = wcmt_arr[Victim_pos];
			ASSERT(DelLpn != -1);
			//仅干净页剔除，无需回写操作
			ASSERT(Mix_4K_opagemap[DelLpn].update == 0);
			Mix_4K_opagemap[DelLpn].map_status=MAP_INVALID;
            Mix_4K_opagemap[DelLpn].map_age=0;
            Mix_4K_opagemap[DelLpn].update=0;
            wcmt_arr[Victim_pos]=-1;
            //链表操作，删除被置换节点
            DelNode=SearchLPNInList(DelLpn, IRRFTL_WC_Head);
            ASSERT(DelNode != NULL);
            DeleteNodeInList(DelNode, IRRFTL_WC_Head);
            wcmt_cold_len --;
#ifdef DEBUG
            if(DelNode->lpn_num != DelLpn){
                    printf("delete lru arr Temp->lpn %d not equal curr-lpn %d\n",DelNode->lpn_num,DelLpn);
                    assert(0);
            }
 #endif    
		}
		MAP_WCMT_NUM_ENTRIES--;
	}
	
}


/******************************
 * Name: IRRFTL_Move_RCMT2WCMT
 * Date: 03.12.2018
 * Author: ymb
 * param: blkno(page size based on 4k,lpn)
 * 		  operation(1 read , 0: write)
 * return value: void
 * function : rcmt write hit to move node into wcmt
 * ********************************/
void IRRFTL_Move_RCMT2WCMT(blkno, operation)
{
	int free_pos=-1;
    int r_pos=-1;
	ASSERT( operation == 0); 
    IRRFTL_WCMT_Is_Full(0);
    
    //从RCMT中剔除
    r_pos = search_table(rcmt_arr,MAP_RCMT_MAX_ENTRIES,blkno);
    ASSERT(r_pos != -1);
    rcmt_arr[r_pos] = -1;
    MAP_RCMT_NUM_ENTRIES --;
    //加入WCMT中
    free_pos = find_free_pos(wcmt_arr, MAP_WCMT_MAX_ENTRIES);
    ASSERT(free_pos != -1);
    wcmt_arr[free_pos] = blkno;
    Mix_4K_opagemap[blkno].map_status = MAP_WCMT;
    Mix_4K_opagemap[blkno].map_age = operation_time; 
    //链表操作
    IRR_AddNewLPNInMRU(blkno, IRRFTL_WH_Head, gHotFlag);
    MAP_WCMT_NUM_ENTRIES ++;
    wcmt_hot_len ++;
    // write data to MixSSD
    IRRFTL_Data_Operation(blkno, operation, gHotFlag); //应只有写操作
    
#ifdef DEBUG
    if(MAP_WCMT_NUM_ENTRIES > MAP_WCMT_MAX_ENTRIES){
        printf("wcmt_arr overflow (WRFTL_Move_RCMT2WCMT) lpn=%d\n",blkno);
        assert(0);
    }
    if((wcmt_hot_len+wcmt_cold_len) > MAP_WCMT_MAX_ENTRIES){
        printf("wcmt_len > MAP_REAL_MAX_ENTRIES\n");
        assert(0);
    }
#endif
    
}



/******************************************
* Name : IRRFTL_Move_RCMT2MRU
* Date : 03.12.2018
* author: ymb 
* param: blkno (逻辑页号)
*        operation (操作类型，0:write， 1:read)
* return value:
* Attention : 当请求在读映射缓存命中的读请求处理。
*             将映射信息迁移至读缓存的MRU
***********************************************/
void IRRFTL_Move_RCMT2MRU(int blkno, int operation)
{
    //rcmt_arr不变，map_age改变就相当于是迁移至MRU
    Mix_4K_opagemap[blkno].map_age = operation_time;
	ASSERT( operation != 0);
	// read operation don't check hot flag(0)
	IRRFTL_Data_Operation(blkno, operation, 0);
}


/******************************************
* Name : IRRFTL_Move_RCMT2WCMT
* Date : 03.12.2018
* author: ymb 
 * param : blkno(lpn based on 4k Page size)
 * 		   operation(0 write 1 read)
 * return value: void
* Attention : TPCS是IRR中一个预取映射项的缓存表单，
*             未在缓存区命中的映射项会整翻译页的全部读取到TPCS中，
*             TPCS也恰好是一个翻译页大小。然后后续请求到来，先去RCMT和WCMT查询，
*             未查询到，再从TPCS中查询。TPCS中命中，则迁移到对于缓存区中。若TPCS
*             不命中，则将TPCS剩余映射项全部剔除，再重新加载未命中映射的整翻译页。
***********************************************/
void IRRFTL_Move_TPCS2RCMT(int blkno, int operation)
{
    int free_pos = -1;
    ASSERT(operation == 1);
    IRRFTL_RCMT_Is_Full(0);
    
    //ghost_arr不变，map_age改变就相当于是迁移至MRU
    Mix_4K_opagemap[blkno].map_age = operation_time;
    Mix_4K_opagemap[blkno].map_status = MAP_RCMT;
    MAP_RCMT_NUM_ENTRIES ++;
    
    free_pos=find_free_pos(rcmt_arr, MAP_RCMT_MAX_ENTRIES);
	ASSERT(free_pos != -1);
    rcmt_arr[free_pos] = blkno;
    //read operation dont to check req blkno hot
	IRRFTL_Data_Operation(blkno, operation,0);
#ifdef DEBUG
    ASSERT(MAP_RCMT_NUM_ENTRIES <= MAP_RCMT_MAX_ENTRIES);
    ASSERT(wcmt_hot_len <= MAP_WCMT_MAX_ENTRIES);
    ASSERT(MAP_WCMT_NUM_ENTRIES <= MAP_WCMT_MAX_ENTRIES);
#endif
}


/**************************
 * Name :IRRFTL_Move_TPCS2WCMT
 * Date :03.12.2018
 * author:ymb
 * param : blkno(lpn based on 4k Page size)
 * 		   operation(0 write 1 read)
 * return value: void
 * Attention: the operation like add entry into wcmt
 * 			  but don't need read map page again
 * ****************************************/
void IRRFTL_Move_TPCS2WCMT(int blkno, int operation)
{
    int free_pos=-1;
    int r_pos=-1;
    //应只有写操作
    ASSERT(operation == 0);
    IRRFTL_WCMT_Is_Full(0);
    //加入WCMT中
    free_pos = find_free_pos(wcmt_arr, MAP_WCMT_MAX_ENTRIES);
    ASSERT(free_pos != -1);
	wcmt_arr[free_pos] = blkno;
    Mix_4K_opagemap[blkno].map_status = MAP_WCMT;
    Mix_4K_opagemap[blkno].map_age = operation_time;
	Mix_4K_opagemap[blkno].update = 1;
    //链表操作
    IRR_AddNewLPNInMRU(blkno, IRRFTL_WH_Head, gHotFlag);
    MAP_WCMT_NUM_ENTRIES ++ ;
    wcmt_hot_len ++;
    //data operation,hot depend on gHotFlag
    IRRFTL_Data_Operation(blkno, operation, gHotFlag);
    
 #ifdef DEBUG
    ASSERT(wcmt_hot_len <= MAP_WCMT_MAX_ENTRIES);
    ASSERT(MAP_WCMT_NUM_ENTRIES <= MAP_WCMT_MAX_ENTRIES);
#endif
    
}




/*****************************
 * Name: IRRFTL_Hit_WCMT
 * Date: 03.12.2018
 * param: blkno(logical address number,lpn)
 * 		  operation(1:read, 0:write)
 * return: void
 * function: 
 * *******************************/
void IRRFTL_Hit_WCMT(int blkno, int operation)
{
	int Hit_Whot_flag = 1;  // Hit_Whot_flag 判断是否在WH链表命中，命中：1，为命中：0
    int Date_hot = 0;       // Date_hot 标志位，判断该数据是否是热
    int temp_blkno;
    Node * ReqNode;
    Mix_4K_opagemap[blkno].map_status = MAP_WCMT;
    Mix_4K_opagemap[blkno].map_age = operation_time;

    //find req lpn Node in WCMT-list and check hot flag
    ReqNode = IsHotLPN(blkno, IRRFTL_WH_Head, &Date_hot);    
    if(ReqNode == NULL){
        Hit_Whot_flag = 0;
        ReqNode = IsHotLPN(blkno, IRRFTL_WC_Head, &Date_hot);
		ASSERT(ReqNode != NULL);
    }
       
    // 如果在热区命中，将数据迁移至热区MRU
    if(Hit_Whot_flag == 1){
        MoveNodeToMRU(ReqNode, IRRFTL_WH_Head,1);   
        // 在热区命中，如果是冷数据，则将其置为热，热链表最后一个热数据及其与最后一个热数据相邻的数个冷数据转移到冷链表
        if(Date_hot == 0){
            do{
                if(ListLength(IRRFTL_WH_Head) == 1)   // 如果Date_hot只有一个点，则跳过
                    break;
                temp_blkno = IRR_DeleteLRUInList(IRRFTL_WH_Head);
                wcmt_hot_len --;
                IRR_AddNewLPNInMRU(temp_blkno, IRRFTL_WC_Head, 0);  
                wcmt_cold_len ++;

            }while(IsLRUHot(IRRFTL_WH_Head) == 0);
        }
        // 如果以是热数据，则不需要删增数据
    }
    else{
        IRR_AddNewLPNInMRU(blkno, IRRFTL_WH_Head, gHotFlag); // 在冷区命中，将数据迁移至热区MRU，删除冷区数据
        wcmt_hot_len ++ ;
        DeleteNodeInList(ReqNode, IRRFTL_WC_Head);
        wcmt_cold_len --;
    }
    //load data or write data to MixSSD
	IRRFTL_Data_Operation(blkno,operation,Hit_Whot_flag);
 #ifdef DEBUG
    if(wcmt_hot_len > MAP_WCMT_MAX_ENTRIES || wcmt_cold_len > MAP_WCMT_MAX_ENTRIES){
        printf("wcmt_hot_len or wcmt_cold_len > MAP_REAL_MAX_ENTRIES \n");
        assert(0);
    }
 #endif   

}

/*****************************
 * Name: IRRFTL_Hit_RCMT
 * Date: 03.12.2018
 * param: blkno(logical address number,lpn)
 * 		  operation(1:read, 0:write)
 * return: void
 * function: req lpn in RCMT ,need to check operation type
 * 	write RCMT hit need to move node into wcmt
 * *******************************/
void IRRFTL_Hit_RCMT(int blkno, int operation)
{
	//write
	if(operation == 0){
		IRRFTL_Move_RCMT2WCMT(blkno, operation);
#ifdef DEBUG
		if((ListLength(IRRFTL_WC_Head)+ListLength(IRRFTL_WH_Head))!= MAP_WCMT_NUM_ENTRIES ){
			printf(" after IRRFTL_Move_RCMT2WCMT error\nWHLength is %d, WCLength is %d, real_arr size is %d\n",
																					ListLength(IRRFTL_WH_Head),
																					ListLength(IRRFTL_WC_Head),
																					MAP_WCMT_NUM_ENTRIES       );
			assert(0);
		}
#endif
	}else{
		//read
		IRRFTL_Move_RCMT2MRU(blkno, operation);
	}
}


/************************************
 * Name: IRRFTL_Hit_TPCS
 * Date: 03.12.2018
 * Author: zhoujie
 * param : blkno(lpn based on 4k Page size)
 * 		   operation(0 write 1 read)
 * return value: void
 * function: to load map buffer entry into cmt 
 * ********************************************/
void IRRFTL_Hit_TPCS(int blkno, int operation)
{
	if(operation == 0)
		IRRFTL_Move_TPCS2WCMT(blkno, operation);
    else
        IRRFTL_Move_TPCS2RCMT(blkno, operation);
}

/************************************
 * Name: IRRFTL_Hit_TPCS
 * Date: 03.12.2018
 * Author: zhoujie
 * param : blkno(lpn based on 4k Page size)
 * 		   operation(0 write 1 read)
 * return value: void
 * function: to load map buffer entry into cmt 
 * ********************************************/
void IRRFTL_NOT_Hit_CMT(int blkno,int operation)
{
	// 剔除TPCS中的原数据，并读取新翻译页
	TPCS = (blkno - Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE;
	//read map page from SLC
    send_flash_request( TPCS*4, 4, 1, 2);
    if(operation==0)
		IRRFTL_Move_TPCS2WCMT(blkno, operation);
    else
		IRRFTL_Move_TPCS2RCMT(blkno, operation);
}






/******************************************
* Name : IRRFTL_Scheme
* Date : 2018-9-22 
* author: ymb 
* param: secno (request secno)
*        scount(request size based secno size)
*        operation (操作类型，0:write， 1:read)
* return value: delay (operation flash delay)
* Attention : 
***********************************************/
double IRRFTL_Scheme(unsigned int secno,int scount, int operation)
{
	int bcount;
	int blkno; // pageno for page based FTL
	double delay;
	int cnt;
    
    if(init_flag == 0){
		IRRFTL_Init();
	}
	blkno = (secno / UPN_SECT_NUM_PER_PAGE) + Mix_4K_page_num_for_2nd_map_table ;
	bcount = (secno + scount -1)/UPN_SECT_NUM_PER_PAGE - (secno)/UPN_SECT_NUM_PER_PAGE + 1;
	cnt = bcount;
	while(cnt > 0){
		cnt --;
#ifdef DEBUG
		rqst ++;
		printf("rqst %d \t secno:%d\t scoutn%d\n",rqst,secno,scount);
#endif
	/*******************正式进入仿真运行******************/
	// 由于仿真初始，冷热队列都为0，需要处理。首先满足热队列，之后再判断冷队列。热队列小时，将迁移至热队列的都为热。冷队列小时，选择热队列最后一个，移到冷队列
		if(wcmt_hot_len < IRRFTL_Hot_Min){
			gHotFlag = 1;
		}else{
			gHotFlag = 0;
		}
		
		if(wcmt_cold_len < IRRFTL_Cold_Min && gHotFlag == 0){
			do{
				if(ListLength(IRRFTL_WH_Head) == 1)   // 如果hot只有一个点，则跳过
					break;
				wcmt_hot_len--;
				IRR_AddNewLPNInMRU(IRR_DeleteLRUInList(IRRFTL_WH_Head), IRRFTL_WC_Head, 0);  
				wcmt_cold_len++;     
			}while(IsLRUHot(IRRFTL_WH_Head) == 0);
		}
		operation_time++; //师兄是放到每个操作里面去，但是直接外面统计，应当更方便

		if(Mix_4K_opagemap[blkno].map_status == MAP_WCMT){
		//req_entry hit in WCMT
			IRRFTL_Hit_WCMT(blkno,operation);
			//~ ..... static value change
		}else if(Mix_4K_opagemap[blkno].map_status == MAP_RCMT){
		// req_entry hit in RCMT
			IRRFTL_Hit_RCMT(blkno,operation);
			//~ ..... static value change
		}else if( (blkno - Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE == TPCS ){
		// req_entry hit in TPCS
			IRRFTL_Hit_TPCS(blkno,operation);
			//~ ..... static value change
		}else{
		// 剔除TPCS中的原数据，并读取新翻译页
			IRRFTL_NOT_Hit_CMT(blkno,operation);
			//~ ..... static value change
		}
		blkno ++;
		
	}//end-while
	
    delay = calculate_delay_SLC_flash();
	delay += calculate_delay_MLC_flash();
    return delay;
}
