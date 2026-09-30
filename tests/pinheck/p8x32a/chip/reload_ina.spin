' an instruction with an INA source writing a slot of translated code: the block with the old word must not run again
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   or      dira, p26
        or      outa, p26
        call    #work
        wrlong  acc, pa
        xor     wl2, ina
        call    #work
        wrlong  acc, pb
        cogid   n
        cogstop n
work    mov     n, #200
        mov     acc, #0
wl      add     acc, #1
wl2     add     acc, #3
        xor     acc, n
        djnz    n, #wl
work_ret ret
p26     long    1 << 26
n       long    0
acc     long    0
pa      long    $6000
pb      long    $6004
        long    $C0DEE0D0
