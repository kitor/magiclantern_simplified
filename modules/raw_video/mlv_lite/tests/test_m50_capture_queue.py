#!/usr/bin/env python3
"""Exercise the actual process_frame body with simulated worker backpressure."""
from pathlib import Path
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / 'mlv_lite.c').read_text()
body = source.split('void process_frame(int next_fullsize_buffer_pos)', 1)[1].split('\nstatic REQUIRES(LiveViewTask)', 1)[0]
prefix = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define ASSERT assert
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
#define INC_MOD(a,n) ((a)=((a)+1)%(n))
#define SLOT_CAPTURING 1
#define SLOT_FULL 2
#define RAW_PRE_RECORDING 2
#define MLV_REC_EVENT_STARTED 1
#define FONT_MED 0
#define NotifyBox(...) ((void)0)
#define bmp_printf(...) ((void)0)
typedef struct { int frameNumber,cropPosX,cropPosY,panPosX,panPosY; } mlv_vidf_hdr_t;
typedef mlv_vidf_hdr_t mlv_hdr_t;
struct slot { int frame_number,status; void *ptr; } slots[4];
mlv_vidf_hdr_t payload[4],vidf_hdr;
int frame_count=2,skip_frames,edmac_active,is_m50=1,m50_copy_pending;
int skipped_frames,buffer_full,capture_slot=-1,raw_recording_state=1;
int writing_queue[5],writing_queue_tail,skip_x,skip_y,mlv_start_timestamp;
void *compress_mq=(void*)1;
int posts,fail_post,freed,next_slot;
int msg_queue_post(void *q,int msg) {
    assert(m50_copy_pending); assert(writing_queue_tail==posts);
    posts++; return fail_post;
}
int choose_next_capture_slot() { return next_slot; }
void pre_record_vsync_step(void) {}
void pre_record_discard_frame(void) {}
void mlv_rec_call_cbr(int x,void *p) {}
void frame_add_checks(int x) {}
void mlv_set_timestamp(mlv_hdr_t *p,int x) {}
void rec_dbg_log(const char *s) {}
void free_slot(int n) { freed++; slots[n].status=0; }
void process_frame(int next_fullsize_buffer_pos)
'''
tests = r'''
int main(void) {
    for(int i=0;i<4;i++) slots[i].ptr=&payload[i];
    process_frame(0);
    assert(posts==1 && m50_copy_pending==1 && frame_count==3);
    assert(writing_queue_tail==1 && slots[0].status==SLOT_CAPTURING);
    process_frame(0); /* even 10/12-bit has edmac_active == 0 */
    assert(posts==1 && skipped_frames==1 && writing_queue_tail==1);
    slots[0].status=SLOT_FULL; m50_copy_pending=0; next_slot=1;
    process_frame(0);
    assert(posts==2 && writing_queue_tail==2 && frame_count==4);
    slots[1].status=SLOT_FULL; m50_copy_pending=0; next_slot=2; fail_post=1;
    process_frame(0);
    assert(posts==3 && writing_queue_tail==2 && frame_count==4);
    assert(freed==1 && slots[2].status==0 && m50_copy_pending==0);
    assert(slots[0].status==SLOT_FULL && slots[1].status==SLOT_FULL);
    /* Worker post rejected during pre-recording must release its slot too. */
    posts=writing_queue_tail=0; raw_recording_state=RAW_PRE_RECORDING;
    process_frame(0);
    assert(writing_queue_tail==0 && freed==2 && m50_copy_pending==0);
    return 0;
}
'''
with tempfile.TemporaryDirectory() as temp:
    c=Path(temp)/'capture.c'; exe=Path(temp)/'capture'
    c.write_text(prefix+body+tests)
    subprocess.run(['cc','-std=gnu99','-O2','-fsanitize=address,undefined',str(c),'-o',str(exe)],check=True)
    subprocess.run([str(exe)],check=True)
print('PASS: in-flight exclusion, completed-copy resume, rejected-post cleanup, pre-record cleanup')
