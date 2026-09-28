PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        coginit x wr
        mov     n, #16
        mov     d, dstart
:loop   mov     t, cnt
        add     t, d
        waitcnt t, #0
        mov     y, wk
        shl     y, #2
        mov     z, n
        shl     z, #2
        add     z, pbase
        shl     z, #16
        or      y, z
        or      y, x
        coginit y
        add     d, #1
        djnz    n, #:loop
        mov     y, wk
        shl     y, #2
        mov     z, sleepv
        shl     z, #16
        or      y, z
        or      y, x
        coginit y
        mov     t, cnt
        add     t, long1
        waitcnt t, #0
        cogstop x
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        cogid   t
        cogstop t
x       long    0
y       long    0
z       long    0
t       long    0
n       long    0
d       long    0
pbase   long    $6000
sleepv  long    $6400
long1   long    12000
dstart  long    9000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  mov     dira, #%10
        cmp     par, sleepv2 wz
if_z    jmp     #:sleep
:l      mov     v, cnt
        wrlong  v, par
        xor     outa, #%10
        jmp     #:l
:sleep  mov     w, cnt
        add     w, big
        waitcnt w, #0
        wrlong  w, par
sleepv2 long    $6400
big     long    $10000000
w       long    0
v       long    0
