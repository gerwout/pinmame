' EXPECT-SLEEPS: 0
PUB main
DAT
        long    $C0DE5EED
        org     0
' CNT and PAR as sources in translated runs (some conditional), then a loop that reads CNT and changes nothing until
' CNT passes a time (it never sleeps); results at $6000
entry   mov     k, #40
:l      add     acc, cnt
        sub     acc, par
        test    k, #3 wc
  if_c  xor     acc, cnt
  if_nc add     acc2, cnt
        rol     acc2, #3
        djnz    k, #:l
        mov     x, cnt
        add     x, d3000
:w      cmp     x, cnt wc
  if_nc jmp     #:w
        mov     y, cnt
        sub     y, x
        wrlong  acc, r0
        wrlong  acc2, r1
        wrlong  y, r2
        cogid   k
        cogstop k
d3000   long    3000
r0      long    $6000
r1      long    $6004
r2      long    $6008
k       long    0
acc     long    $1234
acc2    long    0
x       long    0
y       long    0
        long    $C0DEE0D0
