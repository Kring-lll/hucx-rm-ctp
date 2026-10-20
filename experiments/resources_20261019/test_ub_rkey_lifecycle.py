"""Compile the actual cache functions with mocked SDK/hash-table dependencies."""
from pathlib import Path
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[2]

def function(text,name):
    pos=text.index(name+'(')
    start=text.rfind('\nstatic ',0,pos)+1
    brace=text.index('{',pos); depth=1; end=brace+1
    while depth:
        depth+=(text[end]=='{')-(text[end]=='}'); end+=1
    return text[start:end]

def main():
    hucx=ROOT/'hucx' if (ROOT/'hucx').is_dir() else ROOT
    src=(hucx/'src/uct/urma/base/urma_md.c').read_text()
    hdr=(hucx/'src/uct/urma/base/urma_md.h').read_text()
    begin=hdr.index('typedef struct uct_ub_rkey_hash_key')
    end=hdr.index('} uct_ub_rkey_hash_key_t;',begin)+len('} uct_ub_rkey_hash_key_t;')
    prefix=r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>
#define UCS_F_ALWAYS_INLINE inline
#define UCS_OK 0
#define UCS_ERR_NO_MEMORY -1
#define UCS_ERR_NO_ELEM -2
#define UCS_ERR_IO_ERROR -3
#define UCS_ERR_INVALID_PARAM -4
#define UCS_ERR_NO_RESOURCE -5
#define UCS_KH_PUT_FAILED -1
#define UCS_KH_PUT_KEY_PRESENT 0
#define URMA_SUCCESS 0
#define ucs_error(...) ((void)0)
#define ucs_warn(...) ((void)0)
#define ucs_debug(...) ((void)0)
typedef int ucs_status_t;
typedef int urma_status_t;
typedef void uct_component_t;
typedef uintptr_t uct_rkey_t;
typedef unsigned khiter_t;
typedef struct { unsigned char raw[16]; } urma_eid_t;
typedef struct { struct { urma_eid_t eid; uint32_t uasid; uint64_t va; } ubva;
 uint64_t len; union { uint32_t value; } attr; uint32_t token_id; } urma_seg_t;
typedef struct { urma_seg_t remote_seg; void *import_tseg; size_t ref_cnt;
 pthread_rwlock_t import_tseg_lock; } uct_ub_rkey_info_t;
typedef struct { urma_eid_t eid; uint32_t uasid; uint64_t seg_va,seg_len;
 uint32_t seg_flag,seg_token_id; } uct_ub_seg_info_t;
'''
    shims=r'''
static uct_ub_rkey_hash_key_t keys[4];
static uct_ub_rkey_info_t *values[4];
static int used[4],g_uct_ub_rkey_lock;
static int fail_alloc,unimport_status,unimports;
static unsigned lookup(uct_ub_rkey_hash_key_t key) {
 for(unsigned i=0;i<4;i++) if(used[i] && uct_ub_rkey_hash_equal(keys[i],key)) return i;
 return 4;
}
static unsigned insert(uct_ub_rkey_hash_key_t key,int *ret) {
 unsigned i=lookup(key); if(i<4) {*ret=0;return i;}
 for(i=0;i<4;i++) if(!used[i]) {used[i]=1;keys[i]=key;*ret=1;return i;}
 *ret=-1;return 4;
}
static void *alloc_elem(size_t n,size_t z) {return fail_alloc?NULL:calloc(n,z);}
static int urma_unimport_seg(void *p) {assert(p);unimports++;return unimport_status;}
#define ucs_calloc(n,z,label) alloc_elem(n,z)
#define ucs_spin_lock(p) ((void)(p))
#define ucs_spin_unlock(p) ((void)(p))
#define kh_put(n,t,k,r) insert(k,r)
#define kh_get(n,t,k) lookup(k)
#define kh_value(t,i) values[i]
#define kh_end(t) 4
#define kh_del(n,t,i) (used[i]=0)
static int live(void) {int n=0;for(int i=0;i<4;i++)n+=used[i];return n;}
static uct_ub_seg_info_t desc(uint64_t len) {
 uct_ub_seg_info_t d={0};d.seg_va=4096;d.uasid=1;d.seg_len=len;
 d.seg_token_id=7;d.seg_flag=3;return d;
}
'''
    tests=r'''
