/*************************************************************
* Modle Name: SFTL(Simple FTL)             
* Author : Zhoujie
* Date : 05.12.2018
* email: 1395529361@qq.com
* *********************************************************/
#include "ssd_IRR.h"
#include "ssd_interface.h"

#define MAP_MAX_ENTRIES 4096
#define SFTL_MAP_HOT  2
#define SFTL_MAP_COLD 3
#define SFTL_MAP_TPB 4

#define SPEED_SLC_FAST 1
#define SPEED_MLC_FAST 2

#ifdef DEBUG
static int debug_count;
static int debug_cycle = 10000;
#endif
static int init_flag = 0;
static int *cluster_arr = NULL;
static int operation_time = 0;
static int Mix_2K_page_num_for_2nd_map_table; //based on 2k map page
static int Mix_4K_page_num_for_2nd_map_table; //based on 4k map page
static int Wear_Flag = -1 ; //1 SLC fast ; 2 MLC fast 
static int Mix_normalization_value = 10; //SLC and MLC ecn all normalized in MLC
static double Wear_Value;
static double Wear_Th = 1.2;

static int Max_Hot_Len;
static double Max_Hot_rate;
double change_rate_step = 0.1;
double Max_Hot_up_rate = 0.8;
double Max_Hot_down_rate = 0.1;

static int Curr_Hot_Len;
static int Curr_Cold_Len;
//take double list to show Hot length
static int cmt_arr[MAP_MAX_ENTRIES];
static Node * SFTL_Hot_Head = NULL;
static Node * SFTL_Cold_Head = NULL;\
static int TPB = -1;

void Wear_Monitor();
static int SFTL_Data_Operation(int blkno,int operation,int hot_flag);
void SFTL_init();
static void Update_SLC_ppn_status(int lpn);
static void Hit_Hot_region(int blkno,int operation);
static void Hot_Region_Is_Full(int isRun);
static void Cold_Region_Is_Full(int isRun);
static Node * Find_ColdList_CleanNode();
static void Max_Map_Cluster_Write_Back();
static void Hit_TPB_Region(int blkno,int operation);
static void Not_Hit_CMT(int blkno,int operation);
static void Hit_Cold_region(int blkno,int operation);
/*****************************
 * Name :Wear_Monitor
 * Date: 05.12.2018 
 * param : void
 * return : void
 * function : to Static SLC and MLC wear speed
 * *******************************/
void Wear_Monitor()
{
	int SLC_ALL_ECN = SLC_stat_erase_num;
	int MLC_ALL_ECN = MLC_stat_erase_num;
	double SLC_AVE_ECN = SLC_ALL_ECN * 1.0 / nand_SLC_blk_num;
	double MLC_AVE_ECN = MLC_ALL_ECN * 1.0 / nand_MLC_blk_num;
	double SLC_Normal_ECN = SLC_AVE_ECN / Mix_normalization_value;
	double MLC_Normal_ECN  = MLC_AVE_ECN;
	if(MLC_Normal_ECN > SLC_Normal_ECN){
		Wear_Value = MLC_Normal_ECN / SLC_Normal_ECN;
		Wear_Flag = SPEED_MLC_FAST;
	}else{
		Wear_Value = SLC_Normal_ECN / MLC_Normal_ECN;
		Wear_Flag = SPEED_SLC_FAST;
	}
#ifdef DEBUG
	printf("SLC ALL ECN %d\t SLC AVE ECN %lf\t SLC Normalized ECN\n",SLC_ALL_ECN ,SLC_AVE_ECN,SLC_Normal_ECN);
	printf("MLC ALL ECN %d\t MLC AVE ECN %lf\t MLC Normalized ECN\n",MLC_ALL_ECN ,MLC_AVE_ECN,MLC_Normal_ECN);
#endif 
	// decide to change Max_Hot_Len
	if(Wear_Value >= Wear_Th){
		if(Wear_Flag == SPEED_MLC_FAST){
			Max_Hot_rate += change_rate_step;
			Max_Hot_rate = (Max_Hot_rate > Max_Hot_up_rate? Max_Hot_up_rate : Max_Hot_rate);
			Max_Hot_Len = MAP_MAX_ENTRIES * Max_Hot_rate;
			
		}else if(Wear_Flag == SPEED_SLC_FAST){
			Max_Hot_rate -= change_rate_step;
			Max_Hot_rate = (Max_Hot_rate < Max_Hot_down_rate? Max_Hot_up_rate : Max_Hot_rate);
			Max_Hot_Len = MAP_MAX_ENTRIES * Max_Hot_rate;
		}else{
			printf("Wear_FLag is %d\n",Wear_Flag);
			assert(0);
		}
	}	
}

