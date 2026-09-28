PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     x, wk
        shl     x, #2
        or      x, #%1000
        mov     par1, #$60
        shl     par1, #8
        mov     n, #7
:start  mov     y, par1
        shl     y, #16
        or      y, x
        coginit y wc, wr
        muxc    f, #1
        wrlong  y, ptr
        add     ptr, #4
        add     par1, #16
        djnz    n, #:start
        mov     y, x
        coginit y wc, wr
        wrlong  y, ptr
        add     ptr, #4
        muxc    f, #2
        mov     n, #9
:locks  locknew y wc
        wrlong  y, ptr
        add     ptr, #4
        muxc    f, #4
        djnz    n, #:locks
        mov     y, #3
        lockset y wc
        muxc    f, #8
        lockset y wc
        muxc    f, #16
        lockclr y wc
        muxc    f, #32
        lockret y
        locknew y wc
        wrlong  y, ptr
        add     ptr, #4
        mov     y, #5
        cogstop y
        mov     y, #6
        cogstop y
        mov     t, cnt
        add     t, #200
        waitcnt t, #0
        cogid   y
        wrlong  y, ptr
        add     ptr, #4
        wrlong  f, ptr
        cogstop y
x       long    0
y       long    0
t       long    0
n       long    0
f       long    0
par1    long    0
ptr     long    $6000
wk      long    $C0DEADD1
        long    $C0DEE0D0
        long    $C0DE0B0B
        org     0
worker  cogid   id
        mov     b, #1
        shl     b, id
        or      dira, b
        or      outa, b
        mov     a, par
        add     a, #4
        wrlong  id, par
        wrlong  cnt, a
        mov     w, cnt
        add     w, #400
        waitcnt w, #0
        andn    outa, b
        cogstop id
id      long    0
b       long    0
a       long    0
w       long    0
