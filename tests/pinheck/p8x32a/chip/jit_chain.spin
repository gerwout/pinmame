PUB main
DAT
        long    $C0DE5EED
        org     0
' a block reached from another block's exit whose first slot no longer fits (its S field now names CNT): the run
' goes back to the interpreter at that slot; a hub read ends the loop's first block. Results at $6000
entry   mov     k, #300
lp      rdlong  h, hp
        add     acc, h
        test    k, #3 wz
  if_z  movs    lb, #$1F1
  if_nz movs    lb, #r1
        add     r1, #7
        jmp     #lb
        nop
lb      mov     acc2, 0-0
        xor     acc3, acc2
        rol     acc3, #1
        djnz    k, #lp
        wrlong  acc, r0
        wrlong  acc3, r4
        cogid   k
        cogstop k
hp      long    $6100
r0      long    $6000
r4      long    $6004
k       long    0
h       long    0
acc     long    0
acc2    long    0
acc3    long    0
r1      long    $1234
        long    $C0DEE0D0