/**************************
 * Name: SFTL_init
 * Date :05.12.2018
 * param: void
 * return :void
 * function : to Init globa value
 * ******************************/
void SFTL_init()
{
	Mix_2K_page_num_for_2nd_map_table = (Mix_4K_opagemap_num/MIX_MAP_ENTRIES_PER_PAGE);
	if((Mix_4K_opagemap_num % MAP_ENTRIES_PER_PAGE) != 0){
		Mix_2K_page_num_for_2nd_map_table++;//every map page 2k(all map page num)
	}
	Mix_4K_page_num_for_2nd_map_table = (int)(Mix_2K_page_num_for_2nd_map_table * 1.0 / 2 + 0.5);//based on 4K
	
	cluster_arr=(int *)malloc(sizeof(int) * Mix_2K_page_num_for_2nd_map_table);
	
	if(cluster_arr == NULL){
		printf("malloc for cluster is failed\n");
		assert(0);
	}
	memset(cmt_arr, -1, sizeof(int) * MAP_MAX_ENTRIES);
	memset(cluster_arr,0, sizeof(int) * Mix_2K_page_num_for_2nd_map_table);
	SFTL_Cold_Head = CreateList();
	SFTL_Hot_Head = CreateList();
	operation_time = 0;
	init_flag = 1;
}

/******************************
 * Name: SFTL_Data_Operation
 * Date: 05.12.2018
 * param: blkno--> (lpn ,based on 4k page)
 * 		  operation (0 :write 1:read)
 * 		  hot_flag(1:hot data,0:cold data)
 * return value:0(operation success)
 * function: to  write or read data from MixSSD
 * Attention:
 * ********************************/
static int SFTL_Data_Operation(int blkno,int operation,int hot_flag)
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


/*****************************
 * Name : Update_SLC_ppn_status
 * Date: 05.12.2018
 * Author: zhoujie
 * param : lpn(data lpn based on 4K)
 * return : void
 * ***************************/
static void Update_SLC_ppn_status(int lpn)
{
	int Type ;
	int ppn;//slc ppn based on 2k 
	Node * TempNode;
	int Date_hot = 0 ;
	ASSERT(lpn <= Mix_4K_opagemap_num);
	
	if(Mix_4K_opagemap[lpn].IsSLC != 1){
		return; 
	}
	ASSERT(Mix_4K_opagemap[lpn].IsSLC == 1);
	Type = Mix_4K_opagemap[lpn].map_status;
	ppn = Mix_4K_opagemap[lpn].ppn;
	ASSERT(ppn < SLC_ppn_num);
	
	if(Type == SFTL_MAP_HOT){
		TempNode = SearchLPNInList(lpn , SFTL_Hot_Head);
		ASSERT(TempNode != NULL );
		if(TempNode.irrflag == 1){
			SLC_ppn_status[ppn] = 2; //in CMT-Hot
			SLC_ppn_status[ppn+1] =2;
		}else{
			SLC_ppn_status[ppn] = 1; //in CMT-Hot but status cold
			SLC_ppn_status[ppn+1] = 1;
		}
	}else if(Type == SFTL_MAP_COLD){
		SLC_ppn_status[ppn] = 1; //in CMT-Cold
		SLC_ppn_status[ppn+1] = 1;
	}else{
		SLC_ppn_status[ppn] = -1;
		SLC_ppn_status[ppn+1] = -1;
	}

}

static void Hit_Hot_region(int blkno,int operation)
{
	Node * ReqNode;
	ReqNode = SearchLPNInList(blkno, SFTL_Hot_Head);
	ASSERT(ReqNode != NULL); 
	Mix_4K_opagemap[blkno].map_age = operation_time;
	Mix_4K_opagemap[blkno].map_status = SFTL_MAP_HOT;
	if( operation == DATA_READ ){
		// don't operation hot region list
		SFTL_Data_Operation(blkno, operation, 0);
	}else{
		Mix_4K_opagemap[blkno].update = 1;
		MoveNodeToMRU(ReqNode, SFTL_Hot_Head, 1);
		SFTL_Data_Operation(blkno, operation, 1);
	}
}


