/*************************************************************
* Modle Name: IRRFTL             
* origin author:ymb        
* modified :zhoujie
* Date :02.12.2018
* email: 1395529361@qq.com
* *********************************************************/
#ifndef _IRR_H_
#define _IRR_H_
#include <stdlib.h>
#include <stdio.h>
#include "List.h"
#include "flash.h"
#include "MixFTL.h"

double IRRFTL_Scheme(int *pageno, int *req_size, int operation);

#endif
