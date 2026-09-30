' self-modifying and translated local code: every result is written to hub and compared with the RTL
PUB main
DAT
        long    $C0DE5EED
        org     0
entry   mov     n, #20
l_disp  mov     t, n
        and     t, #3
        add     t, #tab
        movs    l_jmp, t
        add     acc, n
l_jmp   jmp     #0-0
c0      add     acc, #1
        jmp     #dnext
c1      xor     acc, n
        jmp     #dnext
c2      rcl     acc, #3 wc
        jmp     #dnext
c3      sar     acc, n wz
        muxz    acc, #$80
dnext   djnz    n, #l_disp
        wrlong  acc, ptr
        add     ptr, #4
        mov     n, #2
l_twice movs    l_nx, #7
l_nx    mov     x, #9
        wrlong  x, ptr
        add     ptr, #4
        djnz    n, #l_twice
        mov     n, #8
        mov     t, #table
l_md    movd    l_wr, t
        add     t, #1
        nop
l_wr    mov     0-0, n
        djnz    n, #l_md
        mov     n, #8
        mov     t, #table
        mov     x, #0
l_sum   movs    l_add, t
        add     t, #1
        nop
l_add   add     x, 0-0
        rol     x, #5
        djnz    n, #l_sum
        wrlong  x, ptr
        add     ptr, #4
        mov     x, #$55
        mov     y, #0
        test    x, #1 wc
        muxc    y, #$F0
        muxnc   y, #$0F
        test    x, #0 wz
        muxz    y, #$100
        muxnz   y, m200
        cmps    x, y wc, wz
        muxc    y, m400
        muxz    y, m800
        rcr     y, #4 wc
        muxc    y, m1000
        ror     y, x
        movi    y, #$1A5
        wrlong  y, ptr
        add     ptr, #4
        mov     n, #6
        mov     acc, #0
l_dyn   movs    l_tgt, n
        jmp     #l_run
l_run   mov     y, y
l_tgt   add     acc, #0-0
        cmp     n, #3 wz
  if_z  movi    l_tgt, #%100001_001
        djnz    n, #l_dyn
        wrlong  acc, ptr
        add     ptr, #4
        mov     n, #3
        mov     acc, #0
l_b     add     acc, #1
        movs    l_b, n
        shl     acc, #1
        djnz    n, #l_b
        wrlong  acc, ptr
        add     ptr, #4
        mov     x, #0
        tjz     x, #l_z1
        or      acc, #1
l_z1    tjnz    x, #l_z2
        or      acc, #2
l_z2    mov     x, #1
        tjnz    x, #l_z3
        or      acc, #4
l_z3    sub     x, #1 wz, wc
if_z    or      acc, #8
if_c    or      acc, #16
        mov     x, #0
        sub     x, #1 wc
if_c    or      acc, #32
        djnz    x, #l_z4
l_z4    wrlong  acc, ptr
        add     ptr, #4
        cogid   t
        cogstop t
tab     jmp     #c0
        jmp     #c1
        jmp     #c2
        jmp     #c3
n       long    0
t       long    0
x       long    0
y       long    0
acc     long    $12345
ptr     long    $6000
m200    long    $200
m400    long    $400
m800    long    $800
m1000   long    $1000
table   res     8
        long    $C0DEE0D0