static void Hot_Region_Is_Full(int isRun)
{
	Node * DelNode;
	int Dlpn = -1;
	int i;
	ASSERT(Curr_Hot_Len >= Max_Hot_Len);
	DelNode = SFTL_Hot_Head->pre;
	Dlpn = DelNode.lpn_num;
	Mix_4K_opagemap[Dlpn].map_status = SFTL_MAP_COLD;
	
	if(Curr_Cold_Len + Curr_Hot_Len >= MAP_MAX_ENTRIES)
		ColdRegion_Is_Full(0);
	MoveNodeToMRU(DelNode,SFTL_Cold_Head,0);
	Curr_Cold_Len ++;
	Curr_Hot_Len -- ;
	
}

static void Cold_Region_Is_Full(int isRun)
{
	Node * VictimNode = NULL;
	int lpn;
	ASSERT(Curr_Cold_Len + Curr_Hot_Len >= MAP_MAX_ENTRIES);
	if((VictimNode = Find_ColdList_CleanNode()) == NULL){
		//find max cluster map write back
		Max_Map_Cluster_Write_Back();
		VictimNode = Find_ColdList_CleanNode();
		ASSERT(VictimNode != NULL);
	}
	lpn = VictimNode.lpn_num;
	Mix_4K_opagemap[lpn].update = 0;
	Mix_4K_opagemap[lpn].map_age = 0;
	Mix_4K_opagemap[lpn].map_status = MAP_INVALID;
	DeleteNodeInList(VictimNode, SFTL_Cold_Head);
	Curr_Cold_Len -- ;
}



static Node * Find_ColdList_CleanNode()
{
	int lpn;
	Node * temp = NULL;
	Node * index = SFTL_Cold_Head->next;
	while(index->next != SFTL_Cold_Head){
		lpn = index.lpn_num;
		if(Mix_4K_opagemap[lpn].update == 0){
			temp = index;
			break;
		}
		index = index->next;
	}
	
	retun temp;
}

