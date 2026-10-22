#!/usr/bin/env python3
"""Compile actual allocator/release functions with mock registration; no hardware."""
import argparse, pathlib, subprocess, tempfile, os

def function(text, name):
    start=text.index(name+'('); start=text.rfind('\n',0,start)+1
    brace=text.index('{',start); level=1; end=brace+1
    while level:
        level += (text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

def main():
    p=argparse.ArgumentParser(); p.add_argument('--source-root'); p.add_argument('--sanitize',action='store_true'); a=p.parse_args()
    root=pathlib.Path(a.source_root) if a.source_root else pathlib.Path(__file__).resolve().parent/'overlay'
    text=(root/'src/uct/urma/rm_urma/rm_urma_iface.c').read_text()
    prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#define RM_BUFFER_ALIGNMENT 4096u
#define RM_PACKET_MAX 4096u
#define ucs_assert assert
static uint64_t clock_value;
static uint64_t ucs_get_time(void) {return ++clock_value;}
typedef struct rm_op {struct rm_op *next; uint64_t id,cost_alloc,cost_reg; void *buffer,*seg,*remote_seg; size_t length; int kind,pooled; unsigned pool_slot;} rm_op_t;
typedef struct {struct {unsigned tx_depth; int tx_pool;} config; struct {int active; uint64_t register_ticks,unregister_ticks,cost_count[4],cost_alloc[4],cost_reg[4],cost_unreg[4];} diag; unsigned op_count,tx_pool_available; uint64_t next_id; rm_op_t *ops; void *tx_pool_buffer,*tx_pool_seg; unsigned *tx_pool_free;} uct_rm_urma_iface_t;
static int registrations,unregistrations;
static void *rm_buffer_alloc(size_t n) {void *p=NULL; if(posix_memalign(&p,4096,(n+4095)&~(size_t)4095))return NULL;memset(p,0,(n+4095)&~(size_t)4095);return p;}
static void *rm_register(uct_rm_urma_iface_t *f,void *p,size_t n) {(void)p;(void)n;registrations++;f->diag.register_ticks++;return malloc(1);}
static void rm_diag_unregister(uct_rm_urma_iface_t *f,void *p) {unregistrations++;f->diag.unregister_ticks++;free(p);}
static void urma_unimport_seg(void *p) {(void)p;}
'''
    test=r'''
int main(void) {
 uct_rm_urma_iface_t f={0}; rm_op_t *ops[16]; unsigned stack[16]; int marker;
 f.config.tx_depth=16;f.config.tx_pool=1;f.next_id=1;f.tx_pool_buffer=rm_buffer_alloc(16*4096);f.tx_pool_seg=&marker;f.tx_pool_free=stack;f.tx_pool_available=16;
 for(unsigned i=0;i<16;i++)stack[i]=i;
 for(unsigned i=0;i<16;i++){ops[i]=rm_op_alloc(&f,88);assert(ops[i]&&ops[i]->pooled&&ops[i]->seg==&marker);ops[i]->kind=2;memset(ops[i]->buffer,(int)i+1,88);for(unsigned j=0;j<i;j++)assert(ops[j]->buffer!=ops[i]->buffer);}
 assert(!rm_op_alloc(&f,88));assert(registrations==0&&f.tx_pool_available==0);
 /* A slot held across CQE/ACK waits remains live; releasing others cannot reuse it. */
 for(unsigned i=1;i<16;i++)rm_op_release(&f,ops[i]);
 for(unsigned i=0;i<88;i++)assert(((unsigned char*)ops[0]->buffer)[i]==1);
 for(unsigned i=0;i<15;i++){ops[i+1]=rm_op_alloc(&f,160);assert(ops[i+1]&&ops[i+1]->buffer!=ops[0]->buffer);ops[i+1]->kind=1;}
 assert(!rm_op_alloc(&f,1));
 for(unsigned i=0;i<16;i++)rm_op_release(&f,ops[i]);
 assert(f.op_count==0&&f.tx_pool_available==16&&unregistrations==0);
 assert(!rm_op_alloc(&f,4097));assert(f.tx_pool_available==16);
 rm_op_t *z=rm_op_alloc(&f,0);assert(z&&!z->pooled&&!z->buffer);z->kind=3;rm_op_release(&f,z);
 f.config.tx_pool=0;
 z=rm_op_alloc(&f,160);assert(z&&!z->pooled&&registrations==1);z->kind=1;rm_op_release(&f,z);assert(unregistrations==1&&f.op_count==0);
 free(f.tx_pool_buffer);puts("TX_POOL_ALLOCATOR_MOCK=PASS hardware_validation=NOT_RUN");return 0;
}
'''
    source=prefix+function(text,'rm_op_alloc')+'\n'+function(text,'rm_op_release')+test
    with tempfile.TemporaryDirectory() as d:
        d=pathlib.Path(d); (d/'test.c').write_text(source)
        subprocess.run(['gcc','-std=gnu99','-Wall','-Wextra','-Werror',*(['-fsanitize=address,undefined'] if a.sanitize else []),'-g',str(d/'test.c'),'-o',str(d/'test')],check=True)
        subprocess.run([str(d/'test')],check=True,env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'))
    cleanup=text[text.index('static void rm_cleanup(uct_rm_urma_iface_t *self)\n{'):]
    assert cleanup.index('rm_stop_io(self)')<cleanup.index('while (self->ops)')<cleanup.index('if (self->tx_pool_seg)')
    assert 'buffer quarantined' in cleanup
    print('TX_POOL_CLEANUP_ORDER_CHECK=PASS')
if __name__=='__main__':main()
