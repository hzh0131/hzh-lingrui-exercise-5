#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "memlib.h"
#include "mm.h"

/* double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT - 1)) & ~0x7)

#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 一个头部或脚部的大小 */
#define WSIZE sizeof(size_t)

/* 两个字的大小 */
#define DSIZE (2*WSIZE)

/* 将大小和分配标记打包 */
#define PACK(size,alloc) ((size)|(alloc))

/* 读取和写入一个字 */
#define GET(p) (*(size_t*)(p))
#define PUT(p,val) (*(size_t*)(p)=(val))

/* 读取块大小和分配状态,在当前设计中，块的大小存储在头部的 size_t 值中，最低 3 位留给标记使用，其余高位用于表示块大小。 */
#define GET_SIZE(p) (GET(p)& ~0x7)
#define GET_ALLOC(p) (GET(p)& 0X1)  // 0 表示空闲，1表示已经被占用

/* 分离空闲链表：暂时设置 16 个桶 */
#define LIST_NUM 16

/* bp 指向块的载荷起始地址 */
#define PREV_FREEP(bp) (*(void **)(bp))  //查找或修改有关地址存的指针
#define NEXT_FREEP(bp) (*(void **)((char *)(bp) + WSIZE)) //查找或修改有关地址存的指针

static void *free_lists[LIST_NUM];


int mm_init(void) //初始化
{ 
  for(int i=0;i<LIST_NUM;i++)
  {
    free_lists[i]=NULL;
  }
  
  char *start=(char*)mem_sbrk(4*WSIZE);
  if(start==(void* )(-1)) //start == (void *)-1：判断扩展模拟堆是否失败。
  {
    return -1;
  }
  
  PUT (start,0);
  PUT(start+WSIZE,PACK(DSIZE,1)); //保存序言块的总大小以及占用情况
  PUT(start+WSIZE*2,PACK(DSIZE,1)); //保存序言块的总大小以及占用情况
  PUT(start+WSIZE*3,PACK(0,1));
  
  return 0; 
}




int get_list_index(size_t size) //拿到已知内存对应所在的那个链表的下标
{
  if(size<=0)
  {
    return -1;
  }

  if(size>0&&size<=32)
  {
    return 0;
  }
 
  int start=32;

  for(int i=1;i<13;i++)
  {
    int gap=2^(((i+1)/2)+3);
    int end=start+gap;
    if(size>start&&size<=end)
    {
      return i;
      break;
    }
    start=end;
  }

  if(size>2048&&size<=4096)
  {
    return 13;
  }
 
  if(size>4096&&size<=8192)
  {
    return 14;
  }

  else
  {
  return 15;
  }

}



static void insert_free_block(void *bp)  //这里的 bp 是一个 void * 指针，指向空闲块的载荷起始地址。插入空闲块
{
  size_t size=GET_SIZE((char*)bp-WSIZE);
  int index=get_list_index(size);

  void* old_head=free_lists[index];

  PREV_FREEP(bp)=NULL;
  NEXT_FREEP(bp)=old_head;
  
  if(old_head!=NULL)
  PREV_FREEP(old_head)=bp;

  free_lists[index]=bp;
}




static void remove_free_block(void *bp) //去掉被使用的空闲块
{
  size_t size=GET_SIZE((char*)bp-WSIZE);
  int index=get_list_index(size);
  
  //该节点为头节点
  if(PREV_FREEP(bp)==NULL)
  {
    void *old_next=NEXT_FREEP(bp);
    if(old_next!=NULL)
    {
      PREV_FREEP(old_next)=NULL;
      free_lists[index]=old_next;
      NEXT_FREEP(bp)=NULL;
    }
    else
    {
      free_lists[index] = NULL;
    }
  }

  //该节点为中间节点
  if(PREV_FREEP(bp)!=NULL&&NEXT_FREEP(bp)!=NULL)
  {
    void *old_next=NEXT_FREEP(bp);
    void *old_prev=PREV_FREEP(bp);
    
    PREV_FREEP(old_next)=old_prev;
    NEXT_FREEP(old_prev)=old_next;
    NEXT_FREEP(bp)=NULL;
    PREV_FREEP(bp)=NULL;
    
  }

  //该节点为尾节点
  if(PREV_FREEP(bp)!=NULL&&NEXT_FREEP(bp)==NULL)
  {
    void *old_prev=PREV_FREEP(bp);
    
    NEXT_FREEP(old_prev)=NULL;
    NEXT_FREEP(bp)=NULL;
    PREV_FREEP(bp)=NULL;
    
  }

}






