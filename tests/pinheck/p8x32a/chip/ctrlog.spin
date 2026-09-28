' EXPECT-LOG: p8x32a: pin-sensing counter mode not modelled
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     ctra, pos
        mov     frqa, #1
        mov     t, cnt
        add     t, #100
        waitcnt t, #0
        cogid   t
        cogstop t
pos     long    %01010 << 26 | 5
t       long    0
        long    $C0DEE0D0
