PUB main
DAT
        long    $C0DE5EED
        org     0
' a translated block rewrites two of its own fixed slots (each ADD's immediate) and runs again: both new words must
' run. The sum at $6000
entry   mov     k, #100
lp      xor     junk, k
s1      add     acc, #1
s2      add     acc, #2
        add     s1, #1
        add     s2, #3
        djnz    k, #lp
        wrlong  acc, r0
        cogid   k
        cogstop k
r0      long    $6000
k       long    0
acc     long    0
junk    long    0
        long    $C0DEE0D0
