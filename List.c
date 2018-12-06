//
// Created by zj on 18-7-8.
//

#include "List.h"
#include <assert.h>


Node *CreateList()
{
    Node *head;
    head=(Node *)malloc(sizeof(Node));
    if(head==NULL){
        printf("(CreateList)malloc for head is failed\n");
        assert(0);
    }
    //init
    head->lpn_num=-1;
    head->next=head;
    head->pre=head;
    return head;
    //irr 冷热数据识别位
    head->irr_flag=0;
}

void FreeList(Node * Head)
{
    Node *t1,*t2;
    t1=Head->next;

    while(t1!=Head){
        t2=t1;
        t1=t1->next;
        free(t2);
    }

    free(Head);
}

Node * SearchLPNInList(int lpn,Node *Head)
{
    Node * res = NULL ;
    Node * temp = Head->next;
    while(temp != Head){
        if(temp->lpn_num == lpn){
			res = temp ;
            break;
        }
        temp=temp->next;
    }
//   没有找到则返回空的Node
    return res;
}


int IsEmptyList(Node *Head)
{
    if(Head->next==Head){
        return 1;
    }else{
        return 0;
    }
}

int ListLength(Node *Head)
{
    int L=0;
    Node *temp=Head->next;
    if(IsEmptyList(Head) == 1){
		return L;
	}
	
	while(temp != Head){
        L++;
        temp=temp->next;
    }
    return L;
}

Node *AddNewLPNInMRU(int lpn,Node *Head)
{
    Node *new;
    new=(Node *)malloc(sizeof(Node));
    if(new==NULL){
        printf("AddNewLPNInMRU  malloc for new node is failed\n");
        assert(0);
    }

//    insert
    new->lpn_num=lpn;
    new->next=Head->next;
    new->pre=Head;

    Head->next->pre=new;
    Head->next=new;

    return new;
}

//从链表删除尾部的节点，但是返回的是被删除的节点的指针
Node *DeleteLRUInList(Node *Head)
{
    Node *temp;
    if(IsEmptyList(Head)){
        printf("List is Empty,can not delete\n");
        assert(0);
    }
    temp=Head->pre;
//    收尾衔接
    temp->pre->next=Head;
    Head->pre=temp->pre;

//    将该节点剥离出list
    temp->pre=NULL;
    temp->next=NULL;

    return temp;
}


Node *InsertNodeInListMRU(Node *Insert,Node *Head)
{
    Node *temp;
//    remove
    Insert->pre->next=Insert->next;
    Insert->next->pre=Insert->pre;

//    Insert->next=NULL;
//    Insert->pre=NULL;
//    Insert
    Insert->pre=Head;
    Insert->next=Head->next;

    Head->next->pre=Insert;
    Head->next=Insert;

    return Head;
}

Node *DeleteNodeInList(Node *DNode,Node *Head)
{
//    debug code
    Node *temp;
    int flag=0;
    temp=Head->pre;
    while(temp != Head){
        if(temp == DNode){
            flag=1;
            break;
        }
        temp=temp->pre;
    }
    if(flag==0){
        printf("can not find delete node in List\n");
        assert(0);
    }
//     debug end

    DNode->pre->next=DNode->next;
    DNode->next->pre=DNode->pre;

    DNode->pre=NULL;
    DNode->next=NULL;
    return DNode;
}

//debug for print value
void PrintList(Node *Head)
{
    int Count=0;
    Node *temp;
    temp=Head->next;
    while(temp!=Head){
        printf("The %d Node->LPN :%d\n",Count,temp->lpn_num);
        temp=temp->next;
        Count++;
    }
}


/**************************************
 * author: ymb  WRFTL,在查找是否存在链表的同时，返回所在链表位置
 **************************************/
Node *IsHotLPNInList(int lpn, Node *Head, int *Len)
{
    int len=0;
    Node *temp;
    temp=Head->next;
    while(temp !=Head){
        if(temp->lpn_num==lpn){
            *Len=len+1;
            return temp;
        }
        temp=temp->next;
        len++;
    }
//   没有找到则返回空的Node
    return NULL;
}

/**************************************
 * Name : IsHotLPN
 * author: ymb  IRRFTL，判断数据是否为热
 * Date: 2018-12-2
 * param: lpn
 * 		  * Head(double point list Head point)
 * 		  * hot(flag to distinc lpn is hot(1) or cold(0))
 * return : lpn in List location point (NULL -> not find )
 * function : to check request lpn is hot? 
 **************************************/