static void *coalesce(void *bp)  //合并内存
{
  size_t current_size=GET_SIZE((char *)bp - WSIZE);//当前块大小
  size_t prev_size=GET_SIZE((char*)bp-2*WSIZE);//前驱块大小
  size_t next_size=GET_SIZE((char*)bp-WSIZE+current_size);//后随快大小
   
  size_t prev_alloc=GET_ALLOC((char*)bp-2*WSIZE);//前驱块占用信息
  size_t next_alloc=GET_ALLOC((char*)bp-WSIZE+current_size);//后随块占用信息
  if(prev_alloc!=0)
  {
    if(next_alloc==0)
    {
      size_t new_size=next_size+current_size;
      size_t val=PACK(new_size,0);
      
      //从移除原来链表中移出已被合并的链节
      remove_free_block(bp+current_size);//下一个内存的有效载荷起始地址
      
      //修改新内存的头部和脚部
      PUT((char*)bp-WSIZE,val);
      PUT((char*)bp-2*WSIZE+new_size,val);
      
      //把合并后新的链节放入对应的链表中
      if(new_size>=32)
      {
        insert_free_block((char*)bp);
      }

      return (char*)bp;
    }
  
    if(next_alloc!=0)
    {
      insert_free_block((char*)bp);
      return (char*)bp;
    }
  }

  else
  {
    if(next_alloc==0)
    {
      size_t new_size=next_size+current_size+prev_size;
      size_t val=PACK(new_size,0);
      
      //从移除原来链表中移出已被合并的链节
      remove_free_block(bp+current_size);//下一个内存的有效载荷起始地址
      remove_free_block(bp-prev_size);//上一个内存的有效载荷起始地址
      
      //修改新内存的头部和脚部
      PUT((char*)bp-WSIZE-prev_size,val);
      PUT((char*)bp-2*WSIZE+current_size+next_size,val);
      
      if(new_size>=32)
      {
        insert_free_block((char*)bp-prev_size);
      }
      
      return ((char*)bp-prev_size);  
    }

    if(next_alloc!=0)
    {
      size_t new_size=current_size+prev_size;
      size_t val=PACK(new_size,0);
      
      //从移除原来链表中移出已被合并的链节
      remove_free_block(bp-prev_size);//上一个内存的有效载荷起始地址
      
      //修改新内存的头部和脚部
      PUT((char*)bp-WSIZE-prev_size,val);
      PUT((char*)bp-2*WSIZE+current_size,val);      
      
      //把合并后新的链节放入对应的链表中
      if(new_size>=32)
      {
        insert_free_block((char*)bp-prev_size);
      }
      
      return ((char*)bp-prev_size);
    }
  
  }

  return bp;
}






static void *extend_heap(size_t words) /*words 表示希望扩展的内存量，以 WSIZE 为一个单位，而不是直接表示字节数。
                                        extend_heap() 采用的是 CSAPP 中的设计约定：调用者传入扩展空间的字数（words），
                                        函数内部再将字数转换成字节数（bytes），当然直接传入size也不是不可以，
                                        这里为了与书上规定一致，就选择了传入words的写法*/
{
  size_t size=words*WSIZE;
  
  // 保证最小块大小，并保持 8 字节对齐
  if(size<4*WSIZE)
  {
    size=4*WSIZE;
  }
  size=ALIGN(size);

  char *new_start=(char*)mem_sbrk(size);

  if(new_start==(void*)-1)
  {
    return NULL;
  }

  size_t val=PACK(size,0);
  char *bp=new_start;
  
  PUT(bp-WSIZE,val);//新空闲块的块头
  PUT(bp+size-DSIZE,val);//新空闲块的块脚
  PUT(bp+size-WSIZE,PACK(0,1));//新的结尾块
  

  bp=coalesce(bp);

  return bp;


}




