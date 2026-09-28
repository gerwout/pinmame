PUB main
DAT
        long    $C0DE5EED
        org     0
entry   clkset  pll
        mov     dira, p24
loop    waitpeq p25, p25
        mov     t, cnt
        test    p26, ina wc
        muxc    outa, p24
        wrlong  t, ptr
        add     ptr, #4
        add     count, #1
        wrlong  count, addr
        waitpne p25, p25
        jmp     #loop
pll     long    $6F
p24     long    1 << 24
p25     long    1 << 25
p26     long    1 << 26
count   long    0
addr    long    $7FF0
ptr     long    $1000
t       long    0
        long    $C0DEE0D0
