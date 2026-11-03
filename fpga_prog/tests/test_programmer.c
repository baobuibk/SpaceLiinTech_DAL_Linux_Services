#include "programmer.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct mock {
    uint8_t memory[8192];
    int reset, owner, wel, released, writes, erases, calls, fail_at;
    int bad_id, no_wel, busy, corrupt, transfers, selected_fpga, handoff_wait_ms;
};
static int fault(struct mock *m) { return ++m->calls == m->fail_at; }
static int gpio(void *ctx, unsigned pin, int value)
{
    struct mock *m = ctx;
    if (fault(m)) return -1;
    if (pin == 0) {
        if (!value) {
            assert(m->owner == 1 && m->selected_fpga && m->handoff_wait_ms == 200);
            m->released++;
        }
        m->reset = value;
    } else {
        assert(m->reset == 1);
        m->owner = value;
        if (value) m->selected_fpga = 1;
    }
    return 0;
}
static int pause_ms(void *ctx, unsigned ms)
{
    struct mock *m = ctx;
    if (fault(m)) return -1;
    if (m->selected_fpga) m->handoff_wait_ms = (int)ms;
    return 0;
}
static int xfer(void *ctx, const uint8_t *tx, uint8_t *rx, size_t n)
{
    struct mock *m = ctx;
    if (fault(m)) return -1;
    assert(m->reset && !m->owner);
    m->transfers++;
    memset(rx, 0, n);
    if (tx[0] == 0x05) { rx[1] = (m->busy ? 1 : 0) | (m->wel ? 2 : 0); return 0; }
    if (tx[0] == 0x06) { m->wel = !m->no_wel; return 0; }
    if (tx[0] == 0x9f) { rx[1] = 1; rx[2] = 0x60; rx[3] = m->bad_id ? 0x19 : 0x18; return 0; }
    assert(n >= 5);
    uint32_t a = ((uint32_t)tx[1]<<24)|((uint32_t)tx[2]<<16)|((uint32_t)tx[3]<<8)|tx[4];
    if (tx[0] == 0x21) {
        assert(m->wel && a % SECTOR_SIZE == 0 && a + SECTOR_SIZE <= sizeof(m->memory));
        memset(m->memory+a, 0xff, SECTOR_SIZE); m->wel = 0; m->erases++;
    } else if (tx[0] == 0x12) {
        assert(m->wel && n-5 <= PAGE_SIZE && a+ n-5 <= sizeof(m->memory));
        assert(a / PAGE_SIZE == (a+n-6) / PAGE_SIZE);
        for (size_t j=0; j<n-5; ++j) m->memory[a+j] &= tx[5+j];
        m->wel=0; m->writes++;
    } else {
        assert(tx[0] == 0x13 && a+n-5 <= sizeof(m->memory));
        memcpy(rx+5, m->memory+a, n-5);
        if (m->corrupt && m->writes) rx[5] ^= 1;
    }
    return 0;
}
static int run(struct mock *m, enum operation op, const uint8_t *data, size_t n, int held)
{
    struct backend b = {m, held, xfer, gpio, pause_ms};
    return program_run(&b, op, data, n);
}
int main(void)
{
    uint8_t image[4103];
    for (size_t i=0; i<sizeof(image); ++i) image[i]=(uint8_t)i;
    struct mock m = {.owner=1};
    assert(run(&m, OP_PROGRAM, image, sizeof(image), 0) == 0);
    assert(!m.reset && m.owner && m.released==1 && m.erases==2 && m.writes==17);
    assert(!memcmp(m.memory, image, sizeof(image)));
    for (size_t i=sizeof(image); i<sizeof(m.memory); ++i) assert(m.memory[i]==0xff);
    int total=m.calls;
    /* Inject an error at every backend operation in a successful update. */
    for (int i=1; i<=total; ++i) {
        m=(struct mock){.owner=1, .fail_at=i};
        assert(run(&m, OP_PROGRAM, image, sizeof(image), 0) != 0);
        assert(!m.released);
        if (i>1) assert(m.reset);
    }
    m=(struct mock){.owner=1,.bad_id=1};
    assert(run(&m, OP_PROGRAM, image, sizeof(image),0)!=0 && !m.erases && m.reset);
    m=(struct mock){.owner=1,.no_wel=1};
    assert(run(&m, OP_PROGRAM,image,sizeof(image),0)!=0 && !m.erases && m.reset);
    m=(struct mock){.owner=1,.busy=1};
    assert(run(&m, OP_ID,NULL,0,0)!=0 && m.reset && !m.released);
    m=(struct mock){.owner=1,.corrupt=1};
    assert(run(&m, OP_PROGRAM,image,sizeof(image),0)!=0 && m.reset && !m.released);
    m=(struct mock){.owner=1};
    assert(run(&m,OP_VERIFY,image,sizeof(image),0)!=0 && !m.erases && m.reset);
    m=(struct mock){.owner=1};
    memcpy(m.memory,image,sizeof(image));
    assert(run(&m,OP_VERIFY,image,sizeof(image),0)==0 && !m.writes && !m.reset);
    m=(struct mock){.owner=1,.reset=1};
    assert(run(&m,OP_ID,NULL,0,1)==0 && m.reset && !m.released);
    m=(struct mock){.owner=0};
    assert(run(&m,OP_RELOAD,NULL,0,0)==0 && !m.reset && m.owner &&
           m.released==1 && !m.transfers && m.handoff_wait_ms==200);
    int reload_calls=m.calls;
    for (int i=1; i<=reload_calls; ++i) {
        m=(struct mock){.owner=0,.fail_at=i};
        assert(run(&m,OP_RELOAD,NULL,0,0)!=0 && !m.released && !m.transfers);
        if (i>1) assert(m.reset);
    }
    m=(struct mock){.owner=1};
    assert(run(&m,OP_PROGRAM,image,FLASH_SIZE+1u,0)!=0 && !m.calls);
    assert(run(&m,OP_PROGRAM,image,0,0)!=0 && !m.calls);
    printf("PASS: page/sector boundaries, ID, WEL, timeout, verification, held reset, bounds; %d fault points\n",total);
    return 0;
}
