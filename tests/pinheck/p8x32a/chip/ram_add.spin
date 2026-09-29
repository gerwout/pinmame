' ARGS: -ext 2 -extat 30000 82
' EXPECT-SLEEPS: 0
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     n, #0
:a      add     n, ina
        test    p7, ina wz
  if_z  jmp     #:a
        wrlong  n, ptr
        cogid   n
        cogstop n
p7      long    %10000000
n       long    0
ptr     long    $6000
        long    $C0DEE0D0