static void Max_Map_Cluster_Write_Back()
{
	int i,lpn,max_map_lpn = -1,max_map_size = -1;
	Node * temp;
	memset(cluster_arr, 0, sizeof(int)*Mix_2K_page_num_for_2nd_map_table);
	temp = SFTL_Hot_Head->next;
	while(temp->next != SFTL_Hot_Head){
		lpn = temp.lpn_num;
		if(Mix_4K_opagemap[lpn].update == 1){
			cluster_arr[(lpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE] += 1;
		}
	}
	temp = SFTL_Cold_Head->next;
	while(temp->next != SFTL_Cold_Head){
		lpn = temp.lpn_num;
		if(Mix_4K_opagemap[lpn].update == 1){
			cluster_arr[(lpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE] += 1;
		}
	}
	for(i =0 ; i< Mix_2K_page_num_for_2nd_map_table;i++){
		if(cluster_arr[i] > max_map_size){
			map_map_lpn = i;
			max_map_size = cluster_arr[i];
		}
	}
	ASSERT( max_map_lpn != -1);
	ASSERT( max_map_size > 0);
	
	//reset map upate
	temp = SFTL_Hot_Head->next;
	while(temp->next != SFTL_Hot_Head){
		lpn = temp.lpn_num;
		if((lpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE == max_map_lpn){
			Mix_4K_opagemap[lpn].update = 0;
		}
	}
	temp = SFTL_Cold_Head->next;
	while(temp->next != SFTL_Cold_Head){
		lpn = temp.lpn_num;
		if((lpn-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE == max_map_lpn){
			Mix_4K_opagemap[lpn].update = 0;
		}
	}
	// map lpn write to SLC
	send_flash_request(max_map_lpn*4,4,1,2);
}


static void Hit_TPB_Region(int blkno,int operation)
{

	if(operation == DATA_READ){
		if(Curr_Cold_Len + Curr_Hot_Len >= MAP_MAX_ENTRIES){
			Cold_Region_Is_Full(0);
		}
		Mix_4K_opagemap[blkno].map_age = operation_time;
		Mix_4K_opagemap[blkno].map_status = SFTL_MAP_COLD;
		Mix_4K_opagemap[blkno].update = 0;		
		IRR_AddNewLPNInMRU(blkno,SFTL_Cold_Head,0);
		
		Curr_Cold_Len ++;
		SFTL_Data_Operation(blkno, operation, 0);
	}else if( operation == DATA_WRITE){
		if(Curr_Hot_Len > Max_Hot_Len){
			Hot_Region_Is_Full(0);
		}
		IRR_AddNewLPNInMRU(blkno,SFTL_Hot_Head,0);
		
		Curr_Hot_Len ++;
		Mix_4K_opagemap[blkno].map_age = operation_time;
		Mix_4K_opagemap[blkno].map_status = SFTL_MAP_HOT;
		Mix_4K_opagemap[blkno].update = 1;
		
		SFTL_Data_Operation(blkno,operation,0);
	}else{
		printf("error in operation type\n");
		assert(0);
	}
}

static void Not_Hit_CMT(int blkno,int operation)
{
	// 剔除TPCS中的原数据，并读取新翻译页
	TPB = (blkno - Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE;
	//read map page from SLC
    send_flash_request( TPB*4, 4, 1, 2);
    Hit_TPB_Region(blkno, operation);
}

static void Hit_Cold_region(int blkno,int operation)
{
	Node * ReqNode;
	ReqNode = SearchLPNInList(blkno. SFTL_Cold_Head);
	ASSERT(ReqNode != NULL);
	Mix_4K_opagemap[blkno].map_age = operation_time;
	if( operation == DATA_READ){
		Mix_4K_opagemap[blkno].map_status = SFTL_MAP_COLD;
		MoveNodeToMRU(ReqNode,SFTL_Cold_Head,0);
		SFTL_Data_Operation(blkno, operation,0);
	}else{
		Mix_4K_opagemap[blkno].map_status = SFTL_MAP_HOT;
		Mix_4K_opagemap[blkno].update = 1;
		if(Curr_Hot_Len > Max_Hot_Len){
			Hot_Region_Is_Full(0);
		}
		
		MoveNodeToMRU(ReqNode,SFTL_Hot_Head,0);
		Curr_Cold_Len --;
		Curr_Hot_Len ++;
		//first load hot when data still cold
		SFTL_Data_Operation(blkno,operation,0);
	}

}

/**************************
 * Name : SFTL_Scheme
 * Date:05.12.2018
 * param : secno ,scount (based no sector size ),operation(1:read,0:write)
 * ***********************/
double SFTL_Scheme(unsigned int secno,int scount, int operation)
{
	int bcount;
	int blkno; // pageno for page based FTL
	double delay;
	int cnt;
    
    if(init_flag == 0){
		SFTL_Init();
	}
	blkno = (secno / UPN_SECT_NUM_PER_PAGE) + Mix_4K_page_num_for_2nd_map_table ;
	bcount = (secno + scount -1)/UPN_SECT_NUM_PER_PAGE - (secno)/UPN_SECT_NUM_PER_PAGE + 1;
	cnt = bcount;
	while(cnt > 0){
		cnt--;
		operation_time ++;
		Update_SLC_ppn_status(blkno);
		if(Mix_4K_opagemap[blkno].map_status == SFTL_MAP_HOT){
			//hit hot region
			Hit_Hot_region(blkno, operation);
		}else(Mix_4K_opagemap[blkno].map_status == SFTL_MAP_COLD){
			//hit cold region
			Hit_Cold_region(blkno, operation);
		}else((blkno-Mix_4K_page_num_for_2nd_map_table)/MIX_MAP_ENTRIES_PER_PAGE == TPB){
			//hit TPB move to hot or cold region
			Hit_TPB_Region(blkno,operation);
		}else{
			// first load TPB
			Not_Hit_CMT(blkno,operation);
		}
		Update_SLC_ppn_status(blkno);
		blkno ++;
	}
	delay = calculate_delay_SLC_flash();
	delay += calculate_delay_MLC_flash();
    return delay;
}