int main(void) {
 void *handle;uct_rkey_t a,b;uct_ub_seg_info_t small=desc(262144),large=desc(524288);
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 assert(uct_ub_rkey_unpack(NULL,&large,&b,&handle)==0);
 assert(a!=b && live()==2); /* changed length must not alias */
 assert(uct_ub_rkey_release(NULL,a,NULL)==0);
 assert(uct_ub_rkey_release(NULL,b,NULL)==0 && live()==0);
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 assert(uct_ub_rkey_unpack(NULL,&small,&b,&handle)==0 && a==b);
 assert(((uct_ub_rkey_info_t*)a)->ref_cnt==2);
 assert(uct_ub_rkey_release(NULL,a,NULL)==0 && live()==1);
 assert(((uct_ub_rkey_info_t*)b)->ref_cnt==1);
 assert(uct_ub_rkey_release(NULL,b,NULL)==0 && live()==0); /* RM: no import */
 uct_ub_seg_info_t attr=small;attr.seg_flag++;
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 assert(uct_ub_rkey_unpack(NULL,&attr,&b,&handle)==0 && a!=b);
 assert(uct_ub_rkey_release(NULL,a,NULL)==0);
 assert(uct_ub_rkey_release(NULL,b,NULL)==0 && live()==0);
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 ((uct_ub_rkey_info_t*)a)->import_tseg=(void*)1;
 assert(uct_ub_rkey_release(NULL,a,NULL)==0 && unimports==1 && live()==0);
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 ((uct_ub_rkey_info_t*)a)->import_tseg=(void*)1;unimport_status=1;
 assert(uct_ub_rkey_release(NULL,a,NULL)==UCS_ERR_IO_ERROR && live()==1);
 assert(((uct_ub_rkey_info_t*)a)->ref_cnt==1);
 unimport_status=0;assert(uct_ub_rkey_release(NULL,a,NULL)==0 && live()==0);
 fail_alloc=1;assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==UCS_ERR_NO_MEMORY);
 assert(live()==0);fail_alloc=0;
 assert(uct_ub_rkey_unpack(NULL,&small,&a,&handle)==0);
 ((uct_ub_rkey_info_t*)a)->ref_cnt=0;
 assert(uct_ub_rkey_release(NULL,a,NULL)==UCS_ERR_INVALID_PARAM);
 assert(((uct_ub_rkey_info_t*)a)->ref_cnt==0);
 ((uct_ub_rkey_info_t*)a)->ref_cnt=1;
 assert(uct_ub_rkey_release(NULL,a,NULL)==0 && live()==0);
 puts("UB_RKEY_LIFECYCLE_SELFTEST=PASS mocked_SDK=1 cases=7");
}
'''
    code=prefix+hdr[begin:end]+'\n'+function(hdr,'uct_ub_rkey_hash_equal')+'\n'+shims+'\n'+function(src,'uct_ub_rkey_unpack')+'\n'+function(src,'uct_ub_rkey_release')+'\n'+tests
    with tempfile.TemporaryDirectory(prefix='ub_rkey_test_') as tmp:
        c=Path(tmp)/'test.c';exe=Path(tmp)/'test';c.write_text(code)
        subprocess.run(['gcc','-std=gnu99','-Wall','-Wextra','-Wno-unused-parameter','-pthread',str(c),'-o',str(exe)],check=True)
        subprocess.run([str(exe)],check=True)
if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    args=parser.parse_args()
    ROOT=args.source_root.resolve()
    main()
