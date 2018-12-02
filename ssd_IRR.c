/*************************************************************
* Modle Name: IRRFTL             
* origin author:ymb        
* modified :zhoujie
* Date :02.12.2018
* email: 1395529361@qq.com
* *********************************************************/
#include "ssd_IRR.h"

int IRRFTL_Hot_Min = 0;
int IRRFTL_Cold_Min = 0;
Node *IRRFTL_WH_Head = NULL;
Node *IRRFTL_WC_Head = NULL;
int *cluster_arr = NULL;
int ccw_len;
int wh_len;
int wc_len;
int TPCS = 0;
int hotflag = 0;
void IRRFTL_init_arr();
void IRRFTL_Hit_WCMT(int blkno, int operation);
void IRRFTL_Move_RCMT2MRU(int blkno, int operation);
void IRRFTL_Move_RCMT2WCMT(int blkno, int operation);
void IRRFTL_Move_TPCS2RCMT(int blkno, int operation);
void IRRFTL_Move_TPCS2WCMT(int blkno, int operation);
void IRRFTL_WCMT_Is_Full(int isrun);
void IRRFTL_RCMT_Is_Full(int isrun);
int IRRFTL_Find_Victim_In_WCMT();

/**********************
 * Name:IRRFTL_init_arr
 * Date: 
 * 
 ***********************/
void IRRFTL_init_arr()
{
	
}
