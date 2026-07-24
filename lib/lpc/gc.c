#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "src/std.h"
#include "gc.h"
#include "lpc/array.h"
#include "lpc/mapping.h"
#include "lpc/object.h"
#include "lpc/program.h"
#include <stdlib.h>
#include <string.h>

static gc_gray_node_t *gray_list_head=NULL;
static int gray_list_size=0;
#define GC_NODE_POOL_SIZE 1024
static gc_gray_node_t node_pool[GC_NODE_POOL_SIZE];
static int node_pool_next=0;
#ifdef GC_STATS
int gc_total_marked=0;
int gc_total_cycles=0;
#endif
void gc_gray_list_init(void){gray_list_head=NULL;gray_list_size=0;node_pool_next=0;}
static gc_gray_node_t*gc_alloc_node(void){
if(node_pool_next<GC_NODE_POOL_SIZE)return&node_pool[node_pool_next++];
return(gc_gray_node_t*)malloc(sizeof(gc_gray_node_t));}
void gc_gray_push(void*obj,uint8_t t){
uint8_t*cp=NULL;if(!obj)return;
switch(t){case 1:cp=&((struct array_s*)obj)->gc_color;break;
case 2:cp=&((struct mapping_s*)obj)->gc_color;break;
case 3:cp=&((struct object_s*)obj)->gc_color;break;default:return;}
if(*cp!=0)return;*cp=1;
gc_gray_node_t*n=gc_alloc_node();
n->obj=obj;n->obj_type=t;n->next=gray_list_head;
gray_list_head=n;gray_list_size++;}
gc_gray_node_t*gc_gray_pop(void){
if(!gray_list_head)return NULL;
gc_gray_node_t*n=gray_list_head;
gray_list_head=n->next;gray_list_size--;return n;}
int gc_gray_list_empty(void){return gray_list_head==NULL;}
static void mark_sv(svalue_t*sv){if(!sv)return;
switch(sv->type){
case T_ARRAY:if(sv->u.arr)gc_gray_push(sv->u.arr,1);break;
case T_MAPPING:if(sv->u.map)gc_gray_push(sv->u.map,2);break;
case T_OBJECT:
if(sv->u.ob&&!(sv->u.ob->flags&O_DESTRUCTED))gc_gray_push(sv->u.ob,3);
break;default:break;}}
void gc_mark_children(void*obj,uint8_t t){
switch(t){case 1:{struct array_s*a=(struct array_s*)obj;
for(unsigned short i=0;i<a->size;i++)mark_sv(&a->item[i]);break;}
case 2:{struct mapping_s*m=(struct mapping_s*)obj;
unsigned int sz=(unsigned int)m->table_size+1;
for(unsigned int i=0;i<sz;i++){mapping_node_t*e=m->table[i];
while(e){mark_sv(&e->values[0]);mark_sv(&e->values[1]);e=e->next;}}break;}
case 3:{struct object_s*ob=(struct object_s*)obj;
if(!ob->prog)break;int nv=ob->prog->num_variables_total;
for(int i=0;i<nv;i++)mark_sv(&ob->variables[i]);break;}}}
void gc_incremental_mark(int steps){int taken=0;
while(taken<steps&&!gc_gray_list_empty()){
gc_gray_node_t*n=gc_gray_pop();if(!n)break;
gc_mark_children(n->obj,n->obj_type);
uint8_t*cp=NULL;switch(n->obj_type){
case 1:cp=&((struct array_s*)n->obj)->gc_color;break;
case 2:cp=&((struct mapping_s*)n->obj)->gc_color;break;
case 3:cp=&((struct object_s*)n->obj)->gc_color;break;}
if(cp) *cp=2;
taken++;
#ifdef GC_STATS
gc_total_marked++;
#endif
}}
void gc_mark_roots(void){
#ifdef GC_STATS
gc_total_cycles++;
#endif
}