Node *IsHotLPN(int lpn, Node *Head, int *hot)
{
    Node *temp;
    temp=Head->next;
    while(temp !=Head){
        if(temp->lpn_num==lpn){
            *hot = temp->irr_flag;
            return temp;
        }
        temp=temp->next;
    }
//   没有找到则返回空的Node
    return NULL;
}


/***********************
 *  Name: IsLRUHot
 *  Date: 2018 - 12-2
 *  Author: ymb
 *  param : *Head( when function called, this Head to ponit to HotList)
 *  return : (1 hot , 0 cold)
 *  function : this function to check HotList (List-tail) LRU Node is hot lpn Node?
 *************************/
int IsLRUHot(Node *Head)
{
    Node *temp;
    temp = Head -> pre;
    if(temp->irr_flag == 0)
        return 0;
    else
        return 1;
}

/*********************************
 * Name: MoveNodeToMRU
 * Author:ymb
 * Date: 02.12.2018
 * Param: Insert( lpn Node)
 * 		  Head(wait to Insert List Head ponit)
 * 		  isHot(to check Insert lpn Node is hot lpn(0 cold ,1 hot))
 * return value: After Insert List
 *********************************/
// 功能与insert类似，但是需要将irr_flag置位
Node *MoveNodeToMRU(Node *Insert,Node *Head, int ishot)
{
    Node *temp;
    Insert->irr_flag = ishot;
    if(Insert->next != NULL && Insert->pre != NULL ){
		//    remove
		Insert->pre->next=Insert->next;
		Insert->next->pre=Insert->pre;
	}
    
    Insert->pre=Head;
    Insert->next=Head->next;

    Head->next->pre=Insert;
    Head->next=Insert;

    return Head;
}

/****************************
 *  Name: IRR_AddNewLPNInMRU
 *  Date: 02.12.2018
 *  Author: ymb
 *  param : lpn
 * 			Head(double list head point)
 * 			ishot(to check insert lpn is hot lpn?) 
 *  return value : new(the Node point to location in Head list)
 *  function : to new Node memory to record lpn info and insert this Node into 
 * 			List Head->next(MRU)
 ****************************/
Node *IRR_AddNewLPNInMRU(int lpn,Node *Head, int ishot)
{
    Node *new;
    new=(Node *)malloc(sizeof(Node));
    if(new==NULL){
        printf("AddNewLPNInMRU  malloc for new node is failed\n");
        assert(0);
    }

//    insert
    new->lpn_num=lpn;
    new->irr_flag=ishot;
    new->next=Head->next;
    new->pre=Head;

    Head->next->pre=new;
    Head->next=new;

    return new;
}

/***********************************
 * Name: IRR_DeleteLRUInList 
 * Date :02.12.2018
 * Author :ymb
 * param : Head (double list Node point to Head)
 * return value: lpn-num(the Delete lpn node(LRU) storage lpn)
 * function : to Delete LRU Node from List
 **************************************/
int IRR_DeleteLRUInList(Node *Head)
{
    Node *temp;
    if(IsEmptyList(Head)){
        printf("List is Empty,can not delete\n");
        assert(0);
    }
    temp=Head->pre;
//    收尾衔接
    temp->pre->next=Head;
    Head->pre=temp->pre;

//    将该节点剥离出list
    temp->pre=NULL;
    temp->next=NULL;

    return temp->lpn_num;
}
/****************debug for List function********************************/
/*
 * Node *Head;

int main()
{
    int i,lpn;
    Node *Temp;
    Head=CreateList();
    for(i=0;i<10;i++){
        AddNewLPNInMRU(i,Head);
    }
//   test print
    printf("AddNewLPNInMRU\n");
    PrintList(Head);

    lpn=6;
    Temp=SearchLPNInList(lpn,Head);
    if(Temp!=NULL){
        printf("search %d in List,the Node ->lpn %d\n",lpn,Temp->lpn_num);
        printf("***************Insert before*************\n");
        PrintList(Head);
        InsertNodeInListMRU(Temp,Head);
        printf("***************Insert after*************\n");
        PrintList(Head);
    }


    lpn=11;
    Temp=SearchLPNInList(lpn,Head);
    if(Temp==NULL){
        printf("can not search %d in List\n",lpn);
        printf("***************Add before*************\n");
        PrintList(Head);
        AddNewLPNInMRU(lpn,Head);
        printf("***************Add after*************\n");
        PrintList(Head);
    }

    printf("***************Delete before*************\n");
    PrintList(Head);
    Temp=DeleteLRUInList(Head);
    printf("delete Node ->LPN %d \n",Temp->lpn_num);
    printf("***************Add after*************\n");
    PrintList(Head);

    FreeList(Head);
    return 0;
}

 */