static void *find_fit(size_t asize) //asize：经过对齐和计算头部、脚部开销后的所需块大小，单位是字节。
{
  int index=get_list_index(asize);
  
  for(;index<LIST_NUM;index++)
  {
  void *link=free_lists[index];
  while(link!=NULL)
  {
      size_t size1=GET_SIZE((char*)link-WSIZE);
    
      if(size1>=asize)
      {
        return link;
      }

      else
      {
        link=NEXT_FREEP(link);
      }


  }
  
  }
    return NULL;

}




static void place(void *bp, size_t asize) /*bp：find_fit() 找到的空闲块的载荷起点。asize：本次请求需要的块大小，
                                          已经包含头部、脚部开销并完成对齐。*/
{
  //将申请到的内存所在的链节移出链表
  remove_free_block(bp);
  
  //拿到申请到的内存的大小
  size_t size1=GET_SIZE((char*)bp-WSIZE);
  size_t left_size=size1-asize;

  
  if(left_size>=32)
  {
    //把对应信息填入需要用到的内存中
    PUT((char*)bp-WSIZE,PACK(asize,1));
    PUT((char*)bp-DSIZE+asize,PACK(asize,1));
    
    //把对应信息填入从链表中申请到的内存减去要用到的内存剩下的内存中
    PUT((char*)bp-WSIZE+asize,PACK(left_size,0));
    PUT((char*)bp-DSIZE+size1,PACK(left_size,0));
    
    //将剩下的内存重新存入 free-list 中
    char* left_bp=(char*)bp+asize;
    insert_free_block(left_bp);


  }

  else
  {
    PUT((char*)bp-WSIZE,PACK(size1,1));
    PUT((char*)bp-DSIZE+size1,PACK(size1,1));
  }

  return ;
}






void *mm_malloc(size_t size) // 串联前面的辅助函数，申请内存 
{

  /*没看懂下面这段代码有什么作用，所以这里我就直接按照自己的思路来构建 mm_malloc 了*/
  /*
  int newsize = ALIGN(size + SIZE_T_SIZE);  //给载荷和大小信息都预留空间
  void *p = mem_sbrk(newsize);
  if (p == (void *)-1)
    return NULL;
  else {
    *(size_t *)p = size;
    return (void *)((char *)p + SIZE_T_SIZE);
  }
  */

  if(size<=0)
  {
    return NULL;
  }
  
  size_t asize=ALIGN(size+DSIZE);
  if(asize<4*WSIZE)
  {
    asize=4*WSIZE;
  }
  
  char *bp=NULL;

  if(find_fit(asize)==NULL)
  {
    bp=extend_heap(asize/WSIZE);
    if(bp==NULL)
    {
      return NULL;
    }
  }

  else
  {
    bp=find_fit(asize);
  }

  place(bp,asize);
  
  return bp;

}




void mm_free(void *ptr) // 释放内存并标记其为空闲块
{
  if (ptr==NULL)
  return ;
  
  //把被释放的内存标记为空闲块
  size_t size=GET_SIZE((char*)ptr-WSIZE);
  PUT((char*)ptr-WSIZE,PACK(size,0));
  PUT((char*)ptr-DSIZE+size,PACK(size,0));

  coalesce(ptr);
  return ;  

}




void *mm_realloc(void *ptr, size_t size) // 重新调整内存大小
{ 
  //第一种情况, ptr 为 NULL
  if(ptr==NULL)
  {
    void*bp=mm_malloc(size);
    
    if(bp==NULL)
    {
      return NULL;
    }

    return bp;
  }  
  
  size_t current_size=GET_SIZE((char*)ptr-WSIZE);
  

  //第二种情况，ptr 不为 NULL 且 size 为0
  if(ptr!=NULL&&size==0)
  {
    mm_free(ptr);
    return NULL;
  }

  //第三种情况，目前的内存块大小足以满足内存需求
  size_t old_payload_size=current_size-DSIZE;
  if(old_payload_size>=size)
  {
    return ptr;
  }
  
  //第四种情况，目前的内存块大小不满足内存需求
  else
  {
    void *new_bp=mm_malloc(size);
    
    if(new_bp==NULL)
    {
        return NULL;
    
    }

    else
    {
      //保留新旧大小中较小长度的内容
      size_t copy_size = old_payload_size < size
                 ? old_payload_size
                 : size;

      memcpy(new_bp, ptr, copy_size);
      mm_free(ptr);
      
      return new_bp;
    
    }

  }
  
  return NULL; 

}
