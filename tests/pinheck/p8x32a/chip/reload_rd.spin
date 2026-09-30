' a RDLONG into a slot of translated code: the block with the old word must not run again
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   call    #work
        wrlong  acc, pa
        wrlong  patch, scr
        rdlong  wl2, scr
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
patch   sub     acc, #3
n       long    0
acc     long    0
pa      long    $6000
pb      long    $6004
scr     long    $6100
        long    $C0DEE0D0
