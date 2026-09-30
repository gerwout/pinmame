' six cogs whose events interleave: hub writes and reads of one long, OUTA toggles, NCO writes and INA samples,
' and a cog polling a hub long in an idle loop that another cog writes twice in a row
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     p, #$60
        shl     p, #8
        mov     n, #6
:start  mov     y, p
        shl     y, #16
        or      y, x
        coginit y
        add     p, #$100
        djnz    n, #:start
        cogid   y
        cogstop y
x       long    0
y       long    0
n       long    0
p       long    0
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     ptr, par
        mov     role, par
        shr     role, #8
        and     role, #7 wz
  if_z  jmp     #writer
        cmp     role, #1 wz
  if_z  jmp     #reader
        cmp     role, #2 wz
  if_z  jmp     #toggler
        cmp     role, #4 wz
  if_z  jmp     #poller
        cmp     role, #5 wz
  if_z  jmp     #twice
sampler mov     m, #64
:s      mov     v, ina
        and     v, pins
        wrlong  v, ptr
        add     ptr, #4
        mov     d, m
        and     d, #6
        add     d, #1
:d4     djnz    d, #:d4
        djnz    m, #:s
        jmp     #done
writer  mov     m, #64
:w      wrlong  k, shared
        add     k, #1
        mov     d, k
        and     d, #7
        add     d, #1
:d1     djnz    d, #:d1
        djnz    m, #:w
        jmp     #done
reader  mov     m, #64
:r      rdlong  v, shared
        wrlong  v, ptr
        add     ptr, #4
        mov     d, m
        and     d, #3
        add     d, #1
:d2     djnz    d, #:d2
        djnz    m, #:r
        jmp     #done
toggler mov     dira, pins
        movs    ctra, #6
        movi    ctra, #$20
        mov     m, #64
:t      xor     outa, pin5
        mov     frqa, m
        shl     frqa, #24
        mov     d, m
        and     d, #5
        add     d, #1
:d3     djnz    d, #:d3
        djnz    m, #:t
        mov     frqa, #0
poller  mov     m, #16
:p      rdlong  v, flag
        cmp     v, last wz
  if_z  jmp     #:p
        mov     last, v
        wrlong  v, ptr
        add     ptr, #4
        djnz    m, #:p
        jmp     #done
twice   mov     d, cnt
        add     d, d3000
        mov     m, #16
:tw     waitcnt d, d_gap
        wrlong  m, flag
        add     m, #100
        wrlong  m, flag
        sub     m, #100
        add     d_gap, #7
        djnz    m, #:tw
done    cogid   v
        cogstop v
pins    long    %1100000
pin5    long    %100000
shared  long    $5F00
flag    long    $5F04
last    long    0
d3000   long    3000
d_gap   long    200
k       long    1
ptr     long    0
role    long    0
m       long    0
d       long    0
v       long    0
        long    $C0DEE0D0
