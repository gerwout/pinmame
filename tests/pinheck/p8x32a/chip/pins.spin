PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, #$FF
        mov     outa, #1
        mov     outa, #2
        rdlong  t, #0
        mov     outa, #3
        mov     t, cnt
        add     t, #40
        waitcnt t, #0
        mov     outa, #4
        wrlong  t, hubres
        cogid   t
        cogstop t
t       long    0
hubres  long    $6000
        long    $C0DEE0D0
