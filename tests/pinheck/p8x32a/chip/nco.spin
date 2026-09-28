PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     dira, #%111110
        mov     ctra, ncoa
        mov     frqa, fa
        mov     ctrb, ncob
        mov     frqb, fb
        mov     t, cnt
        add     t, #400
        waitcnt t, #0
        mov     frqa, fhi
        mov     frqb, fhalf
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        mov     frqb, #0
        mov     phsb, data
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        shl     phsb, #1
        mov     u, ina
        wrlong  u, ptr
        add     ptr, #4
        andn    dira, #%10
        mov     t, cnt
        add     t, #60
        waitcnt t, #0
        or      dira, #%10
        mov     frqa, fa
        mov     ctrb, ncob1
        mov     frqb, fb
        mov     t, cnt
        add     t, #300
        waitcnt t, #0
        mov     u, ina
        wrlong  u, ptr
        add     ptr, #4
        mov     t, cnt
        add     t, idle
        waitcnt t, #0
        mov     ctra, #0
        mov     ctrb, ncob
        mov     t, cnt
        add     t, #100
        waitcnt t, #0
        mov     u, phsb
        wrlong  u, ptr
        cogstop x
        cogid   t
        cogstop t
x       long    0
t       long    0
u       long    0
ncoa    long    %00100 << 26 | 1
ncob    long    %00101 << 26 | 3 << 9 | 2
ncob1   long    %00100 << 26 | 2
fa      long    $0100_0000
fb      long    $4000_0000
fhi     long    $C000_0000
fhalf   long    $7FFF_FFFF
data    long    $A5C3_0000
ptr     long    $6000
idle    long    20000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%10000
        mov     ctra, nco4
        mov     frqa, f4
        mov     a, #$60
        shl     a, #8
        add     a, #32
        mov     s, cnt
        mov     n, #3
:own    waitpne p4, p4
        waitpeq p4, p4
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        djnz    n, #:own
        waitpeq p1, p1
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        waitpne p1, p1
        mov     e, cnt
        sub     e, s
        wrlong  e, a
        add     a, #4
        mov     e, ina
        wrlong  e, a
:spin   jmp     #:spin
nco4    long    %00100 << 26 | 4
f4      long    $0080_0000
p1      long    %10
p4      long    %10000
n       long    0
s       long    0
e       long    0
a       long    0
