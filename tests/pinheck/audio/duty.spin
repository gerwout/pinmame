PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, pins
        mov     ctra, dutyl
        mov     frqa, f0
        mov     ctrb, dutyr
        mov     frqb, f1
        mov     t, cnt
        add     t, d1
        waitcnt t, d1
        mov     frqa, f2
        mov     frqb, f3
        waitcnt t, d1
        mov     frqa, f4
        mov     frqb, #0
        waitcnt t, d1
        mov     ctra, #0
        or      outa, p14
        waitcnt t, d1
        mov     ctra, dutyl
        andn    outa, p14
        andn    dira, p15
        waitcnt t, d1
        or      dira, p15
        mov     frqa, f5
        mov     frqb, f5
        waitcnt t, d1
        cogid   t
        cogstop t
t       long    0
d1      long    3000
pins    long    (1 << 14) | (1 << 15)
p14     long    1 << 14
p15     long    1 << 15
dutyl   long    %00110 << 26 | 15
dutyr   long    %00110 << 26 | 14
f0      long    $4000_0000
f1      long    $0100_0000
f2      long    $FFFF_FFFF
f3      long    $8000_0000
f4      long    $1234_5678
f5      long    $3000_0000
        long    $C0DEE0D0
