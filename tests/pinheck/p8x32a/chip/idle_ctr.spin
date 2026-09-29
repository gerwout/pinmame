' EXPECT-SLEEPS: 8
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     dira, p4
        movi    ctra, #%00100_000
        movs    ctra, #4
        mov     frqa, frq
        mov     n, #4
:hi     test    p4, ina wz
  if_z  jmp     #:hi
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
:lo     test    p4, ina wz
  if_nz jmp     #:lo
        mov     t, cnt
        wrlong  t, ptr
        add     ptr, #4
        djnz    n, #:hi
        mov     frqa, #0
        cogid   t
        cogstop t
p4      long    %10000
frq     long    $00100000
t       long    0
n       long    0
ptr     long    $6000
        long    $C0DEE0D0
