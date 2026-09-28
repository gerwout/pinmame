' ARGS: -extat 20000 80 -extat 25000 0 -extat 30001 80
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   waitpeq p7, p7
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        waitpne p7, p7
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        waitpeq p7, p7
        mov     t, cnt
        wrlong  t, ptr
        cogid   t
        cogstop t
p7      long    %10000000
t       long    0
ptr     long    $6000
        long    $C0DEE0D0
